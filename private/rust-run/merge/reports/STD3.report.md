STATUS: DONE
COMMIT: f898f5b94b16a064555ed4b4c0ad490896a38ea7
CHANGED: 7 files, diff against start commit 6d74344f
OPEN: 6 unresolved items
BLOCKERS: none
GATE: not run
SCORE-codex: calls 222915/229961 flagged 1  (base 225798/229961 flagged 1)
SCORE-ruff: calls 103179/112300 flagged 1  (base 103252/112300 flagged 1)
SCORE-pm: calls 39897/47116 flagged 74  (base 39680/47116 flagged 74)
SCORE-bevy: calls 60622/84295 flagged 60  (base 60018/84295 flagged 76)
DISK: 6.0G /tmp/STD3
ROWS: 32123
STAMPS: 111 complete, 11 complete_except_deref, 4 incomplete
UNLISTED-CALLABLE: 0 on complete types (after)
RECOVERABLE: codex 32145/2301/2048/0; bevy 7249/316/249/5
---

Committed on `w2/std-inventory`; the working tree is clean. Nothing was pushed, and the coordinator gate was not run, per the stop rule.

What changed and why:

- [generate.py](/tmp/cbm-m/std/tools/std-inventory/generate.py:27) now adds source-verified, hashed cross-crate incoherent impls for slice, `CStr`, and `io::Error`; normalizes alias-headed impl receivers so `RawFd` methods attach to `i32`; and includes `Box<[T]>::into_vec`.
- The same generator removes the false generic-element classification for Cell, RefCell, UnsafeCell, OnceCell, OnceLock, Mutex, RwLock, both Weak identities, and NonNull. All ten now stamp `complete`.
- [rust_std_inventory.h](/tmp/cbm-m/std/internal/cbm/lsp/rust_std_inventory.h:1) is regenerated with 32,123 rows, source hashes, all required omitted methods, and the 111/11/4 stamp split.
- [probe.py](/tmp/cbm-m/std/tools/std-inventory/probe.py:261), [probe_specs.py](/tmp/cbm-m/std/tools/std-inventory/probe_specs.py), and [probe_traits.rs](/tmp/cbm-m/std/tools/std-inventory/probe_traits.rs) implement the independent stable-rustc oracle.
- [Makefile](/tmp/cbm-m/std/tools/std-inventory/Makefile:11) exposes `make probe`; [README.md](/tmp/cbm-m/std/tools/std-inventory/README.md:15) documents the compiler-probe contract. Compiler probing remains a developer test, not an index-time dependency.

Evidence per acceptance criterion:

| Criterion | Executed evidence |
|---|---|
| 1. Proof crates and required rows | The five copied proof crates were byte-identical to the named reviewer inputs outside `target` and all passed `rustup run stable cargo check`; see [proofs.log](/tmp/STD3/proofs.log) and the empty [copy diff](/tmp/STD3/proofs-copy.diff). Every required receiver/method query returns exactly one row, including `RUST_STD_BOX.into_vec`: [required-rows.log](/tmp/STD3/required-rows.log). |
| 2. Full compiler discovery | `python3 tools/std-inventory/probe.py --work-dir /tmp/STD3/probe-full` reports **PASS: 985348 compiler-probed calls; 0 callable-and-unlisted**, 122/126 identities, 2,707 names, 314 receiver specs, 50 pointee specs, stable rustc 1.99.0, **2085.8 seconds**: [verdict](/tmp/STD3/probe-full.log), [progress](/tmp/STD3/probe-full.progress). |
| 3. Stamps | The ten specified Deref-less identities each query as `complete`; totals are **111 complete / 11 complete_except_deref / 4 incomplete**: [required-rows.log](/tmp/STD3/required-rows.log). No compiler-probed complete type failed. |
| 4. Determinism, suites, lint, graph equality | Two regenerations and the committed header compare byte-equal; an invalid fixture is rejected with make exit 2: [determinism.log](/tmp/STD3/determinism.log). Removing only `RUST_STD_VEC.sort` makes the compiler oracle fail on exactly that pair, then the restored header passes: [red](/tmp/STD3/probe-discriminator-red.log), [green](/tmp/STD3/probe-discriminator-green.log). The five suites report **1648 passed**: [suites.log](/tmp/STD3/suites.log). `make -f Makefile.cbm lint-ci` reports **=== CI linters passed ===**: [lint log](/tmp/STD3/lint-first.log). |
| 5. Recoverability | `measure.py` reports codex **32145/2301/2048/0** and bevy **7249/316/249/5**, in listed/unlisted-complete/unlisted-except-deref/unlisted-incomplete order: [codex log](/tmp/STD3/recovery-codex.log), [codex SQL](/tmp/STD3/recovery-codex.sql), [bevy log](/tmp/STD3/recovery-bevy.log), [bevy SQL](/tmp/STD3/recovery-bevy.sql). |
| 6. Commit and scope | Commit `f898f5b9` names the incoherent/alias omission classes and **111/11/4** stamp counts. The start-to-final diff contains only the generated header and six `tools/std-inventory` files. |

