STATUS: DONE
COMMIT: c185c187
CHANGED: 15 files
CLOSED: the HashMap/HashSet question (bevy_platform mount under the ruled assumption); review round 1 findings 1-2, round 2 findings 1-4 and round 3 (raw identifiers); newtype `*x` typing
OPEN: 5 (D1, D2, B3, B4, no_implicit_prelude over-exclusion)
BLOCKERS: none
SCORES: bevy 63090/84295 flagged 35 (base 61368/84295 flagged 41; the base score.log says 35); pm 39983/47116 flagged 73 (base 39983/47116 flagged 73); codex 226161/229953 flagged 1 (base 226161/229953 flagged 1); ruff 104067/112300 flagged 1 (base 104067/112300 flagged 1)
---
# Rust receiver typing lane (lane/rs-recv, rebased on main ddc8c9b0)

**Target miss.** bevy is at 63,090, 5 sites below the 63,095 target. Flagged pairs are unchanged and no CALLS pair is removed. The 5 sites are explained under commit 5d46fde9.

## The HashMap finding
It is not a collision with std's HashMap.
- bevy_platform declares its collections module as `cfg::alloc! { pub mod collections; }` in crates/bevy_platform/src/lib.rs.
- The module tree read an item-macro call only as "may declare unseen names". So every path through `bevy_platform::collections` failed.
- Field and parameter types spelled `HashMap<..>` therefore stayed the written path, and inherent lookup stopped with method_not_found. On main there were 545 final stops on `HashMap` and 137 on `HashSet`.

## Commits (shas after the rebase)
1. **9320339d**: mount `mod name;` from any item-macro brace body. The review found two defects in it: a wrong link (the macro may drop its input) and a stale digest. Both are fixed by 3ad30f2b and 5d46fde9.

2. **ad3379be**: `*x` on an owned single-field tuple struct with no readable Deref target is unknown. Review: no finding.
   - It fixes bevy crates/bevy_pbr/src/render/mesh.rs:1493, `u16::from(*render_lightmap.slot_index)`, where LightmapSlotIndex is a `#[derive(Deref)]` newtype. The resolver had linked it to the wrong `From` impl; it now withholds.

3. **3ad30f2b**: a macro that the local expander reads decides by its selected arm.
   - An out-of-line `mod` is mounted only when it is transcribed at item position, with no cfg on it or on the invocation.
   - The file digest hashes the whole invocation text (review finding 2).
   - Fixture: a `gate!` macro whose arm drops `mod dep;`, beside a dependency crate `dep` and a local dep.rs. It now links the dependency. 9320339d linked app/src/dep.rs (WRONG).

