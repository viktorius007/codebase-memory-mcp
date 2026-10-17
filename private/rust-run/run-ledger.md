## Wave 1 (2026-10-07 13:29 →) — see `wave1/run-ledger.md`, `wave1/EXPERIMENT.md`, `wave2/PLAN.md`, HANDOFF.md

Ten Sol 6.1 arms on the work order items 1–3 and the located item-5 defects; all reported; six resumed for recall narrowing. Owner decisions this evening: std inventory (generator from rustdoc JSON, per-type completeness stamp) is wave 2 item 1, deferral lifted on the measurement pm 4 / jmap 0 / jmapc 0 std-reason stops; this run is an experiment feeding the Sol profile.

# Rust graph fleet — coordinator state

Goal: every Rust call, definition and relationship the corpus source can prove, on the graph,
with no wrong link and no rust-analyzer at index time (owner rules). Never push.

## State 2026-10-07 12:30 — after the adversarial review (see review/REVIEW.md, review/run-ledger.md)

Status doc rewritten and amended (uncommitted at the time of writing; commit first). Review: 8 arms
(rescore, corpora, resolver × Sol 6.1 high / Opus 5.5 high; Sol corpora+resolver rerun with full
access), 14 cross-provider BLOCKING/MAJOR findings, 4 new held-out corpora with truth. Owner decisions
and the work order are in docs/RUST_GRAPH_STATUS.md "Work, in order". /tmp cleaned (2,842 test dirs,
9 review clones trashed). Definition of done from 6 Oct: (a) 0 wrong pairs — WITHDRAWN by the review;
(b) marker — landed but accounting not exact; (c) causes — superseded by the review's list; (d) status
doc — DONE. Queue = status doc items 1–5.

## (earlier) State 2026-10-07 03:10 — CHECKPOINT: no arm live, main aa48ba96 clean, every wave-6 report landed

### 07 Oct 03:10 — checkpoint

main = aa48ba96 (UM round 3: unresolved-site records; `unresolved_occurrences` + `unresolved_sites` view, 32
stop reasons; `trace_path` carries an `unresolved_sites` block, `check_index_coverage`/`index_status` totals;
`tools/rust-audit/unresolved.py` accounting PASS on both corpora — every in-corpus truth site published or
recorded; links identical (exact diff 0 on codex; ruff 1 unconfirmed IMPORTS edge, the known run-to-run flake
`generate_cli_help.rs -> ruff::lib`); CPU +3.5%, score.db +9.8% codex / +4% ruff; 1.54M codex / 368k ruff
records, the bulk `keyword_syntax_filtered`, `macro_invocation_not_proven`, `macro_token_not_resolved`).
Scores: codex 225,790 / 229,952 (98.2%), pairs 1 (NOT IN TRUTH, cfg(windows)); ruff 103,252 / 112,301
(91.9%), pairs 1 (conditional, `Recording::new -> Buffer::elements` declaration vs `&mut W` forwarder).
Frozen: /tmp/cbm-score-aa48ba96, /tmp/cbm-score2-aa48ba96 — the only frozen pair. /tmp: those two +
/tmp/cbm-trial (build dir) + the test-suite temp dirs the owner must trash (classifier-denied for me).
Repository: branch `main` only; working tree clean but untracked handoff.md. No Codex process. 95 GB free.
Wave 6 outcome: CD 8512665d, XL 1633e0f8 (3 rounds), RR 903dcdc3 (2 rounds), UM aa48ba96 (3 rounds) — all
landed; durations 200-330 min each under load 50-220; fleet cost first rounds $230.19, resumes ~$25 more
(not in usage.py; read the jsonl2/3 usage events to total them).
Definition of done, status: (a) 0 wrong pairs on both corpora — codex 0 wrong (1 not in truth), ruff 0 wrong
(1 conditional, labelled by the scorer not the graph) — MET for calls; (b) unresolved-site marker — LANDED;
(c) every WORKLIST cause landed or recorded unprovable — ranks 1-12 landed, 13 (std iterator head) folded
into IT, 14-23 open (callable result type 133+131, block macro visibility 116, block item identity 83, trait
method return 79+15, proc-macro method nodes 72, callback generic 69+3, the rest small); (d) status doc
rewrite — NOT DONE. Queue, in order: RR's 62+1 recorded losses (generic Self/bound proof rust_lsp.c:12877/
12887, 13 self-receiver stops, blanket-owner `type_parameter` shape :14327); WORKLIST 14, 17, 19, 15, 16, 18;
6,729 unassigned ruff misses (now enumerable from the UM records by stop reason — start there, no audit arm
needed); EnumVariant USAGE 0 / 37,429; impl links 71.8%; const/static defs 59.3%; From-argument proof; 2
self-recursion sites; the Python same-language suffix guess (registry.c:1101, xl OPEN); fqn.c:65 extension
stripping coalesces same-stem files (xl/rr OPEN); IMPORTS run-to-run nondeterminism; test suites leave temp
dirs in /tmp; stash0's 15 tests (retired/ patch); docs/RUST_GRAPH_STATUS.md rewrite from this ledger.

## (earlier) State 2026-10-07 00:25 — one arm live (um round 2); main 903dcdc3 clean

### 07 Oct 00:25 — XL and RR landed

