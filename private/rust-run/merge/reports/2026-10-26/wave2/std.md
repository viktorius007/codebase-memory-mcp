STATUS: DONE
COMMIT: ec7b1e667fdfb3e52e1a0f12a807f4675b249589 (lane/w2-std rebased on main 13ae6670, 6 commits; code unchanged since fdee2b90 apart from rebases)
CHANGED: internal/cbm/lsp/rust_lsp.c, internal/cbm/lsp/rust_std_projections.h (new, 56 rows), tests/fixtures/graph/rust/{stdtab,stdtab2,stdguard,guardleaf} (new)
CLOSED: review finding (5 wrong links of the class "table projection ahead of a corpus trait impl, a reference-level Index impl, a local shadow, a range-typed index") by 2cdd6bc2; B2/B1 sub-cause "lock guard named through a cfg-selected re-export" (bevy_platform::sync::RwLock{Read,Write}Guard) by fdee2b90 (+182 bevy); earlier: Entry API, map index, Index autoderef, map/set iterator Items, mem::take/replace, Vec/slice get_mut(i)
OPEN: B2/B1 grouped table below (top remaining: F.Mut 292 and F.Ref 165 method_not_found heads, Handle trait_receiver_shape 144, call chains whose inner call is itself unresolved 289, Query for-loops 179+54, closure params 106+117); dotted `bevy_platform.sync.RwLock{Read,Write}Guard` heads (33 sites) still method_not_found because rust_registered_type_qn settles the path before the leaf rule; generic corpus IntoIterator impls with qualified-projection Items (bevy HashMap/HashSet `for x in &map`); vec!/Vec::new element typing; bevy USAGE extract_cameras -> RenderExtractedVisibleEntities.classes (Query::get_mut `D::Item` typed as head `D.Item`, exposed by mem::take)
BLOCKERS: none. Note: an incremental BUILD_DIR reused across rebases produced a binary that hit stack smashing in cbm_parallel_resolve. Clean build dirs do not, so build each rebased tip in a fresh dir.
SCORES: bevy 71445/84295 f35; pm 45167/47116 f87 (main 7b274c86 is also f87); codex 226242/229953 f1; ruff 104504/112300 f1 (measured on accab627, the same code rebased on 7b274c86)
---
# Final verification
- Tip ec7b1e66 on main 13ae6670: build in a fresh dir (/scratch/agent/build/w2-std-fresh3) with 0 warnings under -Werror. rust_lsp suite: 836 passed, 0 failed, EXIT=0. Fixtures: 74 fixtures x 2 modes, 96 known mismatches, 0 failures.
- 9879d7e5 on main 6ab8c225 (fresh build dir): the six suites (rust_lsp pipeline edge_structural lang_contract extraction call_reference_contract) gave 1961 passed, 0 failed. EXIT=1 comes from LeakSanitizer only (2 leak reports). Log: /scratch/agent/tmp/w2-std-tests7.log.
- Scores on accab627 (rebased on 7b274c86) against main 7b274c86 built and scored the same way. Dirs: /scratch/agent/score/w2-std/{s10,m10}-*. Edge diffs: d10-*.
  bevy 71,127 -> 71,445 (+318), flagged 35 -> 35.
  pm 45,164 -> 45,167 (+3), flagged 87 -> 87. The rise from 73 is on main, not this lane.
  codex 226,200 -> 226,242 (+42), flagged 1 -> 1.
  ruff 104,466 -> 104,504 (+38), flagged 1 -> 1.
  CALLS removed: 0 on all four.
  Other removed edges: SEMANTICALLY_RELATED churn only, plus the one bevy USAGE pair extract_cameras -> RenderExtractedVisibleEntities.classes explained below.

# w2-std, round 2 (resolve-dump diagnosis, review fixes, guard leaves)

## Final tip against main c4a6b353 (main = w2-recv's final scores, /tmp/w2r-<corpus>)
bevy 67,813 -> 68,127 (+314) flagged 35 -> 35; pm 42,364 -> 42,367 flagged 73 -> 73;
codex 226,198 -> 226,240 flagged 1 -> 1; ruff 103,872 -> 103,910 flagged 1 -> 1.
CALLS removed 0 on all four (tools/rust-bench/edgediff.py against /tmp/w2r-<corpus>/cache/score.db).
Other removed edges: SEMANTICALLY_RELATED only (26/0/17/15, similarity churn), plus the one bevy USAGE
pair explained under "Removed pairs" below. Score dirs /scratch/agent/score/w2-std/s7-*.

