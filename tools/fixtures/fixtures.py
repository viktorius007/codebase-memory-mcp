#!/usr/bin/env python3
"""Graph fixtures: check the code graph against the compiler's and interpreter's own run.

    python3 -I -B tools/fixtures/fixtures.py --bin <binary> [--only NAME ...] [--log FILE]
    (normally: make -f Makefile.cbm fixtures BUILD_DIR=<dir>)

For every fixture under tests/fixtures/graph/<rust|python>/<name>/ the check

  1. runs the program under its own toolchain and reads what happened (the truth);
  2. copies the fixture to a directory outside any git working tree and indexes it with <binary>
     in both pipeline modes (sequential, and parallel with 60 pad files added);
  3. compares the graph with the truth, edge by edge and site by site;
  4. fails on every mismatch that tools/fixtures/known.tsv does not list, on every listed entry
     that no longer mismatches, and on every site whose result differs between the two modes.

Outcome of one site (a call, a class base, a name use or a write that the run proved):
  right      the graph has the edge to the target the run reached
  WRONG      the graph has an edge to something else
  PHANTOM    the graph has an edge the run never produced
  withheld   no edge, and an unresolved_sites row records the stop (accepted)
  FALSE-STOP withheld, but the recorded reason denies something the run just did (STOP_FALSE)
  SILENT     no edge and no row
Everything but right and withheld is a mismatch.

Rust truth.  Each fixture function that matters prints with
    println!("text\\n{}", std::backtrace::Backtrace::force_capture())
so the run prints its own call stack.  The first frame inside the fixture is the callee, the next
one is the call site.  Both are mapped to graph nodes by file and line only (the innermost
function or method node that contains the line), never by name, so a fixture line holds at most
one function.  Programs are compiled with rustc -C debuginfo=1 and the edition of their
Cargo.toml; a workspace is built member by member, dependencies first.

Python truth.  tools/fixtures/pytrace.py imports every module of the fixture under the running
interpreter and records class bases, calls, name reads and name writes as they happen.

Authoring a fixture: put it in a new directory with a PROVENANCE file (one line naming the
review and finding), add the print statements above, run the check, and list a site that is
known to mismatch in tools/fixtures/known.tsv with the finding it belongs to.
"""
import argparse
import concurrent.futures as cf
import json
import os
import re
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FIXDIR = os.path.join(ROOT, "tests", "fixtures", "graph")
KNOWN = os.path.join(ROOT, "tools", "fixtures", "known.tsv")
PYTRACE = os.path.join(ROOT, "tools", "fixtures", "pytrace.py")
INDEX_LOCAL = os.path.join(ROOT, "scripts", "index-local.sh")
PROJECT = "fx"
PAD_FILES = 60  # the parallel pipeline starts above 50 files
MODES = ("sequential", "parallel")

# Stop reasons that assert a fact about the world.  A run that executed a project definition
# of the called name contradicts them, so such a stop is a mismatch even though it is recorded.
STOP_FALSE = {
    "method_not_found", "function_not_in_registry", "module_function_not_registered",
    "associated_item_not_registered",
}


class Die(Exception):
    pass


def die(msg):
    raise Die(msg)


# ───────────────────────────── graph ─────────────────────────────