main = 903dcdc3. Landed: 1633e0f8 XL round 3 (no cross-language link on a name match; one language-family
guard in the candidate registry; ruff removed 843 IMPORTS / 372 CALLS / 318 USAGE / 317 DECORATES / 80
INHERITS from .py/.tsx/.ts/.sh into Rust, codex 120; CONFIGURES and .toml/.proto/.json/.md untouched; Rust
links identical to the unit; suites 1,998 incl. registry/edge_imports/language), 903dcdc3 RR round 2
(reference/primitive/nested-borrow receivers; ruff 103,022 -> 103,252 (91.9%), pairs 12 -> 1 — the 11
formatter-buffer declaration links withheld, the 1 left is BS's `Recording::new -> Buffer::elements`
declaration; 62 confirmed ruff calls lost each with its stop reason; codex -1 proved link vetoed by a generic
`RolloutConfigView for &T` candidate at rust_lsp.c:12887). codex 225,790 / 229,952 (98.2%), pairs 1 (NOT IN
TRUTH cfg(windows)); ruff 103,252 / 112,301 (91.9%), pairs 1 (conditional). Frozen /tmp/cbm-score{,2}-903dcdc3
only; every other frozen score, clone (cd, xl, rr), evidence dir and branch is trashed/deleted. Queue additions
from RR: generic Self/bound proof for 49 ruff sites + the codex cwd veto (rust_lsp.c:12877/12887), 13
self-receiver stops, blanket-owner `type_parameter` shape (rust_lsp.c:14327). Rule added: an arm's frozen
baseline stays until that arm is decided (rr and xl lost theirs mid-round when main moved).
um round 2 RUNNING (386 commands at 00:21).

## (earlier) State 2026-10-06 22:55 — three resumes live (xl round 3, rr round 2, um round 2); main 8512665d clean

### 06 Oct 22:55 — wave 6 graded: CD landed, XL/RR/UM rejected with reasons and resumed

