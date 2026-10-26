STATUS: DONE
COMMIT: 84009fb0 (lane/w5-deref, rebased on main c3ceda65)
CHANGED: internal/cbm/lsp/rust_lsp.c (+33/-13 over the two commits), tests/test_rust_lsp.c (+60)
CLOSED (bevy sites still uncredited; main gate6 -> 2adea3ca, by file:line key against the w4 group file): G016 187->152, G005 430->317, G024 95->72, G025 97->90, G012 299->295. G020, G053 and G054 are unchanged. 1549cb92 adds 96 more credited on top; not recounted by group.
OPEN:
- Regression found and fixed: the wave-4 review fix (3add2055) broke the derive(Deref) tokeniser. Every entry after the first in a derive list was rejected, so `derive(Component, Deref, DerefMut)` never counted. Main lost those edges. The w4 review-round bevy score (73,860) did not show it, and neither did the probe, because the probe wrote Deref first.
- G020 (162, fn-item receivers run_if/pipe/xor): the cross pass writes no row for these sites. Two in-scope traits declare run_if (IntoScheduleConfigs, ObserverSystemExt), and deciding between them needs proof that the fn item satisfies IntoObserverSystem (first parameter On<E, B>) or IntoSystem. That proof is not modelled. Withheld; nothing written.
- F::Ref / F::Mut projections (world.entity(x).get/contains: 102 F.Ref and 36 F.Mut dump rows, in G025/G053): a probe shows the projection already works when x's type is known (`impl WorldEntityFetch for Entity { type Ref<'w> = EntityRef<'w>; }`). The bevy sites fail because the argument is untyped (`let &Rel(target) = ..` patterns), which is binding typing (w5-bind lane), not a deref gap.
- G053/G054 (World.entity, 80): the negative proof over corpus traits (World implements no ContainsEntity) was not written. Impls a derive or macro emits would have to be ruled out first. Withheld.
- G005 residue: Single<D> (deref to D's query item), Gizmos -> GizmoBuffer then a generic-trait method, TaskPool. Not reached.
- derive(Deref) still has no graph fixture: the fixture runner builds lib crates only and cannot build the proc-macro crate a derive needs. It is covered by the rust_lsp unit test rustlsp_crossfile_derived_deref_reaches_field instead.
BLOCKERS: none
SCORES: bevy 74936/84295 f36; pm 45167/47116 f87; codex 226360/229953 f1; ruff 104669/112300 f1
---
Main gate6 (1b64143f): bevy 74,536 f36, pm 45,167 f87, codex 226,360 f1, ruff 104,669 f1. CALLS removed against main: 0 on all four. bevy adds 410 CALLS; pm, ruff and codex have identical edge sets.

## 2adea3ca: a derive list entry after the first is read past its leading space
- Dump (gate6): `timer.tick(..)` with `timer: Mut<AnimationTimer>` and `#[derive(Component, Deref, DerefMut)] struct AnimationTimer(Timer)` gave `...params.Mut.tick method_not_found`. Probes showed all three change_detection_impl! forms (`Res<'w, T>, T, Resource` and `Mut/Ref<'w, T>, T,`) already give the hop. A debug print in rust_register_derived_deref showed derived=0 for that attribute.
- Fix: the derive tokeniser skips an entry's leading whitespace; an entry matches when the name is the whole entry or follows `::`.
- Corpus: examples/2d/sprite_sheet.rs:27 `timer.tick(time.delta())` now links timer/impl#[Timer]tick() (site gone from the miss list).
- bevy 74,536 -> 74,840 f36, CALLS removed 0, added 283.
- Test rustlsp_crossfile_derived_deref_reaches_field: bare, mid-list and path derives link; `derive(Component, DerefMut)` and `#[doc = "derive(Deref)"]` do not. It fails on the parent commit at the mid-list case.

## 1549cb92: a generic one-field derive(Deref) type derefs to its field read against its parameters
- Dump: `self.entry(..)` in `impl<SPI> ViewSortedRenderPhases<SPI>` (derive Deref of `HashMap<.., SortedRenderPhase<SPI>>`) gave `ViewSortedRenderPhases.entry method_not_found`. Generic newtypes were kept out.
- Rule: the field's written type is read as the Target of `impl<P..> Deref for Name<P..>` through rust_register_deref_target, so the receiver's arguments substitute.
- Corpus: crates/bevy_render/src/render_phase/mod.rs:1699 `self.entry(retained_view_entity)` now links collections/hash_map/impl#[`HashMap<K, V, S>`]entry() (dump row lsp_deref_dispatch).
- bevy 74,840 -> 74,936 f36, CALLS removed 0, added 127.
- Test: the same unit test's generic case, red on 2adea3ca (it returns before reading a generic type).

## Checks on the final tip 1549cb92
- Build `make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/w5-deref -j4`: rc 0, no warnings.
- `FIXTURES_WORK=/scratch/agent/tmp/w5deref/fxwork make -f Makefile.cbm fixtures BUILD_DIR=/scratch/agent/build/w5-deref`: 83 fixtures x 2 modes, 97 known mismatches, 0 failures.
- `scripts/test.sh --suites rust_lsp BUILD_DIR=build/t-w5-deref`: 837 passed, 0 failed.
- Scores, one at a time, scratch under /scratch/agent: as in SCORES.

## After review (PASS), rebased on main c3ceda65: tip 84009fb0
- 237bef6b and 84009fb0 are the two reviewed commits rebased; the rebase had no conflicts.
- Build rc 0. rust_lsp: 837 passed, 0 failed. Fixtures: 89 x 2 modes, 97 known mismatches, 0 failures.
- The review's optional assertion (a generic field `0:Inner<T>` on `Wrapper<u8>`) was tried with Inner registered generic too, and it fails: -1 at tests/test_rust_lsp.c:1608. It was dropped, so substitution of a field naming T is not covered by a unit test. Through the full pipeline it does work: a probe `#[derive(Deref)] struct Phases<P>(HashMap<u32, P>)` links `Phases<String>.get` to HashMap::get. The unit harness case was not investigated further (OPEN).