class Graph:
    def __init__(self, db):
        con = sqlite3.connect("file:%s?mode=ro" % db, uri=True)
        pre = PROJECT + "."
        self.nodes = {}
        for i, label, name, qn, fp, s, e in con.execute(
                "select id,label,name,qualified_name,file_path,start_line,end_line from nodes"):
            if qn.startswith(pre):
                qn = qn[len(pre):]
            self.nodes[i] = dict(id=i, label=label, name=name, qn=qn, file=fp, start=s, end=e)
        self.edges = []
        for t, s, d, p in con.execute("select type,source_id,target_id,properties from edges "
                                      "where type in ('CALLS','INHERITS','USAGE','WRITES')"):
            try:
                props = json.loads(p or "{}")
            except ValueError:
                props = {}
            self.edges.append(dict(type=t, src=self.nodes[s]["qn"], dst=self.nodes[d]["qn"],
                                   dstnode=self.nodes[d], line=props.get("line"),
                                   name=props.get("callee")))
        self.unres = []
        for fp, kind, line, caller, name, reason in con.execute(
                "select file_path,kind,line,caller,name,reason from unresolved_sites"):
            if caller.startswith(pre):
                caller = caller[len(pre):]
            self.unres.append(dict(file=fp, kind=kind, line=line, caller=caller, name=name,
                                   reason=reason))
        con.close()
        self.by_qn = {n["qn"]: n for n in self.nodes.values()}

    def declares(self, qn, impl_qn):
        """True when qn is the declaration of a trait method that impl_qn (m[Trait]) implements."""
        n = self.by_qn.get(qn)
        owner = self.by_qn.get(qn.rsplit(".", 1)[0]) if n and "." in qn else None
        return bool(owner and owner["label"] == "Interface" and impl_qn.endswith("[%s]" % owner["name"])
                    and n["name"] == impl_qn.rsplit("[", 1)[0].rsplit(".", 1)[-1])

    def def_at(self, file, line):
        """Innermost Function/Method node whose lines contain file:line."""
        best = [n for n in self.nodes.values()
                if n["label"] in ("Function", "Method") and n["file"] == file and n["start"] <= line <= n["end"]]
        if not best:
            return None
        best.sort(key=lambda n: n["end"] - n["start"])
        if len(best) > 1 and best[0]["end"] - best[0]["start"] == best[1]["end"] - best[1]["start"]:
            die("two graph definitions share %s:%d (%s, %s): put one function per line in the fixture"
                % (file, line, best[0]["qn"], best[1]["qn"]))
        return best[0]


# ───────────────────────────── comparison ─────────────────────────────

class Fact:
    """One thing the run proved: an edge of `etype` from `src` to `dst` (graph QNs)."""

    def __init__(self, etype, src, dst, name, line=None):
        self.etype, self.src, self.dst, self.name, self.line = etype, src, dst, name, line

    @property
    def site(self):
        return "%s %s -> %s" % (self.etype, self.src, self.dst)


def stop_kinds(etype):
    return {"CALLS": ("call",), "INHERITS": ("type",), "USAGE": ("reference", "type", "member"),
            "WRITES": ("reference", "member")}[etype]


def compare(g, facts, executed, judge_target):
    # executed None: every source counts as run (a Rust fixture calls what it defines)
    """Return {site: outcome} for every fact, and for every edge from an executed source that
    no fact backs and judge_target accepts."""
    out = {}
    have = {(f.etype, f.src, f.dst) for f in facts}
    rivals = set()
    for f in facts:
        edges = [e for e in g.edges if e["type"] == f.etype and e["src"] == f.src]
        if any(e["dst"] == f.dst for e in edges):
            out[f.site] = "right"
            continue
        rival = [e for e in edges if f.name and e["dstnode"]["name"] == f.name
                 and (f.line is None or e["line"] in (None, f.line))
                 and (f.etype, f.src, e["dst"]) not in have]
        if rival and all(g.declares(e["dst"], f.dst) for e in rival):
            out[f.site] = "right"  # generic or dyn dispatch: the trait's declaration is the provable target
            continue
        if rival:
            out[f.site] = "WRONG -> " + ", ".join(sorted({e["dst"] for e in rival}))
            rivals |= {(f.etype, f.src, e["dst"]) for e in rival}
            continue
        rows = [u for u in g.unres if u["caller"] == f.src and u["kind"] in stop_kinds(f.etype)
                and u["name"].split(".")[-1] == (f.name or "") and (f.line is None or u["line"] == f.line)]
        if rows:
            reasons = sorted({u["reason"] for u in rows})
            bad = any(r in STOP_FALSE for r in reasons)
            out[f.site] = ("FALSE-STOP " if bad else "withheld ") + ",".join(reasons)
            continue
        out[f.site] = "SILENT"
    for e in g.edges:
        key = (e["type"], e["src"], e["dst"])
        if key in have or key in rivals or (executed is not None and e["src"] not in executed) \
                or not judge_target(e):
            continue
        out["%s %s -> %s" % key] = "PHANTOM"
    return out