main = 8512665d (CD: channel-handle and Deref/dyn calls withheld unless proved; codex pairs 5 -> 1, the 1 left
is cfg(windows) NOT IN TRUTH; ruff 103,022 / 112,301 (91.7%), pairs 12 conditional; codex 225,791 (98.2%)).
Frozen /tmp/cbm-score{,2}-8512665d. All wave-6 arms ran 200+ min under load average 150-220 (owner's other
session's cargo/clippy jobs + a 2-day WebKit process); costs cd $8.39, rr $8.49, um $7.22, xl $3.49+.
XL round 2 (report2 DONE 5/5): exact edge-set diff on 8512665d — Rust untouched, 0 cross-language code links,
BUT removed .rs/.py -> .toml/.json CONFIGURES (codex 5,503, ruff 1,297; brief put .toml/.proto out of scope)
and ~5,000 .py -> .py links unread (USAGE 3,678, WRITES 970, CALLS 311) -> NOT LANDED; RESUMED 22:35 (round 3,
followup2 -> report3): restore config/proto/doc targets, read 30 per corpus or narrow the closure.
RR round 1 (PARTIAL 3/5): ruff +230 and pairs 12 -> 1, codex -20; 80 confirmed lost pairs unattributed, one
codex regression unexplained (turn_context.rs:1208 -> TurnEnvironment.cwd) -> RESUMED 20:45 (followup ->
report2): account or restore every one.
UM round 1 (PARTIAL 4/5): graph identical on both corpora (exact diff empty); cost measured by me back to back
on 8512665d: CPU 393.40 -> 393.78 s (+0.1%), RSS +7.5%, score.db +9.2% codex / +4.1% ruff — PASSES the cap
(the arm's +360% wall time was load). Records: table unresolved_occurrences + view unresolved_sites
(file, line, caller qn, written name, reason; 22 reasons, top: type_path_not_proven 236k,
target_not_materialized 222k, unknown_receiver_type 86k). Its own unresolved.py lists 1,546 call sites and
104,144 type/field refs neither published nor recorded, 5,533 caller_not_mapped -> NOT LANDED; RESUMED 22:52
(followup -> report2): every in-corpus truth site published or recorded with the resolver's reason; show the
tool output. Lesson recorded: `git diff` patches miss an arm's untracked files (um's unresolved.py) — save
`git status --short` and add untracked files to the patch set.
Checkpoint definition (owner, 20:10): after the four wave-6 reports are dealt with completely. State now: one
landed, three rejected with reasons and resumed; main clean; resumes run in their clones.

## (earlier) State 2026-10-06 18:00 — four Codex processes live (xl on 1ec0ecaa; cd, rr, um on b5725e30 — all 6.1 medium)

### 06 Oct 18:00 — wave 5 all landed; wave 6 launched

main = b5725e30. Landed since 16:10 (SCORE.md): dc26ddc7 UA (ruff +39), 1ec0ecaa IT round 2 (ruff +659,
USAGE +131, 0 semantic losses; round 1's two wrong pairs were the RP defect), b5725e30 BS round 2 (ruff +129,
codex +35; pairs 11 -> 12, the new one `buffer.rs:661 -> Buffer::elements` declaration vs the `&mut W`
forwarder — read in source by me, same conditional class as the 11). ruff 103,035 / 112,301 (91.7%), pairs
12 all conditional; codex 225,791 (98.2%), pairs 5 (4 wrong targets + 1 not in truth). Frozen
/tmp/cbm-score{,2}-b5725e30; /tmp/cbm-score{,2}-1ec0ecaa restored from Trash because arm xl was briefed on
them (trash when xl is decided). All wave-5 clones, evidence and branches gone. Fleet total: 48 arms, $202.60
at 16:30.
New wrong-link class found while reading bs's OPEN 3: non-Rust files linked to same-named Rust items by a
name match (ruff: IMPORTS 843, CALLS 372, USAGE 318 from .py/.tsx/.ts/.sh; codex: 60/65/3 from
.py/.mjs/.js/.bazel/.ps1/.sh; e.g. `scripts/publish-crates.py -> Function request api.rs`). Pre-existing,
all false. Task XL, arm xl-61-med (branch fleet/xl-61-med, base 1ec0ecaa; report 18:48, PARTIAL 4/5, $2.07+):
trial on b5725e30 — Rust scores identical to the unit, codex diff = exactly the 120 cross-language edges
removed; ruff removes ~1,600 but ADDS 18 .py -> .rs DECORATES (pass_semantic.c:439, pass_parallel.c:3171)
and 20 .py -> .py CALLS from the registry.c:1101 suffix guess (1 read false) -> NOT LANDED; RESUMED 19:05
(thread 01a10ff9-4628-75a3-9a54-322126267b61, followup -> report2): no cross-language edge of any type,
every added same-language edge read true or its producer closed. Trial outputs trashed; main clean.
Method that caught it: exact edge-set diff of the two score.db files on (source qn, target qn, type)
grouped by source/target extension — compare_graphs.py truncates its listing and would have hidden it.
Wave 6 launched 17:55 on b5725e30: cd-61-med (CD: the 4 codex wrong targets — channel `recv` via
lsp_method_dispatch, Deref/dyn via lsp_deref_dispatch), rr-61-med (RR: WORKLIST ranks 7 + 11 + BI remainder:
reference/primitive impl receivers, deep borrows, blanket-body forwarding), um-61-med (UM: unresolved-site
records queryable through the MCP tools and coverage totals; graph unchanged, +10% cost cap). Queue after:
WORKLIST ranks 14 (callable result type 133+131), 17, 19; 6,729 unassigned ruff misses (audit arm first);
EnumVariant USAGE 0 / 37,429; impl links 71.8%; const/static defs 59.3%; From-argument proof; 2
self-recursion sites; stash0's 15 tests (retired/ patch); docs/RUST_GRAPH_STATUS.md rewrite at the end.
Definition of done for this run (stated to owner 16:40): 0 wrong pairs on both corpora (conditional pairs
labelled), unresolved-site marker landed, every WORKLIST cause landed or recorded unprovable, status doc
rewritten.

## (earlier) State 2026-10-06 16:10 — two Codex processes live (bs-61-med and it-61-med resumed on main 97320c5f); ua trial scoring

Owner direction (5 Oct): the coordinator is tech lead and the only consumer; work autonomously; use
Codex Sol 6.1 / 5.6 at low/medium/high as workhorses, A/B them, record cost; every deployment must
return evidence. The coordinator takes the FINDINGS §6 decisions on the principle "the graph states
what the source proves and labels what is conditional": name-guess fallbacks removed for Rust,
self-recursion published, truth generator corrected, both cfg alternatives linked and labelled (not
started).

### main = 414fa176 (not pushed) — current figures in the 14:10 section; summaries `summary-414fa176.txt`, `summary-ruff-414fa176.txt`. Below: the 8b5c22ae baseline this run started from

calls ALL 225,420 / 229,946 98.0% (path 99.4, method 96.9, macro 97.2); wrong-or-unconfirmed pairs 133;
unverifiable pairs 5,303; IMPORTS 48,814 / 49,307, 949 not confirmed; USAGE as at 8f8e6299 (Struct
56,954 / 7,854 recall 66.4%, Enum 53.8%, Field 67.7%, Variable 90.7%; EnumVariant 0 / 66,424).
Frozen scores: /tmp/cbm-score-8b5c22ae (main), -b31b4569, -8f8e6299, /tmp/cbm-score-b45d8d10-t2 (graph
of b45d8d10 on the corrected truth: 225,328, pairs 136 — like-for-like baseline of every wave-2 arm).

### Held-out corpus (6 Oct 01:35) — the fixes do not generalise

Second bench: `~/.cache/cbm-rust-bench2` (ruff at b8c5e73, 1,953 files; `ruff.scip`, `truth.tsv`,
`refs.tsv`; generated with `~/.rustup/toolchains/stable-aarch64-apple-darwin/bin/rust-analyzer scip .`
in 3 min — the rustup proxy binary loops — and tsdiff built to /tmp/cbm-truth2/tsdiff-target).
Score: `CBM_RUST_BENCH=~/.cache/cbm-rust-bench2 CBM_RUST_CORPUS=~/.cache/cbm-rust-bench2/ruff
scripts/rust-score.sh <bin> <out>` (commit a839a2f1). main on ruff (`summary-ruff-8b5c22ae.txt`,
frozen /tmp/cbm-score2-a839a2f1): calls 77,202 / 112,301 68.7% (path 80.9, method 63.8, macro 65.9);
wrong-or-unconfirmed pairs 296; const/static defs 59.3%; types 91.1%; IMPLEMENTS 71.6%; IMPORTS
92.6%, 4,366 not confirmed; USAGE recall Struct 57.7%, Enum 43.0%, Interface 29.5%. Truth on ruff is
unvalidated. Audit arm ra-61-high (task RA: every cause counted, truth tested) RUNNING since 01:36.
Every later merge is scored on BOTH corpora.
Audit ra-61-high (02:15, PARTIAL 2/6; files in audit/ra/): all 296 ruff pairs judged — 46 wrong, 237
conditionally correct (forwarder/default-method targets), 13 correct (12 = scorer picks the outer of
nested callers, precision.py:180); 100 sampled call misses all real; unconfirmed USAGE ~12,900 wrong
by sample (name binding: 23-29 of 30 wrong per kind) and IMPORTS guesses ~2,900 wrong (20 of 30) —
the WM/WU/WI work is the top soundness item on ruff too; missing const/static, types, imports without
node = macro-generated items (proc-macro mostly). Only 377 of 35,099 missing calls have a cause.
RESUMED 02:20 (thread 01a10cb6-9554-79a3-9074-7948e942cedc, out/ra-61-high.followup.txt ->
report2.md): a cause for every missing call, measured from the resolver, with codex counts.
Arm rx-61-med (task RX) RUNNING since 02:22: `vendored` dir skipped (65 sites), fn named `function`
rejected (28), fn inside trait default body (3), scorer nested-caller attribution (12 false wrongs).
rx-61-med scorer fix LANDED as 9b04330a at 03:40 (codex unchanged, ruff pairs 296 -> 284); a-c still held. Earlier: rx-61-med report (02:57, PARTIAL 4/5; patch fleet/patches/rx-61-med.patch): ruff calls +73, pairs 283;
a-c expose 32 new wrong IMPORTS/USAGE via name-guess fallbacks and lose one Module to a Folder QN
collision (pipeline.c:653, fqn.c:346) -> HELD until WM/WI land, then re-measure. Its precision.py fix
REGRESSES codex (coordinator re-score: 'caller has no oracle data' 270 -> 5,826; unverifiable pairs
5,303 -> 10,489): not landed, main unchanged. RESUMED 03:00 (out/rx-61-med.followup.txt -> report2.md).

Audit round 2 (ra-61-high report2, 05:38; audit/ra/measured/REPORT.md + WORKLIST.md; full ledgers in
/tmp/cbm-fleet/ra-61-high/private/ra-audit/measured, 37 MB): 28,370 of 35,099 ruff misses attributed
(6,729 unassigned). Rank 1: `edition = { workspace = true }` ignored (rust_cargo.c:242), tree falls
back to 2015: 18,315 sites, pairs unchanged in the counterfactual. Then declaration scope lost
(rust_modtree.c:1489) 2,303 / codex 220; qualified trait identity rejected (rust_lsp.c:759) 2,076 —
its probe raised wrong pairs 296 -> 526, needs soundness work; macro argument grammar 1,775;
associated-const type unread 973; 18 smaller causes in WORKLIST.md. Arm ed-61-med (task ED, the
edition reader) RUNNING since 05:45. Next after ED lands: one arm per WORKLIST rank 2-5.

06 Oct ~07:30: WM LANDED as 75e91b22 (see SCORE.md): codex 225,710 (98.2%), pairs 133; ruff 79,054
(70.4%), pairs 262. Disk filled to 0 at ~06:10 (ra-61-high wrote 73 GB of traces to /tmp/ra-61-measured;
owner deleted evidence dirs; COMMON.md now caps arms at 8 GB). ed-61-med died from it and was RESUMED
(thread 01a10da5-cf1f-7e01-bded-5083088f3428; its tree touches the vendored TOML grammar — asked to
justify or remove). WI trial on 75e91b22: codex good (IMPORTS not confirmed 906 -> 370, confirmed -25,
the five wrong USAGE sites publish nothing) but on ruff confirmed IMPORTS fall 18,567 -> 14,387 for
-1,413 unconfirmed: the guess is what finds ruff's imports while the edition is misread. WI HELD until
ED lands, then re-measure on both. RX a-c (fleet/patches/rx-61-med.patch) also still held: re-measure
on main after ED. WM residue to queue: ~1,900 WRITES withheld (5 of 30 sampled true), 203 confirmed
loss candidates, StartupProbe cfg export, fn-local struct identity.

ED (ed-61-med report 08:20, PARTIAL 5/6; fleet/patches/ed-61-med.patch, on 9b04330a): manifest reader
rewritten to read every edition form, unknown edition declines path resolution; no grammar change. Its
own score on 9b04330a: ruff 77,202 -> 95,489 (+18,287, 85.0%), codex identical; 11 new disputed ruff
pairs (5 wrong trait dispatch), 21 confirmed calls + 328 confirmed USAGE lost, cause open. Conflicts
with 75e91b22 in rust_cargo.c (main added [dependencies.<name>] tables). Coordinator committed its tree
on branch ed-61-med in the clone, started `git merge main-now` there, and RESUMED the arm (followup2 ->
out/ed-61-med.report3.md) to resolve, re-score vs /tmp/cbm-score{,2}-75e91b22 and account for the pairs.
A failed 3-way apply on main is kept as a git stash (stash@{0}); main is clean at 75e91b22.

### 06 Oct 09:40 — four merges this hour, wave 3 launched

main = b8722c26: 75e91b22 WM, b296a6b2 ED (ruff 68.7 -> 87.2%), 71723861 WI, b8722c26 RX a-c (SCORE.md).
codex 225,710 / 229,946 (98.2%), pairs 133 (list unchanged since 8b5c22ae); ruff 98,018 / 112,301 (87.3%),
pairs 278 (7 read wrong — all lsp_trait_dispatch — task TD; 12 consistent with source, truth absent).
Frozen: /tmp/cbm-score-b8722c26, /tmp/cbm-score2-b8722c26. Wave 3 arms (6.1 medium, tasks/<T>.md): ds-61-med
(declaration scope, 2,303 ruff / 220 codex), qt-61-med (qualified trait identity, 2,076; dispatch must
withhold), ma-61-med (standard macro argument grammar, 1,775), ac-61-med (associated-const type, 973),
td-61-med (trait dispatch soundness: the 7 wrong pairs + 237 conditional). Queue after wave 3: WM residue
(~1,900 WRITES withheld, 203 loss candidates, StartupProbe cfg export, fn-local struct identity), WORKLIST
ranks 6-19, 6,729 unassigned ruff misses, From-argument proof (jwf), unresolved-site marker, 2 self-recursion
sites. Stash stash@{0} on main is a failed apply of ed-61-med.patch (landed since): owner may drop it.

### 06 Oct 12:30 — wave 3 complete

main = 0e10fa93 (TD merged: ruff pairs 66 -> 42, 22 wrong withheld, 0 new; codex 133 unchanged — the arm
judged all 133 correct-but-not-in-truth or conditional, not re-read by the coordinator). codex 225,645
(98.1%); ruff 99,847 / 112,301 (88.9%). Frozen: /tmp/cbm-score-0e10fa93, /tmp/cbm-score2-0e10fa93; all
older frozen scores, trial dirs and finished clones are in the Trash (owner rule: trash as you go).
No Codex process live. Queue, in order: (1) blanket-impl / borrowed-receiver proof to restore the 506
ruff + 70 codex calls QT withheld and the 67 + 14 TD withheld; (2) WM residue (WRITES, 203 losses,
StartupProbe, fn-local structs); (3) WORKLIST ranks 6-19; (4) 6,729 unassigned ruff misses; (5) the
133 codex flagged pairs: are they truth defects? (6) From-argument proof; (7) unresolved-site marker.

### 06 Oct 16:10 — wave 5 results; RP landed

main = 97320c5f (RP: reference receiver keeps trait-method precedence, generic receivers link to the
bound trait declaration; type_rep.h carries reference shape). ruff 102,208 / 112,301 (91.0%), pairs
33 -> 11 (the 11 = conditional formatter-buffer pairs buffer.rs:39/63/69 — graph picks the declaration,
rust-analyzer the `&mut W` blanket impl); codex 225,752 (98.2%), pairs 5. Frozen /tmp/cbm-score-97320c5f,
/tmp/cbm-score2-97320c5f; 414fa176 scores, rp clone and evidence trashed; branch fleet/rp-61-med deleted.
Repo hygiene (owner order 15:30): 131 merged branches deleted by me, 75 unmerged (s3 chain retired by the
30 Sep decision, w1-w9 lanes) exported to private/rust-run/retired/<branch>.patch and deleted by the owner;
3 stashes dropped by the owner (exports in stashes/). Repo = main + live arm branches only; the branch of
an arm is deleted in the step that lands or rejects it.
Wave 5 decisions: ua-61-med DONE 5/5 ($1.77, 26 min; codex +2, ruff +39, 0 losses of calls) — merged
main-now cleanly in its clone, TRIAL RUNNING on 97320c5f (/tmp/cbm-trial-score{,2}-ua). bs-61-med DONE
5/5 ($3.49, 51 min; codex +64, ruff +814, pairs unchanged, 1 new flagged ruff USAGE pair judged correct)
— 3 conflict hunks with RP in rust_lsp.c; RESUMED 16:00 (thread 01a10f7a-e377-7b11-9b6b-9fb458581602,
followup -> report2) to resolve and re-score vs 97320c5f. it-61-med PARTIAL 3/5 ($2.54, 38 min; ruff
+657 but 2 new wrong pairs = the RP defect, 73 confirmed ruff calls + 73 USAGE lost unaccounted) — 1
conflict hunk with RP; RESUMED 16:05 (thread 01a10f7b-28e4-74d3-a0d6-8e25d4259e31, followup -> report2):
no new wrong pair, every loss read. ww-61-med round 2 (report2, DONE on the revised standard): zero
CALLS/USAGE change, codex WRITES -23 true +3, ruff +1, a second proof walk added — NOT LANDED; the ~1,900
WM-withheld WRITES are not provable from the owner type (4 corpus sites in total) — item CLOSED as a
negative result; clone and evidence trashed. Patches of all five in fleet/patches/ (ww-61-med.rev2.patch).

### 06 Oct 14:10 — wave 4 landed, wave 5 launched

main = 414fa176. Landed since 12:30 (SCORE.md): f4aa834a CP (harness only: all 175 flagged pairs read —
codex 0 graph-wrong, 6 truth-wrong, 98 scorer-wrong, 29 conditional; ruff 22 graph-wrong, 3 scorer, 17
conditional; truth generator + precision.py repaired, bench tables regenerated, old as `*.pre-cp`; codex
pairs 133 -> 5, ruff 42 -> 33, graph unchanged; coordinator read 3 reclassified codex pairs in source: all
correct links into dev-dep-cycle crate core_test_support), 414fa176 BI partial (one-deep borrowed receiver +
explicit &self method dispatches; borrowed Vec/slice iteration keeps its reference layer; ruff 99,847 ->
101,491 (90.4%), pairs 33, 0 new; codex +64, pairs 5). codex 225,715 / 229,952 (98.2%), pairs 5 (4 WRONG
TARGET deref/method dispatch: network_policy.rs:363 decide, update_loop.rs:87/287 recv, runtime.rs:777
on_blocked_request; 1 NOT IN TRUTH windows_sandbox.rs:381). ruff 33 = 22 graph-wrong (audit/cp/graph-defects.tsv:
19 reference receiver loses trait precedence rust_lsp.c:7877/:7816/:12569, 2 generic T::method :11559/:11601,
1 F: Setup closure :12534) + 11 conditional. Frozen: /tmp/cbm-score-414fa176, /tmp/cbm-score2-414fa176;
every older frozen score, trial score and finished clone is in the Trash (bi-61-med, cp-61-med clones too).
Trial builds reuse /tmp/cbm-trial (BUILD_DIR); trial score dirs are trashed after the decision, summaries
kept as fleet/evidence/<arm>.trial-{codex,ruff}.txt.

ww-61-med (task WW, WRITES by proved owner; report 13:38, PARTIAL 3/5, $5.16, 81 min): NOT LANDED. Trial on
414fa176: codex calls -356 confirmed, Field USAGE -428, WRITES edges -20, pairs 5 -> 4; ruff calls -216,
Field USAGE -171, WRITES +1, pairs 33. Its shared typing guards (rust_lsp.c:5640 + generic/foreign/container
receiver guards) reach CALLS/USAGE. Tree committed on branch ww-61-med in its clone, main 414fa176 merged
(84dd8531, clean); RESUMED 13:55 (thread 01a10f00-6a84-7ae3-acb1-5e6c3d1272bf, out/ww-61-med.followup.txt ->
report2.md): standard = no confirmed call/USAGE lost on either corpus vs 414fa176 unless read false; WRITES
listed with proof; the four criterion-1 sites publish. The 203-entry WM ledger is gone with the wm clone.

Wave 5 launched 14:00 (6.1 medium, branches fleet/<arm>, clones /tmp/cbm-fleet/<arm>): rp-61-med (task RP:
the 22 ruff graph-wrong rules — soundness first), bs-61-med (BS: WORKLIST rank 6 bound_strategy_closed 424
sites + rank 9), ua-61-med (UA: rank 8 ufcs_argument_kind_gate 358+4), it-61-med (IT: ranks 10+12 iterator
Item typing 229+17, 175+12, + rank 13). RP/BS/UA told not to touch each other's selection rules; conflicts
resolved by the arm in its clone at merge time as before. Queue after wave 5: BI remainder (deeper borrows,
blanket-body forwarding: 506+70 QT-withheld, 67+14 TD-withheld), WORKLIST ranks 7, 11, 14-19, 6,729
unassigned ruff misses, From-argument proof (jwf), unresolved-site marker, 2 self-recursion sites, the 4
codex WRONG TARGET pairs, stash0's 15 tests (salvageable with rework), WM residue (StartupProbe cfg export,
fn-local struct identity).