Exact start-to-final edge-set differences:

| Corpus | Start/final edges | Removed/added by every edge type | Exact lists |
|---|---:|---:|---|
| codex | 953756 / 953756 | 0 / 0 | [summary](/tmp/STD3/edges-codex.log), [removed](/tmp/STD3/edges-codex/removed.tsv), [added](/tmp/STD3/edges-codex/added.tsv) |
| ruff | 415740 / 415740 | 0 / 0 | [summary](/tmp/STD3/edges-ruff.log), [removed](/tmp/STD3/edges-ruff/removed.tsv), [added](/tmp/STD3/edges-ruff/added.tsv) |
| pm | 169433 / 169433 | 0 / 0 | [summary](/tmp/STD3/edges-pm.log), [removed](/tmp/STD3/edges-pm/removed.tsv), [added](/tmp/STD3/edges-pm/added.tsv) |
| bevy | 294617 / 294617 | 0 / 0 | [summary](/tmp/STD3/edges-bevy.log), [removed](/tmp/STD3/edges-bevy/removed.tsv), [added](/tmp/STD3/edges-bevy/added.tsv) |

There are no changed edge classes to explain. All bench and corpus inputs were resolved with the required `os.path.realpath` command: [paths](/tmp/STD3/score-paths.log). The score differences from frozen main already existed at the starting commit.

Ruled out or assumed:

- The seven-file diff rules out resolver logic and `rust_std_methods.h` changes.
- Final graph coverage records no issue for the six tool files. The generated macro header is parse-partial; its affected rows were checked directly through regeneration equality and exact row queries: [coverage](/tmp/STD3/cbm-coverage-final.log).
- Assumption: the explicitly named `/tmp/R-STD` harness was treated as read-only task input; all copied inputs, compiler outputs, builds, scores, and indices were written only under `/tmp/STD3`.

OPEN items:

1. [generate.py:22](/tmp/cbm-m/std/tools/std-inventory/generate.py:22): `dyn Any` inherent methods remain absent from JSON; stamp is incomplete.
2. [generate.py:23](/tmp/cbm-m/std/tools/std-inventory/generate.py:23): `dyn Error` inherent methods remain absent from JSON; stamp is incomplete.
3. [receivers.json:48](/tmp/cbm-m/std/tools/std-inventory/receivers.json:48): `Option.Inner` has no concrete receiver.
4. [receivers.json:49](/tmp/cbm-m/std/tools/std-inventory/receivers.json:49): `Option.Item` has no concrete receiver.
5. [generate.py:152](/tmp/cbm-m/std/tools/std-inventory/generate.py:152): supertrait-derived methods still inherit the impl-trait provenance label. Fixing it requires carrying declaring-trait identity through recursive collection, so it was not a contained completeness repair.
6. [generate.py:190](/tmp/cbm-m/std/tools/std-inventory/generate.py:190): bound-ignored generic-self impls still create the reviewer’s 223-name universal over-list. Reduction was left out because finite probing cannot prove safety for every generic instantiation.

Not checked: the two Windows handle identities on a Darwin compiler, other targets/toolchains/features, macro-generated names absent from both rust-src and rustdoc, mutation tests, the forbidden coordinator gate, or an independent-agent review.