def is_mismatch(outcome):
    return not (outcome == "right" or outcome.startswith("withheld "))


# ───────────────────────────── rust truth ─────────────────────────────

def run(cmd, cwd, env=None, timeout=300):
    return subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True, timeout=timeout)


def rust_packages(root):
    import tomllib
    def load(path):
        with open(path, "rb") as f:
            return tomllib.load(f)
    top = load(os.path.join(root, "Cargo.toml"))
    dirs = [""] if "package" in top else []
    dirs += top.get("workspace", {}).get("members", [])
    pk = {}
    for d in dirs:
        t = load(os.path.join(root, d, "Cargo.toml")) if d else top
        p = t["package"]
        deps = {k: v["path"] for k, v in t.get("dependencies", {}).items() if isinstance(v, dict) and "path" in v}
        pk[p["name"]] = dict(dir=d, edition=p.get("edition", "2015"), deps=deps)
    return pk


def rust_truth(root, scratch):
    """Compile and run every binary of the fixture; return the stdout of the runs."""
    rustc = os.environ.get("FIXTURES_RUSTC", "rustc")
    pk = rust_packages(root)
    out = os.path.join(scratch, "bin")
    os.makedirs(out, exist_ok=True)
    built, order, seen = {}, [], set()

    def visit(name):
        if name not in seen:
            seen.add(name)
            for dep in pk[name]["deps"]:
                visit(dep)
            order.append(name)
    for name in pk:
        visit(name)
    texts = []
    for name in order:
        p = pk[name]
        base = p["dir"] + "/" if p["dir"] else ""
        common = [rustc, "--edition", p["edition"], "-A", "warnings", "-C", "debuginfo=1", "-L", "dependency=" + out]
        for dep in p["deps"]:
            common += ["--extern", "%s=%s" % (dep.replace("-", "_"), built[dep])]
        crate = name.replace("-", "_")
        if os.path.isfile(os.path.join(root, base + "src/lib.rs")):
            rlib = os.path.join(out, "lib%s.rlib" % crate)
            r = run(common + ["--crate-type", "lib", "--crate-name", crate, "-o", rlib, base + "src/lib.rs"], root)
            if r.returncode:
                die("rustc failed on %s lib: %s" % (name, r.stderr.strip()[:400]))
            built[name] = rlib
            common += ["--extern", "%s=%s" % (crate, rlib)]
        bins = []
        if os.path.isfile(os.path.join(root, base + "src/main.rs")):
            bins.append(base + "src/main.rs")
        bdir = os.path.join(root, base + "src/bin")
        if os.path.isdir(bdir):
            bins += [base + "src/bin/" + f for f in sorted(os.listdir(bdir)) if f.endswith(".rs")]
        for b in bins:
            exe = os.path.join(out, re.sub(r"\W", "_", name + "_" + b))
            r = run(common + ["--crate-name", re.sub(r"\W", "_", name + "_" + os.path.basename(b)[:-3]), "-o", exe, b], root)
            if r.returncode:
                die("rustc failed on %s: %s" % (b, r.stderr.strip()[:400]))
            r = run([exe], root)
            if r.returncode:
                die("%s exited %d: %s" % (b, r.returncode, r.stderr.strip()[:300]))
            texts.append(r.stdout)
    return "\n".join(texts)


FRAME = re.compile(r"^\s*(\d+): (.*)$")
AT = re.compile(r"^\s+at (.+):(\d+):(\d+)$")


def parse_traces(text):
    """One list of (symbol, (file, line) or None) per printed backtrace."""
    traces, cur = [], None
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        m = FRAME.match(lines[i])
        if m:
            if m.group(1) == "0":
                cur = []
                traces.append(cur)
            loc = None
            if i + 1 < len(lines):
                a = AT.match(lines[i + 1])
                if a:
                    loc = (a.group(1), int(a.group(2)))
                    i += 1
            if cur is not None:
                cur.append((m.group(2), loc))
        i += 1
    return traces