Reported to owner, not acted on: stray host projects `jwm-review`, `ra-audit` in ~/.cache/codebase-memory-mcp
(two arms used the host MCP index_repository); 105 GB ~/.codex/plugins/cache/project-management/.../target
(owner's plugin build); /tmp/cbm-run read-only remnants in Trash.

### 06 Oct 11:00 — wave 3 results

main = 6826f1c5. Landed this hour (SCORE.md): 03777ea7 MA (ruff +583), 7b9e746d QT (ruff pairs 278 -> 66,
212 wrong removed; 506 ruff + 70 codex confirmed calls withheld for borrowed receivers — blanket-impl proof
queued), 6826f1c5 AC (ruff +985). ruff 100,005 / 112,301 (89.1%), pairs 66; codex 225,662 (98.1%), pairs
133. ds-61-med: cause gone after WM/ED (2,250 of 2,303 ruff sites and 202 of 220 codex now resolved);
no change. td-61-med: PARTIAL, overlapped QT; tree committed on branch td-61-med in its clone, merge of
main-now started there (conflicts extract_defs.c, rust_lsp.c), arm RESUMED (thread
01a10e72-0d28-7d80-830f-2d5e652e04d2, followup -> report2) to resolve and judge all 66 + 133 pairs.
Stashes on main: stash@{0} td apply, stash@{1} ed apply — both superseded, owner may drop.

### Landed on 5 Oct (one SCORE.md line each)

| Commit | What | Arm |
|---|---|---|
| b45d8d10 | W9-P2 `.await` output (+524 calls) | p2-61-med |
| bfccf545 | Grammar: six Orchard lexical fixes + four stable constructs; `macro_rules` stays an identifier | g-61-low |
| 764021cc | Score summary repaired (H a–f) | h-61-high |
| 58f6683f | Daemon-free scoring: `scripts/index-local.sh`, `tools/rust-audit/compare_graphs.py` | s-61-low |
| 6550a3a7 | W9-P4 turbofish methods, one fn type-param carrier (key `tp`, codec 11) (+160) | p4-61-high |
| b31b4569 | (6 Oct) WR self-recursion published when the receiver type proves it (+199, pairs unchanged); two corpus recursions still withheld (records_tests.rs:140, loader/local.rs:370) | wr-61-med, revised after judge |
| 8b5c22ae | (6 Oct) WF sole unregistered trait candidate withheld, from/into only by argument (-267 calls, -4 wrong pairs); conflict with WR in rust_ufcs_target resolved by coordinator | wf-61-low |
| 8f8e6299 | Truth generator corrected and tracked in git (`tsdiff/src/bin/*.rs`); bench tables replaced, old kept as `~/.cache/cbm-rust-bench/*.pre-tg` | tg-61-high |

### In flight / awaiting decision (wave 2: remove wrong links; all arms branched from 58f6683f = graph of b45d8d10)

| Task | Arms (clone /tmp/cbm-fleet/<arm>, report fleet/out/<arm>.report.md, score dir) | State | Next action |
|---|---|---|---|
| WM inline-module identity + USAGE binding | wm-61-high (B; patch in fleet/patches, clone kept), wm-61-med (A, rejected) | Judge jwm-61-high (01:27): land neither. B: all 15 sampled added links true, fixes both jwu fixtures; but all 4,072 lost confirmed USAGE pairs are source-proved (3,612 Enum: `E::V {..}` qualifier dropped, extract_usages.c:64-80), 6,252 WRITES withheld wholesale (21 of 23 sampled true), generic-parameter shadow publishes a wrong link, block-level `use` omitted, cfg-twin call pair added, Proto->Rust USAGE, 1,076 new unconfirmed pairs unread. Coordinator trial (worktree /tmp/cbm-trial, main + B): calls 225,644, pairs 134; + wi-56-med: IMPORTS not confirmed 951 -> 387, 5 of WI's 9 wrong USAGE remain (tui exec_cell/model.rs:233,248,252, chatwidget/exec_state.rs:57, history_cell/tests.rs:2038 -> protocol.rs:3556). wm-61-high RESUMED 01:45 with out/wm-61-high.followup.txt (thread 01a10b9a-8947-73a1-bc77-7c0b3e4ed518) -> out/wm-61-high.report2.md | grade report2; apply the clone's diff to main (3-way), score on codex AND ruff, run the judge's fixtures (/tmp/jwm-61-review, /tmp/jwu-61-high/valid-probe), suites, then land. The judge indexed once through the host CLI by mistake (log ~/.cache/codebase-memory-mcp/logs/jwm-review-1791211153.log): told the owner |
| WU USAGE name guess | wu-61-low (A), wu-61-high (B) | Judge jwu-61-high: land neither. All 3,015 lost Function+Variable links are source-proved; both still publish a wrong link on two compiled fixtures (/tmp/jwu-61-high/valid-probe `external::Same`; build.rs `use external::Remote`). wu-61-high resumed 00:18 and stopped BLOCKED at 00:31 (report2): needs rust_modtree.c to return the identity of a constant at an occurrence — which wm-61-high adds | superseded by WM-B. After WM lands: measure the judge's five standards (end of jwu report criterion 6) and the two fixtures on the new main; whatever remains is one follow-up task on the new main (fresh arm, or resume wu-61-high thread 01a10b9a-80b7-7520-9c59-ec2b5cd4c211 after rebasing its clone) |
| WI IMPORTS name guess | wi-56-med (land this diff), wi-61-med | Applies cleanly to main. Not landed: exposes wrong USAGE through the bare-name fallback (9 on old main, 5 on main + WM-B) | land after the fallback is closed (WM + WU follow-up), verify the five sites publish nothing |
| WF `From` recall | landed 8b5c22ae | Judge jwf-61-high: 229 correct calls withheld; matcher defects named (reference mutability type_rep.h:91, hidden inherent methods, suffixed literals rust_lsp.c:10966, `.into()` / `?` sites publish nothing) | queue item 3 "prove the From argument" starts from jwf-61-high.report.md and /tmp/jwf-61-high/decision.md |

Judges use fleet/tasks/_JUDGE.tmpl (independent review of two solutions; its sample, its re-execution).

### Queue after wave 2 (not started)

1. Unresolved-site marker: every call/usage the resolver gave up on is recorded and queryable, so
   "no callers" can be told from "unknown". Not started; required for daily use.
2. Second, held-out corpus with its own truth (candidates beside the repo: ../cargo ../bevy ../ruff
   ../serde ../nextest; needs `rust-analyzer scip` outside the sandbox, CARGO_TARGET_DIR outside the
   corpus; codex took ~60 min, 22 GB peak). Required before any claim that gains generalise.
3. Recall by proof, after the guesses are gone: `From` argument proof; names proved by import;
   upstream typing (`Some/Ok/Err`, `vec![]`, impl-self type args, `loop`, `async {}` — FINDINGS §3);
   Struct/Enum path heads (~43,500); Field writes / shorthand patterns / variant literals (~70,000);
   EnumVariant links (0 of 66,424); macro-generated definitions (373 types).
4. Remaining soundness: forwarding impl on Arc/Box<dyn Tr> (3 pairs), QN collision x.rs vs x.proto,
   fn-local namesake structs, cfg alternatives labelled.
5. Harness leftovers: port from h-56-high the module-level recall line (13,402 / 14,736) and the bound
   on unverifiable site credits; rust-score.sh:74 KeyError when a category has no misses; families.py:97
   sample of <10; 4-94 node-property differences between two runs of one binary (`importance`,
   `transitive_loop_depth`) — nondeterminism, unexplained; test_incremental.c:3157 skips silently when
   its fixture download fails; `tools/rust-audit/test_truth.py` needs TSDIFF_BIN_DIR.
6. Grammar leftovers: resolver does not read `u8<>` / bounded dyn types (missing, not wrong); ten
   nightly-only constructs unparsed; frontmatter edge cases (orchard/report1.md).

### Fleet mechanics (all under fleet/)

- `launch.sh <arm> <model> <effort> <task> <branch> [merge-ref|-] [net]`: clone to /tmp/cbm-fleet/<arm>,
  one sandboxed `codex exec`; brief = COMMON.md + tasks/<task>.md with `@BASE@` = frozen score of main
  at launch (/tmp/cbm-score-<sha8>, must exist: freeze after every merge with `cp -R /tmp/cbm-score-main`).
  NEVER edit launch.sh while arms run (bash re-reads it: truncated jsonl, exit 127). NEVER re-score a
  directory an arm reads.
- Sandbox facts: `.git` read-only (coordinator commits in the clone, `git fetch <clone> <branch>:fleet/<arm>`,
  then merge.sh); Unix sockets denied (no daemon); network closed unless `net`; arms score themselves
  with the daemon-free path. Arms share /tmp and can read sibling clones.
- Resume an arm: `cd <clone> && codex exec resume <thread> --skip-git-repo-check -m <model>
  -c model_reasoning_effort=… -c sandbox_mode="workspace-write" --json -o out/<arm>.report2.md "$(cat followup)"`;
  thread id = first line of out/<arm>.jsonl, or the rollout file name in ~/.codex/sessions/2026/10/05
  whose cwd is the clone.
- `usage.py`: minutes, tokens, notional $ per arm. `grade_g.py <arm>`: grammar grader. `AB.md`: results.
- The coordinator re-scores every merge on the merged tree; only that figure is recorded. No agent's
  self-score is recorded. A request-file service running things outside the sandbox was refused by the
  safety classifier: do not rebuild it.
- Merging a grammar change: delete `prod_grammar_rust.o` (main build) and `grammar_rust.o` (test build)
  first; the Makefile does not rebuild them.
- Capacity: at most ~6 arms at once (14 drove load to 87 and tripled every duration). DISK: 41 GB free
  at 23:45 (was 197 GB); /tmp/cbm-fleet holds ~30 clones with builds and score dirs. `rm` is blocked for
  the coordinator and `trash` frees nothing: the owner must empty the Trash or delete finished clones.
  Do not launch new arms below ~25 GB.
- Model choice from AB.md: 6.1 low/medium is the default; 5.6 cost 4-12x for no better result (not to
  be used again without a reason); effort level did not predict correctness; two arms + independent
  grading or a judge is what catches defects.

### Other branches

`w9/enum` (enumeration instrument, never merge), `w7/twin-digest`, `w8/enum` (superseded), `fleet/*`
(merged arm branches, deletable), `w9/p2-await-output`, `w9/p4-turbofish-method` (merged via fleet/p2, fleet/p4).

## How a merge is done (coordinator)

1. Read the report header only: `sed -n '1,/^---$/p' <report>`.
2. Commit the arm's tree in its clone, `git fetch` it as `fleet/<arm>`, write a 3-line pseudo-report
   (STATUS/BRANCH/---) and run `private/rust-run/merge.sh <report> x`: merges without committing, builds
   /tmp/cbm-main-build, commits the merge only if the build passes (aborts and leaves main
   unchanged otherwise, exit 4; conflict = exit 3), re-scores into /tmp/cbm-score-main, runs
   the Rust suites for a merge commit. Chain merges in one background command each, polling
   the previous log for its `exit=` line; never wait on `pgrep -f merge.sh`.
3. Append a SCORE.md line; remove the agent's worktree (`git worktree unlock`, then plain
   `git worktree remove`; a refusal means uncommitted work — leave it and report).