4. **5d46fde9**: mount a macro-declared module when nothing else can bind its name.
   - **Rule (named bounded assumption, ruled by the coordinator).** A `mod name;` inside an item macro the local expander cannot read (a path, an alias, or another file's definition) is mounted only when all of these hold:
     - its file exists;
     - the crate has no dependency, extern crate or language crate of that name;
     - the module has no glob import;
     - the module has no other item, import or module of that name;
     - there is no second macro-declared candidate for the name.

     Otherwise the may-declare fact stays.
   - **Argument.** A consumer path through `name` compiles only if the name is bound. With no other candidate binding, the macro-emitted declaration is that binding.
   - The review's counterexample is excluded by the dependency clause.
   - Candidates are recorded at scan time (RMF_MACRO_MOD) and mounted after the crate's tree is built, so every glob, item and import of the module is known first.
   - **Observation, fixture tests/fixtures/graph/rust/ownmap.**
     - plat mounts `collections` through `cfg::alloc!`, where cfg.rs has `pub use crate::enabled as alloc`.
     - app has `plat::disabled! { mod dep; }` and `gate! { mod dep; }` beside a dependency `dep` and a local app/src/dep.rs.
     - Result: 14 of 14 sites right in both modes. bc4a427d (now 3ad30f2b) got 13 right and 1 withheld.
   - **Observation, corpus.** bevy crates/bevy_input/src/touch.rs:307 `self.just_released.get(&id)` now links to the truth target, collections/hash_map/impl#[HashMap<K, V, S>]get().
   - **Cost against the unconditional mount: 5 sites.** crates/bevy_utils/src/lib.rs writes `cfg::alloc! { mod map; pub use map::*; }` in a module that also has glob imports (`pub use bloom_filter::*`). The glob clause therefore withholds `map`, and the 5 TypeIdHashMapExt calls into HashMap stay unlinked.

5. **ff8aa3b4**: round 2 review, four findings fixed.
   1. **Registration order.** The dependency check no longer depends on the order crates are registered:
      - candidates are mounted once, after every crate root exists;
      - any dependency key of the package (an `externs` entry) counts as a binding, whether or not its root is registered yet.
   2. **cfg detection.** A cfg in an expansion is now read by the attribute's name token, so `#[cfg (any())]` is caught.
   3. **Prelude names.** Standard-prelude names (RMT_PRELUDE) now count as bindings.
   4. **`mod` keyword.** The byte prefilter is now `mod`. The parse recognises the keyword as a token, so `mod/*c*/slot;` is counted as a rival.
   - **Observation.** The ownmap fixture now lists `app` first among the workspace members.
     - The 5d46fde9 binary fails it: `CALLS app.src.main.dropped -> dep.src.lib.Thing.ping: WRONG -> app.src.dep.Thing.ping`.
     - ff8aa3b4 gets 14 of 14 right in both modes.
   - **No fixture for the reviewer's dep2 variant.** The fixture harness cannot build renamed dependencies (`package = ..` raises KeyError in fixtures.py). That variant exercises the same ordering defect shown above.
   - **No fixtures for findings 2-4.** Each is a one-line check in the code; their effect is not observable as a run-time call in the harness.

## Removed pairs (edgediff against the frozen base, final tip)
- **bevy**: no CALLS pair removed; 1,188 added.
  - IMPORTS: 260 removed, 268 added. Imports that fell back to `score.crates.bevy_platform.src.lib` or `score.src.lib` now reach the defining HashMap, HashSet, Entry, AlignedVec and FixedHasher.
  - SEMANTICALLY_RELATED: 42 removed, 25 added. This is similarity churn from the changed graph.
- **pm**: 8 removed, all Python USAGE and WRITES. They are identical on main (lane/py).
- **codex** (7 CALLS removed) and **ruff** (1 CALLS removed): edgediff from main to the tip is 0 removed and 0 added on both. These come from lane/rs and lane/py on main, not from this lane.

## Verification on ff8aa3b4 (on main ddc8c9b0)
- **Build:** `make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/rs-recv` rc=0, no warnings (-Werror).
- **Fixtures:** see the summary line appended below.
- **Suites:** see the per-suite lines appended below. A LeakSanitizer rc=1 with 0 failed assertions is the known pre-existing condition.
- **Scores** (one corpus at a time, scripts/rust-score.sh):

  | corpus | calls ALL | flagged |
  |---|---|---|
  | bevy | 63090 / 84295 (74.8%) | 35, same pair set as base |
  | pm | 39983 / 47116 (84.9%) | 73 |
  | ruff | 104067 / 112300 (92.7%) | 1 |
  | codex | 226161 / 229953 (98.4%) | 1 |

## Open
- **Known over-exclusion: `#![no_implicit_prelude]`.** The candidate check counts standard-prelude names as bindings even in a module under `#![no_implicit_prelude]`. There no prelude binding exists, so a macro-declared module with a prelude name (for example `mod Option;`) is withheld even though the mount would be sound. This is withheld, not wrong, and was left as is by ruling.
- **D1** (Res 2,396, ResMut 1,620, Mut 308, Single 115 final method_not_found stops).
  - The receiver types are established. The missing piece is the Deref target, which is emitted by `change_detection_impl!` (defined in change_detection/traits.rs:367, invoked at params.rs:524 and after).
  - The extractor does not see macro-generated impls, and the shared registry is built before the macro table is available to it.
  - Fix: expand cross-file item macros into synthetic impl relations at one seam that both pipelines use.
- **D2.** In examples the receiver is a module-local placeholder (`<module>.Query`), so the `lsp_prelude_trait` best effort names no node.
  - `bevy::prelude::*` reaches `Res` but not `Query`, which comes through `pub use query::*` in bevy_ecs/src/system/mod.rs.
  - I have not yet located the failing glob step.
- **B3 and B4** (the parts not involving HashMap): not started.
- **Default-feature model.** Not needed for this lane after the second ruling. It remains the route for the inventory's P3 class.

## Final verification lines (ff8aa3b4)
- Fixtures: `fixtures: 52 fixtures x 2 modes, 93 known mismatches, 0 failures, 72.6 s`
- Suites: `1962 passed, 0 failed, 0 skipped`.
  - call_reference_contract 15, rust_lsp 836, edge_structural 39, lang_contract 43, extraction 487, pipeline 542.
  - edge_structural and pipeline exit rc=1 from LeakSanitizer with 0 failed assertions. This is the known pre-existing condition.
- Scores on ff8aa3b4:

  | corpus | calls ALL | flagged | CALLS removed vs frozen base |
  |---|---|---|---|
  | bevy | 63090 / 84295 | 35 (same pair set) | 0 |
  | pm | 39983 / 47116 | 73 | 0 |
  | ruff | 104067 / 112300 | 1 | 1 |
  | codex | 226161 / 229953 | 1 | 7 |

  The ruff and codex removals come from main: edgediff from main to the tip is 0 on both.

## Round 3 fix (c185c187): raw identifiers
Wherever the candidate logic compares names, a leading `r#` is stripped on both sides:
- `#[r#cfg(..)]` is read as a cfg attribute;
- `mod r#slot {..}` binds the same name as a candidate `mod slot;`, and the binding lookup checks both the `slot` and `r#slot` keys;
- the rival count, the prelude check, the package-dependency check and the extern-prelude check all use the bare name.

No fixture builds a raw-identifier module.

Verification on c185c187:
- Build: rc=0, no warnings (-Werror).
- Fixtures: `fixtures: 52 fixtures x 2 modes, 93 known mismatches, 0 failures, 41.4 s`.
- rust_lsp suite: `836 passed, 0 failed, 0 skipped` (rust_lsp rc=0).
- Scores were not rerun on c185c187; the round-3 instructions asked for fixtures and the rust_lsp suite only. The last scored tip is ff8aa3b4, given in the table above.