def rust_facts(g, text):
    facts, executed, unmapped = {}, set(), set()
    traces = parse_traces(text)
    if not traces:
        die("the run printed no backtrace: the fixture has no instrumented print statement")

    def project(loc):
        return loc is not None and loc[0].startswith("./")
    for fr in traces:
        first = next((k for k, (_, loc) in enumerate(fr) if project(loc)), None)
        if first is None:
            continue
        cfile, cline = fr[first][1][0][2:], fr[first][1][1]
        callee = g.def_at(cfile, cline)
        if callee is None:
            unmapped.add((cfile, cline))
            continue
        executed.add(callee["qn"])
        nxt = fr[first + 1][1] if first + 1 < len(fr) else None
        if not project(nxt):
            continue  # entered from the standard library: no call site in the fixture
        caller = g.def_at(nxt[0][2:], nxt[1])
        if caller is None:
            unmapped.add((nxt[0][2:], nxt[1]))
            continue
        executed.add(caller["qn"])
        facts.setdefault(("CALLS", caller["qn"], callee["qn"], nxt[1]),
                         Fact("CALLS", caller["qn"], callee["qn"], callee["name"], nxt[1]))
    return list(facts.values()), executed, sorted(unmapped)


def rust_traceable(root, g):
    """Definitions whose body holds an instrumented print (found in the source text)."""
    qns = set()
    for dirpath, _, files in os.walk(root):
        for f in files:
            if f.endswith(".rs"):
                path = os.path.join(dirpath, f)
                rel = os.path.relpath(path, root)
                with open(path, encoding="utf-8") as fh:
                    for n, line in enumerate(fh, 1):
                        if "force_capture" in line:
                            d = g.def_at(rel, n)
                            if d:
                                qns.add(d["qn"])
    return qns


# ───────────────────────────── python truth ─────────────────────────────

def python_truth(root, scratch):
    r = run([sys.executable, "-I", "-B", PYTRACE, root], root, timeout=300)
    if r.returncode:
        die("pytrace failed: %s" % (r.stderr.strip()[-400:] or r.stdout.strip()[-400:]))
    return json.loads(r.stdout)


def python_facts(tr):
    facts = [Fact(e["type"], e["src"], e["dst"], e["name"], e.get("line")) for e in tr["facts"]]
    return facts, set(tr["executed"])


def python_judged(e):
    return e["dstnode"]["label"] in ("Class", "Function", "Method", "Variable")


# ───────────────────────────── index ─────────────────────────────

def git_free(path):
    r = subprocess.run(["git", "-C", path, "rev-parse", "--git-dir"], capture_output=True, text=True)
    return r.returncode != 0


def stage(fixture, lang, dest, pad):
    shutil.copytree(fixture, dest, ignore=shutil.ignore_patterns("PROVENANCE", "target", "__pycache__"))
    if pad:
        d = os.path.join(dest, "cbm_pad")
        os.makedirs(d)
        for i in range(PAD_FILES):
            ext = "rs" if lang == "rust" else "py"
            body = "pub fn cbm_pad_%d() -> u32 { %d }\n" % (i, i) if lang == "rust" else \
                   "def cbm_pad_%d():\n    return %d\n" % (i, i)
            with open(os.path.join(d, "cbm_pad%d.%s" % (i, ext)), "w") as f:
                f.write(body)


def index(binary, src, work, tag):
    cache, rt = os.path.join(work, "cache"), os.path.join(work, "rt")
    os.makedirs(cache)
    os.makedirs(rt)
    env = dict(os.environ, CBM_CACHE_DIR=cache, CBM_RUNTIME_DIR=rt, TMPDIR=work)
    r = subprocess.run([INDEX_LOCAL, binary, src, PROJECT], env=env, capture_output=True, text=True, timeout=300)
    log = r.stdout + r.stderr
    with open(os.path.join(work, "index.log"), "w") as f:
        f.write(log)
    if r.returncode:
        die("indexing %s failed: %s" % (tag, log.strip()[-300:]))
    m = re.search(r"pipeline\.mode mode=(\w+)", log)
    return os.path.join(cache, PROJECT + ".db"), (m.group(1) if m else "?")


