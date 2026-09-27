#!/usr/bin/env python3
"""Score Rust call sites against a rust-analyzer SCIP index, before and after the gate.

Inputs: the resolver dump written by indexing with CBM_RUST_RESOLVE_DUMP=<file>
(one `R` row per resolver record, one `C` row per call carrier with its joined
resolution, admission verdict and closure reason), the graph SQLite database of the same
index, the corpus root, and the SCIP index of that corpus.

For each `C` row the callee identifier is located inside the call span; the
sole SCIP occurrence starting at that identifier is the truth. Truths are classed
as internal (a workspace function), external, local (closure / fn value),
constructor, or unknown (no occurrence, e.g. cfg-inactive code). The resolver's
callee is mapped to the sole SCIP definition inside that graph node's source
span and compared as a complete symbol identity. The report prints, per strategy, how
often the resolver was correct or wrong on internal truths, why each resolved
site was closed by the gate, why an admitted internal target has no graph node,
the classes of internal truths the resolver produced nothing for, and
site-level recall.

    scripts/scip-site-join.py --corpus DIR --dump FILE --db FILE --scip FILE [--out DIR]
"""
import argparse, bisect, collections, importlib.util, json, os, re, sqlite3, subprocess, sys

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("--corpus", required=True)
parser.add_argument("--dump", required=True)
parser.add_argument("--db", required=True)
parser.add_argument("--scip", required=True)
parser.add_argument("--out", default=None, help="directory for samples.json / rows.json")
args = parser.parse_args()
CORPUS = os.path.abspath(args.corpus)
DUMP = args.dump
DB = args.db
OUT = args.out
if OUT:
    os.makedirs(OUT, exist_ok=True)

_spec = importlib.util.spec_from_file_location(
    "scip_call_edges", os.path.join(os.path.dirname(os.path.abspath(__file__)), "scip-call-edges.py"))
_decoder = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_decoder)
_, defs, refs, definition_sites = _decoder.parse_with_definition_sites(args.scip)
meta = json.loads(subprocess.run(["cargo", "metadata", "--no-deps", "--format-version", "1"],
                                 cwd=CORPUS, capture_output=True, text=True, check=True).stdout)
workspace = {p["name"] for p in meta["packages"]}

def crate_of(sym):
    m = re.match(r"rust-analyzer cargo ([^\s]+) ", sym)
    return m.group(1) if m else None

def descriptor_of(sym):
    parts = sym.split(" ", 4)
    return parts[4] if len(parts) > 4 else ""

IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")

def parse_descriptor(desc):
    """Return (leaf, owner, trait, kind) from a rust-analyzer descriptor."""
    if desc.endswith("()."):
        body = desc[:-3]
        kind = "fn"
    else:
        return (None, None, None, "other")
    last = body.rsplit("/", 1)[-1]
    owner = trait = None
    m = re.match(r"impl#\[(.*?)\](?:\[(.*?)\])?(.*)$", last)
    if m:
        owner, trait, leaf = m.group(1), m.group(2), m.group(3)
        owner = owner.strip("`")
        if trait: trait = trait.strip("`")
    elif "#" in last:
        owner, leaf = last.rsplit("#", 1)
        owner = owner.rsplit("/", 1)[-1]
    else:
        leaf = last
    leaf = leaf.strip("`")
    if leaf.endswith("!"): leaf = leaf[:-1]; kind = "macro"
    return (leaf, owner, trait, kind)

# symbol -> defining file (first definition occurrence)
def_file = {}
def_files = collections.defaultdict(set)
def_line = {}
for path, entries in defs.items():
    for rng, sym in entries:
        def_files[sym].add(path)
        if sym not in def_file:
            def_file[sym] = path
            def_line[sym] = rng[0]

# per-file occurrence index keyed by (line, col) start
occ_index = {}
for path, entries in refs.items():
    d = occ_index.setdefault(path, {})
    for rng, sym in entries:
        d.setdefault((rng[0], rng[1]), set()).add(sym)