## Method: B2/B1 grouped by first failing step
CBM_RUST_RESOLVE_DUMP on bevy with the tip binary (dump /scratch/agent/tmp/dmp2/dump.tsv), restricted
to the B1 and B2 sites of /scratch/agent/tmp/rs-inv/bevy.cls.tsv (6,390 distinct sites; that file is
from frozen base b9a7012c, so 705 of them are credited now). Script
/scratch/agent/tmp/w2std2/group.py <dump> <score.db> <cls.tsv> [n]: for each site it takes the site's
last R row (callee identifier on the truth line), its stop, and the receiver expression form. A call
receiver is looked up by its exact span (inner R row: unresolved reason, or resolved target and that
target's declared return-type head from the graph). An identifier receiver is traced to its binding
(let, let-typed, for, if/while let Some/Ok, closure parameter, fn parameter) and the binding's
initialiser is classified the same way. Full table: /scratch/agent/tmp/w2std2/b1b2-grouped-tip.txt.

Columns: sites | class | site stop | receiver form or binding | first failing step | example.
```
total 6390
521 | B2 | CREDITED-now |  |  | 
289 | B2 | unknown_receiver_type | call | inner-unresolved:unknown_receiver_type | tools/build-easefunction-graphs/src/main.rs:74 .graph
233 | B1 | unknown_receiver_type | param | ? |  | examples/large_scenes/bevy_city/src/main.rs:117 scene
213 | B2 | method_not_found | call | inner-ok ret=Mut | benches/benches/bevy_transform/propagate.rs:177 .entity_mut
191 | B1 | unknown_receiver_type | binding-notfound |  |  | examples/3d/transmission.rs:539 camera_transform
184 | B1 | CREDITED-now |  |  | 
179 | B1 | unknown_receiver_type | for | ident& | param Query | examples/no_std/library/src/lib.rs:119 timer
179 | B2 | RESOLVED:lsp_method_dispatch | other | local:let=method_call | examples/large_scenes/mipmap_generator/examples/test_compression.rs:72
147 | B2 | unknown_receiver_type | let | call | unres:unknown_receiver_type | crates/bevy_tasks/examples/idle_behavior.rs:16 pool
144 | B2 | trait_receiver_shape:14:score.crates.bevy_asset.src.handle.Handle:14:score.crates.bevy_asset.src.handle.Handle | let | call | ok:deref_dispatch ret=Handle | examples/large_scenes/bevy_city/src/assets.rs:424 tree_lod_material
133 | B2 | unknown_receiver_type | let | other |  | examples/large_scenes/mipmap_generator/src/lib.rs:270 tasks
117 | B1 | unknown_receiver_type | other | local:closure_param | crates/bevy_tasks/examples/idle_behavior.rs:18
113 | B1 | unknown_receiver_type | other | local:pattern_binding | crates/bevy_solari/src/scene/extract.rs:82
106 | B1 | unknown_receiver_type | closure | .? |  | benches/benches/bevy_reflect/function.rs:141 func
103 | B2 | unknown_receiver_type | call | inner-unresolved:method_not_found | crates/bevy_remote/src/builtin_methods.rs:968 .archetype
98 | B1 | RESOLVED:lsp_method_dispatch | other | local:pattern_binding | crates/bevy_solari/src/scene/extract.rs:48
94 | B1 | unknown_receiver_type | let-Some | other |  | crates/bevy_solari/src/pathtracer/prepare.rs:32 viewport
86 | B2 | unknown_receiver_type | call | inner-unresolved:function_not_in_registry | tools/ci/src/main.rs:11 ::from_env
74 | B2 | RESOLVED:lsp_method_dispatch | call method | inner-row-missing .mesh | examples/mobile/src/lib.rs:111
70 | B1 | unknown_receiver_type | let-Ok | other |  | examples/testbed/full_ui.rs:461 scroll_content
57 | B2 | RESOLVED:lsp_method_dispatch | call path | inner-row-missing ::default | examples/mobile/src/lib.rs:111
56 | B2 | method_not_found | let | call | unres:trait_receiver_partial | crates/bevy_scene/src/lib.rs:1040 root
54 | B1 | unknown_receiver_type | for | ident.iter_mut | param Query | examples/window/window_drag_move.rs:136 window
51 | B2 | no-R-row |  |  | examples/mobile/src/lib.rs:30
50 | B1 | RESOLVED:lsp_method_dispatch | other | local:closure_param | examples/stress_tests/many_cubes.rs:646
47 | B2 | method_not_found | call | inner-ok ret=Ref | examples/ecs/immutable_components.rs:78 .entity
47 | B2 | method_not_found | let | call | ok:method_dispatch ret=Mut | examples/gltf/gltf_extension_animation_graph.rs:279 entity
45 | B1 | unknown_receiver_type | let-tuple | other |  | examples/2d/rotate_to_cursor.rs:47 camera
43 | B1 | trait_scope_identity | binding-notfound |  |  | crates/bevy_remote/src/builtin_methods.rs:745 errs
42 | B1 | method_not_found | let-Ok | call | ok:method_dispatch ret=Result | examples/no_std/library/src/lib.rs:132 target
```

method_not_found B1/B2 sites by the receiver type head the resolver held (dump target column `type.method`):
```
292 F.Mut examples/no_std/library/src/lib.rs:132 .insert
165 F.Ref examples/ecs/immutable_components.rs:78 .get
83  examples/usage/debug_frustum_culling.rs:129 .physical_size
44 D.Item tests/window/desktop_request_redraw.rs:99 .rotate_x
33 crates.bevy_pbr.src.render.mesh.RenderMeshQueueData examples/shader_advanced/custom_shader_instancing.rs:169 .mesh_asset_id
27 F.DeferredMut crates/bevy_ui/src/interaction_states.rs:35 .get_mut
22 examples.3d.order_independent_transparency.ResMut examples/3d/order_independent_transparency.rs:425 .add
21 bevy_platform.sync.RwLockReadGuard crates/bevy_asset/src/server/mod.rs:254 .get_by_extension
20 alloc.boxed.Box crates/bevy_reflect/src/array.rs:332 .into_partial_reflect
18 crates.bevy_ecs.src.change_detection.params.Mut crates/bevy_sprite_render/src/mesh2d/material.rs:951 .entry
17 Self.Item crates/bevy_ecs/src/bundle/info.rs:366 .initialize
15 crates.bevy_ecs.src.world.mod.World crates/bevy_world_serialization/src/world_asset_spawner.rs:893 .run_system_once
14 crates.bevy_render.src.extract_param.Extract crates/bevy_ui_render/src/debug_overlay.rs:204 .get
14 bevy_platform.collections.HashSet crates/bevy_ecs/src/schedule/schedule.rs:1396 .is_empty
12 bevy_platform.sync.RwLockWriteGuard crates/bevy_asset/src/server/mod.rs:200 .push
```
Read: the heads `F.Mut`, `F.Ref`, `F.DeferredMut`, `D.Item`, `Self.Item` are generic or projection
paths left unresolved in a return type (w2-recv / next lane); Mut/ResMut/Res/Extract heads remain where
the receiver came from a call chain; `RenderMeshQueueData` and `bevy_platform.collections.HashSet` are
corpus types without the method on their own impl (trait or Deref target).

## Sub-causes fixed this round
1. 2cdd6bc2 review fix (coordinator's five confirmed wrong links). Guards: Entry and get_mut rows withhold
   when the corpus registers a trait impl method of that name on the std receiver; get_mut's element row
   needs an index proven integer (a range variable withholds); `a[i]` peels a reference only when every
   corpus Index impl of the type is on the owned type with a readable self type; a producer path that is
   a bare identifier bound as a local value is not the std fn. Fixture stdguard: g_trait_entry,
   g_trait_get_mut, g_shadow_take were WRONG/FALSE-STOP on 21604362 and are withheld; g_ref_index and
   g_std_take stay right. bevy 63,188 -> 63,185 (pre-rebase), flagged unchanged.
2. fdee2b90 guard leaves (largest std-side sub-cause in the dump after the Mut/Ref/Res heads, which are
   w2-recv's): `RwLockWriteGuard` 68 + `RwLockReadGuard` 44 bare heads, from bevy_platform::sync's
   cfg-selected `implementation` re-export. Table rows CBM_STD_GUARD_LEAF RwLockReadGuard,
   RwLockWriteGuard, MutexGuard -> std.sync.*; rule: a head no corpus type claims and the use graph does
   not settle is read by its last segment as the std guard, which derefs to its first type argument.
   Observation: bevy crates/bevy_remote/src/schemas/json_schema.rs:529 `type_registry.get(..)` on
   `let type_registry = atr.read();` matches truth impl#[TypeRegistry]get(). Fixture guardleaf: l_write,
   l_read were FALSE-STOP method_not_found before. bevy 63,185 -> 63,367 (pre-rebase), flagged 35.
A third sub-cause was not attempted in the time box: the next std-side head in the dump is the dotted
`bevy_platform.sync.RwLock{Read,Write}Guard` (33 sites) that the leaf rule does not reach.

## Verification on fdee2b90
- Build warning-free (-Werror): make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/w2-std.
- Fixtures: 63 fixtures x 2 modes, 94 known mismatches, 0 failures.
- Suites: not confirmed on this tip. Two background runs of scripts/test.sh died after linking build/t-w2-std/test-runner, with no EXIT line and no failure output. Cause not found; another process may have killed them. Last completed run: 21604362, 1962 passed, 0 failed (exit 1 from LeakSanitizer only).

# Round 1 report (commits 1-4; hashes before the rebase onto c4a6b353)
# w2-std: closed std-library table for Rust receiver typing

## Finding that shaped the lane
Main already carries a large std table (rust_std_methods.h: Option, Result, Vec/slice, String,
Path, Rc/Arc/Box, Mutex/RwLock/RefCell/Cell, OnceLock, closures, producers, iterator adapters).
Two probe fixtures written first (stdtab: 100 std shapes on a corpus type; stdtab2: 41 shapes
composing std with corpus generics, Deref, IntoIterator, Iterator, associated types) were right on
94/100 and 40/41 with main's binary. So the common shapes the brief lists (`.unwrap()`, `.expect()`,
`?`, `.iter()`, `.map/.filter/.enumerate/.zip/.chain`, `for x in v`, `if let Some(x)`, `.get()`,
Box/Rc/Arc Deref, `.as_ref()`, `.clone()`) already credit when the receiver's own type is known.
The bevy B1/B2 residue is dominated by receivers whose root is not std: `for .. in &query` (197 of
the for-loop B1 sites with a declared iterable are Query, 31 Extract), Res/ResMut Deref, Commands
and EntityCommands chains, closures stored in struct fields with fn-pointer types. Those belong
to w2-recv and w2-trait. This lane therefore closed the std gaps that remained.

## The table: internal/cbm/lsp/rust_std_projections.h
Closed X-macro list, no index-time I/O; each row names the std (or hashbrown) type, the member and
its documentation URL. A member not listed stays unknown.
- CBM_STD_ENTRY_OF (2): std.collections.{HashMap,BTreeMap}.entry -> {hash_map,btree_map}.Entry<K,V>.
- CBM_STD_ENTRY_VALUE (12): {std hash_map, std btree_map, hashbrown.hash_map}.Entry .{or_insert,
  or_insert_with, or_insert_with_key, or_default} -> &mut V.
- CBM_STD_INDEX_VALUE (2): std.collections.{HashMap,BTreeMap}: map[&k] -> V.
- CBM_STD_ITER_ITEM (34): Item of std.collections.{hash_map,btree_map}.{Iter (&K,&V), IterMut
  (&K,&mut V), IntoIter/Drain (K,V), Keys &K, Values &V, ValuesMut &mut V, IntoKeys K, IntoValues V}
  (btree_map without Drain), std.collections.{hash_set,btree_set}.{Iter &T, IntoIter T, Drain T}
  (btree_set without Drain), and the same hashbrown.hash_map / hashbrown.hash_set types. hashbrown
  is listed because it is the crate std's maps are built on and bevy_platform's HashMap/HashSet
  return its iterator and Entry types; this is the one dependency named by the table.
- CBM_STD_PRODUCER (2, appended to the existing producer list): mem.take, mem.replace -> ~A.
- CBM_STD_SEQ_ELEM (1): get_mut on Vec or slice -> Option<&mut T>, only when the index argument is
  not a range expression.

## Commits (each: rule, observation, credited gained)
1. 864dc903 Entry API, map indexing, Index autoderef. Rule: a method call whose receiver head names
   a table map or Entry types by its row; `a[i]` peels references off the base before trying a
   corpus `impl Index<I> for T` (rustc autoderefs the indexed base), then the table map Output.
   Observation: bevy crates/bevy_animation/src/lib.rs:864 `playing_animation.replay()` after
   `self.active_animations.entry(animation).or_default()` matches truth impl#[ActiveAnimation]replay().
   Gained: bevy +51 (63,114 -> 63,165), pm +3 (42,237 -> 42,240).
2. 275c0054 map/set iterator Items. Rule: a receiver or `for` iterable whose head is a listed
   iterator is an Iterator over the row's Item over its own type arguments.
   Observation: bevy crates/bevy_reflect/src/serde/de/struct_utils.rs:129
   `skipped_field.generate_default()` in `for (_, skipped_field) in serialization_data.iter_skipped()`
   matches truth impl#[SkippedField]generate_default(). Gained: bevy +2, pm 0.
3. fb5e5874 mem::take / mem::replace. Rule: the path call (std/core/alloc) types as the referent of
   its `&mut T` argument. Observation: bevy crates/bevy_app/src/sub_app.rs:441 `plugin.ready(app)`
   after `let plugins = core::mem::take(&mut self.plugin_registry)` matches truth
   plugin/Plugin#ready(). Gained: bevy +16, pm 0.
4. 21604362 Vec/slice get_mut(i). Rule above. Observation: bevy
   crates/bevy_render/src/material_bind_groups.rs:968 `self.slabs.get_mut(..).expect(..).free(..)`
   matches truth impl#[MaterialBindlessSlab]free(). Gained: bevy +5, pm 0.

Total against main: bevy +74, pm +3, codex +42, ruff +38 credited; flagged unchanged on all four.

## Removed pairs against main (tools/rust-bench/edgediff.py, main = gate3 score of 040a9e27, whose
scores equal main 27586789)
- CALLS removed: 0 on bevy, pm, codex and ruff.
- SEMANTICALLY_RELATED removed 30 / 0 / 17 / 12 (bevy/pm/codex/ruff): similarity edges, which also
  churn between runs of the same binary (gate2 shows the same type moving with no Rust change).
- USAGE removed 1 on bevy: crates/bevy_render/src/camera.rs extract_cameras ->
  RenderExtractedVisibleEntities.classes (line 587). Introduced by commit 3. The `match` at 578 has
  arms `mem::take(&mut **existing_render_visible_entities_cpu_culling)` (a `ref mut` binding from
  `Query<&mut RenderExtractedVisibleEntities, ..>::get_mut`) and
  `RenderExtractedVisibleEntities::default()`. Before, the first arm was unknown and the match took
  the second; now the first arm types as `D.Item` and the arms disagree, so the match is unknown.
  Root cause (diagnosed with a debug build indexing bevy, W2T trace in bevy_render.src.camera):
  `Query::get_mut` returns `Result<D::Item<'_, 's>, _>` and the existing evaluator substitutes it as
  a template whose head is the unresolved projection `D.Item`; `**binding` keeps that head and
  mem::take passes it through. The mistyping predates this lane (it is the QueryData::Item gap of
  w2-recv's Query work); mem::take only exposes it. Fix options for whoever owns it: resolve
  `D::Item` through the QueryData impl, or have the producer reject a referent whose head is a
  type-parameter projection. OPEN, no CALLS effect, flagged unchanged.
- Added non-CALLS edges (codex USAGE +356, WRITES +110, TESTS +15; bevy USAGE +26) come from the
  newly typed receivers (field reads and writes through Entry and mem::take results).

## Verification
- Build warning-free: make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/w2-std -j4 (-Werror).
- Fixtures: make -f Makefile.cbm fixtures BUILD_DIR=/scratch/agent/build/w2-std: 56 fixtures x 2
  modes, 93 known mismatches, 0 failures. New fixtures: stdtab (105 sites, 101 right, 4 withheld:
  s_vec_new, s_vec_macro, s_split_first, s_iter_peek) and stdtab2 (49 sites, 48 right, 1 withheld:
  c_generic, a bound-projection receiver). Red on main: 14 of the right sites are withheld with
  main's binary (s_map_index, s_entry, c_index_ref, c_entry_btree, c_entry_hash, c_map_index_field,
  c_drain_ret, c_iter_ret, c_vals_ret, s_mem_take_for, s_mem_take_opt, s_mem_replace, s_get_mut_vec,
  s_get_mut_slice).
- Scores (scripts/rust-score.sh as gate.sh runs it, binary of 21604362):
  bevy 63,188/84,295 flagged 35 (main 63,114, 35); pm 42,240/47,116 flagged 73 (main 42,237, 73);
  codex 226,209/229,953 flagged 1 (main 226,167, 1); ruff 103,832/112,300 flagged 1 (main 103,794, 1).
- Suites: scripts/test.sh --suites "rust_lsp pipeline edge_structural lang_contract extraction
  call_reference_contract" BUILD_DIR=build/t-w2-std on 21604362: 1962 passed, 0 failed, 0 skipped;
  exit 1 from LeakSanitizer only (the known leak exit), 685 s. Log /scratch/agent/tmp/w2-std-tests2.log.
- Score dirs: /scratch/agent/score/w2-std/s4-{bevy,pm,codex,ruff} with edge diffs in s4-*-edges/.