# ───────────────────────────── one fixture ─────────────────────────────

def check_fixture(lang, name, binary, workroot, n):
    t0 = time.time()
    fixture = os.path.join(FIXDIR, lang, name)
    # short paths: the index worker's socket lives under the runtime directory (104 bytes at most)
    scratch = os.path.join(workroot, str(n))
    os.makedirs(scratch)
    res = dict(lang=lang, name=name, modes={}, error=None, times={})
    try:
        truth_root = os.path.join(scratch, "truth")  # the truth runs on a copy: no build output in the repo
        stage(fixture, lang, truth_root, False)
        t = time.time()
        truth = rust_truth(truth_root, scratch) if lang == "rust" else python_truth(truth_root, scratch)
        res["times"]["truth"] = time.time() - t
        for mode in MODES:
            t = time.time()
            work = os.path.join(scratch, mode[0])
            src = os.path.join(work, "src")
            os.makedirs(work)
            stage(fixture, lang, src, mode == "parallel")
            if not git_free(src):
                die("%s is inside a git working tree: set FIXTURES_WORK to a directory outside any" % src)
            db, got = index(binary, src, work, "%s/%s %s" % (lang, name, mode))
            if got != mode:
                die("%s/%s: asked for %s mode, the index log says %s" % (lang, name, mode, got))
            g = Graph(db)
            if lang == "rust":
                facts, executed, unmapped = rust_facts(g, truth)
                traceable = rust_traceable(truth_root, g)
                outcome = compare(g, facts, None, lambda e: e["type"] == "CALLS" and e["dst"] in traceable)
                for f, ln in unmapped:
                    outcome["UNMAPPED %s:%d" % (f, ln)] = "UNMAPPED (no graph definition holds this line)"
            else:
                facts, executed = python_facts(truth)
                outcome = compare(g, facts, executed, python_judged)
            res["modes"][mode] = outcome
            res["times"][mode] = time.time() - t
    except Die as e:
        res["error"] = str(e)
    except subprocess.TimeoutExpired as e:
        res["error"] = "timeout: %s" % " ".join(map(str, e.cmd))[:120]
    res["times"]["total"] = time.time() - t0
    return res


# ───────────────────────────── known list and report ─────────────────────────────

def load_known(path):
    known = {}
    if not os.path.exists(path):
        return known
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            if not line.strip() or line.startswith("#"):
                continue
            cols = line.rstrip("\n").split("\t")
            if len(cols) < 3 or not cols[2].strip():
                die("%s:%d: need fixture, site and finding, tab separated" % (path, n))
            if (cols[0], cols[1]) in known:
                die("%s:%d: duplicate entry %s %s" % (path, n, cols[0], cols[1]))
            known[(cols[0], cols[1])] = cols[2]
    return known


def mismatches(r):
    """site -> text for every mismatch of one fixture; a site that differs between modes is one."""
    seq, par = r["modes"]["sequential"], r["modes"]["parallel"]
    mism = {}
    for s in sorted(set(seq) | set(par)):
        a, b = seq.get(s, "absent"), par.get(s, "absent")
        if a != b:
            mism[s] = "MODE-DIFF sequential: %s | parallel: %s" % (a, b)
        elif is_mismatch(a):
            mism[s] = a
    return mism