4. Copy /tmp/cbm-run/reports/* and /tmp/cbm-run/merge-*.log into private/rust-run/ — /tmp
   does not survive a reboot.
5. Judge a class by its sites recovered (join classified.tsv to the new misses), never by the
   score change of the merge.

## Dispatch rules

- Every brief = BRIEF.md + W9-ADDENDUM.md + one cause. One cause per agent; a brief with a
  "count / reproduce first, then fix" step runs long (agents past 200k tokens on 4 Oct:
  std-trait-paths 205k, std-head-proof 249k, closure bounds 205k, P5 216k, enumeration 387k) —
  split diagnosis from fix.
- State only measured facts in a brief. Two briefs on 4 Oct carried unmeasured premises
  (glob IMPORTS; the W8-I2 threshold) and each cost a round.
- Neighbouring classes that need the same plumbing get one prerequisite agent first (P4 and P5
  both built fn type-param transport and now conflict).
- At most one agent per region of internal/cbm/lsp/rust_lsp.c. Agents branch from local `main`,
  rebase once before gates, never push, never commit on main, never run the score script on
  /tmp/cbm-score-main.
- Agents cannot write under private/; reports go to /tmp/cbm-run/reports/<name>.md, done-signal
  is one message to `main`. General-purpose (non-worktree) agents cannot write report files at
  all: they return text, saved with `audit/extract.py <agent-id> <out>`.
- Disk: dispatch only above ~25 GB free; `rm` is blocked, `trash` frees nothing. Another
  session (project-management) shares the machine: load 60-140 and a disk dip to 23 GB on 4 Oct.
- Load flakes (pass when the suite is re-run alone with fresh runtime dirs):
  tool_detect_changes_* (test_incremental.c:1942-2066, test_mcp.c 10592/11547/11632/14624/
  14717/15110/15297/15808), test_watcher.c 1748/2341/2583/3095/3237, test_daemon_bootstrap.c:1116,
  test_daemon_runtime.c:2846, test_daemon_frontend.c:1619, test_daemon_application.c:2458,
  test_extraction.c:6405, test_cli.c:1975, test_subprocess.c:110/157, test_py_lsp_scale.c:109.
- Harness facts: `git worktree remove --force`, `git add -A`, `git checkout -- <path>` are
  blocked by hooks; `cd` into another directory moves this session's cwd — use absolute paths;
  Makefile.cbm does not rebuild a grammar object when its parser.c changes; scripts/test.sh
  deletes the production binary in BUILD_DIR and fails at step 5e when the private
  CBM_RUNTIME_DIR does not exist; macOS has no `timeout` command.
- Second opinions: Sol 6.1 high via `audit/sol.sh <letter>` (codex exec, workspace-write,
  scratch dir as workdir; it cannot run the indexer in its sandbox) and round 2 via
  `audit/sol2.sh` (resume fails while the first run is still alive: wait for exit.txt).

## Decisions

Owner's, standing: completeness criterion (every provable link; cost is no reason to stop);
no rust-analyzer at index time; a wrong link is worse than a missing one; self-recursive
CALLS dropped; a call to one of two cfg twins publishes nothing; the Linux half of the
build-fingerprint check has never been compiled here (CI must build before a push). The
audits put the fourth and fifth under question — see FINDINGS §6.

Coordinator's (owner may reverse): declared signature tables for well-known dependency crates
(tokio, tempfile merged); a merge that loses only name-matched "lucky" links is accepted;
build every link a bounded sound inference can prove. **Withdrawn after the audits:** "a Rust
`use` with no evidence keeps the old name fallbacks" (the fallback publishes ~580 wrong
IMPORTS); "+N unconfirmed is oracle blindness when the agent says so" as a merge rule — the
agent's per-site source reading is required, as it was for the last four merges.

## Artefacts

- This directory: SCORE.md, summary-<sha>.txt, run-ledger.md, BRIEF.md + W9-ADDENDUM.md (briefs of the
  earlier Claude-agent waves), merge.sh, audit/ (FINDINGS.md, results/), orchard/ (ASSESSMENT.md,
  report1.md, candidate/, inputs/), fleet/ (COMMON.md, tasks/, launch.sh, usage.py, grade_g.py, AB.md,
  out/ = every arm's report, jsonl and follow-up, patches/ = unlanded trees), reports/, merge-logs/,
  w9-worklist.md + w9enum/.
- Corpus and truth: ~/.cache/cbm-rust-bench/ (codex/codex-rs at bc98fc69f8; truth.tsv, refs.tsv corrected
  5 Oct; *.pre-tg = previous). Index they come from: private/rust-s3/codex.scip (copy at
  /tmp/cbm-truth/frozen/). Generators: tools/rust-audit/tsdiff (needs ../rust-analyzer; arms reach it
  through the symlink /tmp/cbm-fleet/rust-analyzer).
- Under /tmp (lost on reboot): /tmp/cbm-main-build, /tmp/cbm-score-main, frozen /tmp/cbm-score-<sha8>,
  /tmp/cbm-fleet/<arm> clones, each arm's evidence dir (/tmp/<arm>*), /tmp/ts-gate (rust-analyzer parser
  gate: tsdiff built against base and g-61-low grammars, registry corpus results).
- docs/RUST_GRAPH_STATUS.md is current as of 7 Oct 12:30 and is amended at the end of every wave.
HANDOFF.md is untracked (case-insensitive FS: the `?? handoff.md` in git status is this file); it is read by the session-start hook, not by git.