# unresolved reasons keyed by site
site_reason = {}
for line in open(DUMP, encoding="utf8", errors="replace"):
    if not line.startswith("R\t"): continue
    f = line.rstrip("\n").split("\t")
    if f[6] == "lsp_unresolved":
        site_reason[(f[1], f[2], f[3])] = (f[7], f[5])

# graph nodes
con = sqlite3.connect(DB)
nodes = {}
node_labels = {}
for qn, fp, name, label, sl, el in con.execute(
        "select qualified_name, file_path, name, label, start_line, end_line from nodes"):
    node_labels.setdefault(qn, set()).add(label)
    if label in ("Function", "Method"):
        if os.path.isabs(fp):
            try:
                fp = os.path.relpath(fp, CORPUS)
            except ValueError:
                pass
        fp = fp.replace(os.sep, "/")
        identity = _decoder.definition_symbol_for_node(definition_sites, fp, sl, el)
        nodes[qn] = (fp, name, label, sl, el, identity)

DERIVED_DEF = re.compile(rb"^\s*(#\[|pub(\([a-z]+\))?\s+(struct|enum|union)\b|(struct|enum|union)\b)")

def no_node_class(truth, rqn):
    """Why an admitted internal target has no callable graph node."""
    labels = node_labels.get(rqn)
    if labels:
        return "node_not_callable:" + "+".join(sorted(labels))
    tfile = def_file.get(truth)
    if tfile is None:
        # rust-analyzer emits no definition occurrence for a method generated by
        # a derive or attribute macro; the graph has no source item to publish.
        _, _, trait, _ = parse_descriptor(descriptor_of(truth))
        return "no_source_definition:" + (trait.split("<")[0] if trait else "inherent")
    data, starts = source(tfile)
    line = def_line.get(truth, 0)
    if line < len(starts):
        end = starts[line + 1] if line + 1 < len(starts) else len(data)
        if DERIVED_DEF.match(data[starts[line]:end]):
            return "derive_generated"
    return "qn_not_in_graph"

sources = {}
def source(rel):
    if rel not in sources:
        try:
            data = open(os.path.join(CORPUS, rel), "rb").read()
        except OSError:
            data = b""
        starts = [0]
        for i, b in enumerate(data):
            if b == 10: starts.append(i + 1)
        sources[rel] = (data, starts)
    return sources[rel]

def to_linecol(starts, off):
    line = bisect.bisect_right(starts, off) - 1
    return line, off - starts[line]

def callee_ident(text):
    """Offset of the callee identifier inside call text, or None."""
    n = len(text)
    if n == 0: return None
    end = n
    while end > 0 and text[end-1] in b" \t\n": end -= 1
    if end == 0: return None
    if text[end-1:end] == b")":
        depth = 0; i = end - 1
        while i >= 0:
            c = text[i:i+1]
            if c == b")": depth += 1
            elif c == b"(":
                depth -= 1
                if depth == 0: break
            i -= 1
        if i < 0: return None
        p = i
    else:
        p = end
    while p > 0 and text[p-1:p] in (b" ", b"\t", b"\n"): p -= 1
    if p > 0 and text[p-1:p] == b"!": p -= 1
    if p > 0 and text[p-1:p] == b">":
        depth = 0; i = p - 1
        while i >= 0:
            c = text[i:i+1]
            if c == b">": depth += 1
            elif c == b"<":
                depth -= 1
                if depth == 0: break
            i -= 1
        if i < 0: return None
        p = i
        if text[p-2:p] == b"::": p -= 2
    q = p
    while q > 0 and (chr(text[q-1]).isalnum() or text[q-1:q] == b"_"): q -= 1
    if q == p: return None
    return q, text[q:p].decode("utf8", "replace")

def leaf_of_callee(callee):
    callee = callee.split("[", 1)[0]
    m = list(IDENT.finditer(callee))
    return m[-1].group(0) if m else None