def report(results, known, a, t_start):
    fails, seen, lines, rows = 0, set(), [], []
    for r in results:
        fx = "%s/%s" % (r["lang"], r["name"])
        if r["error"]:
            lines.append("FAIL  %-30s ERROR %s" % (fx, r["error"]))
            fails += 1
            continue
        mism = mismatches(r)
        for mode in MODES:
            o = r["modes"][mode]
            bad = [s for s in mism if s in o or mism[s].startswith("MODE-DIFF")]
            status = "ok   " if not bad else "known" if all((fx, s) in known for s in bad) else "FAIL "
            lines.append("%s %-30s %-10s sites=%-4d right=%-4d withheld=%-4d mismatch=%-3d %5.2fs"
                         % (status, fx, mode, len(o), sum(v == "right" for v in o.values()),
                            sum(v.startswith("withheld ") for v in o.values()), len(bad), r["times"][mode]))
        for s, text in mism.items():
            tag = known.get((fx, s))
            if tag:
                seen.add((fx, s))
                lines.append("      known    [%s] %s: %s" % (tag, s, text))
            else:
                fails += 1
                lines.append("      MISMATCH %s: %s: %s" % (fx, s, text))
        rows.append((fx, r["times"]))
    ran = {"%s/%s" % (r["lang"], r["name"]) for r in results if not r["error"]}
    for (fx, s), tag in sorted(known.items()):
        if fx in ran and (fx, s) not in seen:
            fails += 1
            lines.append("      STALE    [%s] %s: %s no longer mismatches: remove it from tools/fixtures/known.tsv" % (tag, fx, s))
    wall = time.time() - t_start
    lines.append("fixtures: %d fixtures x 2 modes, %d known mismatches, %d failures, %.1f s"
                 % (len(results), len(seen), fails, wall))
    print("\n".join(lines))
    if a.log:
        os.makedirs(os.path.dirname(os.path.abspath(a.log)), exist_ok=True)
        with open(a.log, "w") as f:
            f.write("fixture\ttruth_s\tsequential_s\tparallel_s\ttotal_s\n")
            for fx, t in rows:
                f.write("%s\t%.2f\t%.2f\t%.2f\t%.2f\n" % (fx, t.get("truth", 0), t.get("sequential", 0),
                                                         t.get("parallel", 0), t.get("total", 0)))
            f.write("wall\t\t\t\t%.2f\n\n" % wall)
            f.write("\n".join(lines) + "\n")
    return 1 if fails else 0


def run_all(a):
    t_start = time.time()
    if sys.version_info < (3, 12):
        die("python3 >= 3.12 is required (this is %d.%d)" % sys.version_info[:2])
    rustc = os.environ.get("FIXTURES_RUSTC", "rustc")
    if not shutil.which(rustc):
        die("%s not found on PATH: the check runs the Rust fixtures under rustc" % rustc)
    binary = os.path.abspath(a.bin)
    if not os.access(binary, os.X_OK):
        die("binary not found or not executable: %s" % binary)
    known = load_known(a.known or KNOWN)
    todo = []
    for lang in ("rust", "python"):
        d = os.path.join(FIXDIR, lang)
        for name in sorted(os.listdir(d)) if os.path.isdir(d) else []:
            if os.path.isdir(os.path.join(d, name)) and (not a.only or name in a.only or lang + "/" + name in a.only):
                if not os.path.isfile(os.path.join(d, name, "PROVENANCE")):
                    die("%s/%s has no PROVENANCE file" % (lang, name))
                todo.append((lang, name))
    if not todo:
        die("no fixture selected")
    base = os.environ.get("FIXTURES_WORK") or ("/tmp" if os.path.isdir("/tmp") else tempfile.gettempdir())
    os.makedirs(base, exist_ok=True)
    workroot = tempfile.mkdtemp(prefix="fx-", dir=base)
    try:
        with cf.ThreadPoolExecutor(a.jobs) as ex:
            results = list(ex.map(lambda t: check_fixture(t[0], t[1], binary, workroot, t[2]), [(l, n, i) for i, (l, n) in enumerate(todo)]))
        return report(results, known, a, t_start)
    finally:
        if a.keep:
            print("fixtures: scratch kept in %s" % workroot)
        else:
            shutil.rmtree(workroot, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--only", nargs="*", default=[])
    ap.add_argument("--log", default=None)
    ap.add_argument("--jobs", type=int, default=int(os.environ.get("FIXTURES_JOBS", "6")))
    ap.add_argument("--known", default=None, help="known list to use instead of tools/fixtures/known.tsv")
    ap.add_argument("--keep", action="store_true", help="keep the scratch directory")
    a = ap.parse_args()
    try:
        return run_all(a)
    except Die as e:
        print("fixtures: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