rows = collections.Counter()
closures = collections.Counter()
no_node_detail = collections.Counter()
samples = collections.defaultdict(list)
wrong_detail = collections.Counter()
missed_truth = collections.Counter()
missed_internal_files = collections.Counter()
edge_truth = set()   # (file, byte start/end, caller, truth symbol)
edge_found = set()
n = 0
for line in open(DUMP, encoding="utf8", errors="replace"):
    if not line.startswith("C\t"): continue
    f = line.rstrip("\n").split("\t")
    _, rel, start, end, caller, callee, rqn, strategy, conf, verdict, impl_key = f[:11]
    closure = f[11] if len(f) > 11 else "-"
    start, end = int(start), int(end)
    data, starts = source(rel)
    text = data[start:end]
    ci = callee_ident(text)
    if ci is None:
        rows[(strategy, verdict, "noident", "-")] += 1
        if len(samples["noident"]) < 20: samples["noident"].append((rel, text[:80]))
        continue
    off, ident = ci
    lc = to_linecol(starts, start + off)
    if rel not in occ_index:
        rows[(strategy, verdict, "no_scip_file", "-")] += 1
        continue
    syms = sorted(occ_index.get(rel, {}).get(lc, ()))
    if len(syms) > 1:
        rows[(strategy, verdict, "ambiguous", "ambiguous_oracle")] += 1
        if len(samples[(strategy, "ambiguous_oracle")]) < 25:
            samples[(strategy, "ambiguous_oracle")].append(
                (rel, start, end, caller, callee, rqn, syms)
            )
        continue
    fn_syms = [s for s in syms if descriptor_of(s).endswith("().")]
    if not syms:
        truth_class = "no_occurrence"
    elif any(s.startswith("local ") for s in syms):
        truth_class = "local"
    elif fn_syms:
        crate = crate_of(fn_syms[0])
        truth_class = "internal" if crate in workspace else "external"
    else:
        # type/variant constructor or other
        d = descriptor_of(syms[0])
        truth_class = "ctor" if (d.endswith("#") or d.endswith(".")) else "other"
    truth = fn_syms[0] if fn_syms else (syms[0] if syms else None)
    site = (rel, start, end, caller, truth)
    if truth_class == "internal":
        edge_truth.add(site)
    if rqn == "-":
        rows[(strategy, verdict, truth_class, "none")] += 1
        if truth_class == "internal":
            leaf, owner, trait, kind = parse_descriptor(descriptor_of(truth))
            shape = "dotted" if ("." in callee and "::" not in callee) else ("scoped" if "::" in callee else "bare")
            reason = site_reason.get((rel, str(start), str(end)), ("no_record", ""))[0]
            key = ("method" if owner else "free", "same_file" if def_file.get(truth) == rel else "cross_file", shape, reason)
            missed_truth[key] += 1
            if len(samples[("missed",) + key]) < 12:
                samples[("missed",) + key].append((rel, caller, callee, truth, site_reason.get((rel, str(start), str(end)))))
        continue
    node = nodes.get(rqn)
    if node is None:
        outcome = "no_node"
        if truth_class == "external": outcome = "no_node_external"
        rows[(strategy, verdict, truth_class, outcome)] += 1
        if verdict == "closed": closures[(strategy, closure, truth_class, outcome)] += 1
        if len(samples[(strategy, outcome)]) < 15: samples[(strategy, outcome)].append((rel, caller, callee, rqn, truth))
        if verdict == "admitted" and truth_class == "internal":
            why = no_node_class(truth, rqn)
            no_node_detail[(strategy, why)] += 1
            if len(samples[("no_node", strategy, why)]) < 12:
                samples[("no_node", strategy, why)].append((rel, caller, callee, rqn, truth))
        continue
    if truth_class == "no_occurrence":
        rows[(strategy, verdict, truth_class, "unknown")] += 1
        if verdict == "closed": closures[(strategy, closure, truth_class, "unknown")] += 1
        continue
    if truth_class != "internal":
        outcome = "wrong_" + truth_class
        rows[(strategy, verdict, truth_class, outcome)] += 1
        if verdict == "closed": closures[(strategy, closure, truth_class, outcome)] += 1
        wrong_detail[(strategy, outcome)] += 1
        graph_identity = node[5]
        if len(samples[("wrong", strategy, outcome)]) < 25:
            samples[("wrong", strategy, outcome)].append(
                (rel, start, end, caller, callee, rqn, truth, graph_identity)
            )
        if len(samples[(strategy, outcome)]) < 15: samples[(strategy, outcome)].append((rel, caller, callee, rqn, truth, syms[:2]))
        continue
    gfile, gname, glabel, gline, gend, graph_identity = node
    if graph_identity == truth:
        outcome = "correct"
        if verdict == "admitted":
            edge_found.add(site)
    elif graph_identity is None:
        outcome = "wrong_unmapped_definition"
    else:
        outcome = "wrong_definition"
    rows[(strategy, verdict, truth_class, outcome)] += 1
    if verdict == "closed":
        closures[(strategy, closure, truth_class, outcome)] += 1
        if len(samples[("closed", strategy, closure)]) < 12:
            samples[("closed", strategy, closure)].append((rel, caller, callee, rqn, truth))
    if outcome != "correct":
        cause = outcome
        if graph_identity is not None:
            _, truth_owner, truth_trait, _ = parse_descriptor(descriptor_of(truth))
            _, graph_owner, graph_trait, _ = parse_descriptor(descriptor_of(graph_identity))
            if bool(truth_trait) != bool(graph_trait):
                cause = "trait_inherent_definition"
            elif truth_owner != graph_owner:
                cause = "different_owner_definition"
            else:
                cause = "different_definition"
        wrong_detail[(strategy, cause)] += 1
        if len(samples[("wrong", strategy, cause)]) < 25:
            samples[("wrong", strategy, cause)].append(
                (rel, start, end, caller, callee, rqn, truth, graph_identity)
            )

# ---- report
print("=== per strategy / verdict / truth class / outcome ===")
for k, v in sorted(rows.items(), key=lambda kv: -kv[1]):
    print(f"{v:8d}  {k}")
print()
print("=== precision per strategy (internal truth only) ===")
per = collections.defaultdict(collections.Counter)
for (strategy, verdict, tc, outcome), v in rows.items():
    per[strategy][outcome] += v
for s, c in sorted(per.items(), key=lambda kv: -sum(kv[1].values())):
    correct = c["correct"]; wrong = sum(v for k, v in c.items() if k.startswith("wrong"))
    tot = correct + wrong
    print(f"{s:28s} correct={correct:7d} wrong={wrong:6d} prec={(correct/tot*100 if tot else 0):6.2f}%  no_node={c['no_node']} ext_no_node={c['no_node_external']} none={c['none']}")
print()
print("=== closed sites with a resolution (strategy, closure reason, truth class, outcome) ===")
for k, v in sorted(closures.items(), key=lambda kv: -kv[1]):
    print(f"{v:8d}  {k}")
print()
print("=== admitted internal targets without a callable graph node (strategy, class) ===")
for k, v in no_node_detail.most_common(): print(v, k)
print()
print("=== wrong-edge causes (strategy, cause) ===")
for k, v in wrong_detail.most_common(): print(v, k)
print()
print("=== resolver-missed internal truths (no resolution) ===")
for k, v in missed_truth.most_common(): print(v, k)
print()
print("=== site-level recall on internal fn truths (admitted sites only) ===")
print("truth sites", len(edge_truth), "found correct", len(edge_found), f"recall={len(edge_found)/max(1,len(edge_truth))*100:.2f}%")
if OUT:
    json.dump({str(k): v for k, v in samples.items()}, open(os.path.join(OUT, "samples.json"), "w"), indent=1, default=str)
    json.dump({str(k): v for k, v in rows.items()}, open(os.path.join(OUT, "rows.json"), "w"), indent=1)
