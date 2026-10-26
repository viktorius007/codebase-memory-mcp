STATUS: DONE
COMMIT: none
CHANGED: 0 files
CLASSES: 16
OPEN: 186 (residue R: bevy 182, pm 4)
BLOCKERS: none
---
# Rust CALLS uncredited-pair inventory: bevy and pm (frozen base b9a7012c)

## Method and unit
- The unit is the scorer's unit: a truth call site that has an in-corpus target and a fn caller, and is not credited by a CALLS (caller,callee) pair. Totals reproduce score.log exactly: bevy 84,295-61,368 = **22,927**; pm 47,116-39,983 = **7,133**.
- `/scratch/agent/tmp/rs-inv/miss.py <corpus>` replicates tools/rust-audit/join.py's credit rule and writes every uncredited site to `<corpus>.miss.tsv`. For each site it also records whether the callee node exists, whether the caller has an edge to a different node with the same name, the stop reasons from `unresolved_sites` (joined on file_path, line and name equal to the callee's last segment, kinds call, reference and member; `NONE` means no row), and the source line.
- `/scratch/agent/tmp/rs-inv/cls.py <corpus>` assigns each site to a class and writes `<corpus>.cls.tsv`. The class rules are:
  - Macro classes take the enclosing `name!(` found by scanning back up to 60 lines. M2 means the name appears among the corpus `macro_rules!` names (grep into `<corpus>.mr.txt`).
  - D1, D2 and D3 parse the receiver's parameter declaration out of the caller fn header.
  - The B classes come from truth's receiver shape.
- Commands, all single-process and read-only on the bench:
  `python3 -I miss.py bevy; python3 -I miss.py pm; python3 -I cls.py bevy; python3 -I cls.py pm` (cwd /scratch/agent/tmp/rs-inv). Counts in the tables below are the cls.py output.
- Limits:
  - The macro-name scan is heuristic: 39 sites on bevy and 6 on pm got `?`, all counted in M3.
  - D1 depends on parsing the parameter type. Sites where it could not be parsed fall to D3.
  - These are site counts. Distinct pairs are fewer, because about half of bevy's credited sites already share a pair.

## Counts per class (sites)

| rank | class | bevy | pm | sum |
|---|---|---|---|---|
| 1 | M2 macro arg inside a corpus `macro_rules!` | 805 | 4,760 | 5,565 |
| 2 | M1 macro arg inside a std/log expression macro (assert*, format, vec, matches, panic, println, info...) | 2,204 | 267 | 2,471 |
| 3 | D1 method on param `Res/ResMut/Local/Single/Extract<T>` (target is T's method, reached via Deref) | 3,273 | 0 | 3,273 |
| 4 | T1 method from a blanket corpus trait on a fn item or tuple receiver (`IntoScheduleConfigs`: `.chain() .after() .in_set() .run_if()`) | 1,449 | 0 | 1,449 |
| 5 | B3 receiver is a field access (`self.f.m()`, `x.f.m()`) | 2,490 | 227 | 2,717 |
| 6 | B4 receiver with a locally explicit type or constructor (`let x = T::new(..)`, struct literal, `let x: T`, `self`) | 1,106 | 509 | 1,615 |
| 7 | P1 path call to a trait method (`Trait::m`, `Type::from`, `T::m`, `<&Self>::m`) | 1,264 | 295 | 1,559 |
| 8 | P3 path call to a cfg-dependent name (`ops::sin` in bevy_math `std_ops` / `std_ops_for_no_std`) | 459 | 0 | 459 |
| 9 | D2 method on `Query<..>` itself (`.iter() .iter_mut() .get()`) | 463 | 0 | 463 |
| 10 | B5 receiver is a const, static or unit path (`REQ_DESCRIPTOR.m()`, `DefaultPlugins.set`) | 245 | 207 | 452 |
| 11 | P2 path call to a free fn or inherent assoc fn | 464 | 40 | 504 |
| 12 | B2 receiver typed by the return type of the previous call (chains, `let x = a.m()`, call results) | 3,819 | 182 | 4,001 |
| 13 | B1 receiver bound by a pattern or closure param (`for x in ..`, `if let Some(x)`, `|x|`) | 2,598 | 178 | 2,776 |
| 14 | D3 method on a typed param, other wrapper or generic | 613 | 98 | 711 |
| 15 | M3 macro arg inside a proc-macro or external macro (`bsn!` 1,191, `bsn_list!` 75, approx crates, `params!`, `json!`) | 1,460 | 113 | 1,573 |
| 16 | X0 truth callee has no graph node | 33 | 253 | 286 |
| - | R residue: method call with another receiver shape (index, `other`, `expr.field`) | 182 | 4 | 186 |
| | **total** | **22,927** | **7,133** | |

The 16 classes cover 99.2% of bevy's uncredited sites (22,745 of 22,927) and 99.9% of pm's (7,129 of 7,133). The rest is in R.

Cross-cutting flag (counted inside other classes, not a class of its own): on bevy, 952 sites, and on pm, 22, sit in callers that already link to a *different* node with the callee's name. Per class: P1 285, M2 129, B1 89, P2 89, B3 81, M1 81, B2 73, D1 48. The writer should check these before widening a rule, because they are where a new rule could add a duplicate or wrong link.

Cross-cutting owner (bevy): 1,723 method sites, 154 path sites and 101 macro sites target the inherent methods of `bevy_platform::collections::HashMap`, and another 540 target `HashSet`. These are plain `impl<K,V,S> HashMap<K,V,S>` blocks in crates/bevy_platform/src/collections/hash_map.rs, yet the stop reason is mostly `method_not_found`. My hypothesis is that the resolver withholds because the name `HashMap` collides with std's `HashMap`. I have not verified this. It is the first thing to check, because one fix there lifts B3, D1, D3 and B4 together.

## Ranking justification and proof rules

1. **M2 corpus macro_rules args** (bevy 805, pm 4,760). Ranked first because one mechanism covers 67% of pm's gap: `check_eq!` 2,748, `check!` 1,844, `invoke!` 136, `check_ne!` 31, plus bevy `children!` 771.
   - Stop reason: `call:macro_call_not_proven` together with `reference:macro_invocation_not_proven` on almost all sites.
   - Proof rule: the macro's corpus `macro_rules!` arm matches the invocation's token tree, and the argument tokens bound to `$x:expr` fragments are parsed as an expression and resolved in the caller's scope (macro_rules hygiene keeps call-site names in call-site scope). For example, pm crates/check/src/lib.rs:26 binds `$left:expr, $right:expr`. The invoked name must resolve uniquely to that corpus macro through `#[macro_export]`, a `use` or textual scope.
   - Derivable with a bounded assumption: arm selection follows first-match over the token tree, and only `expr`, `ident` and `ty` fragments are used. Withhold when any arm is `tt`-recursive over the argument.
2. **M1 std expression macros** (bevy 2,204, pm 267). Second because it is the cheapest rule: a closed name list, and the arguments are comma-separated expressions (format macros skip the leading literal).
   - Stop reason: `macro_call_not_proven`, or `unknown_receiver_type` plus `macro_invocation_not_proven` once the argument is parsed.
   - Proof rule: when `assert_eq!`, `assert!`, `format!`, `vec!`, `matches!` and the like are not shadowed by a corpus `macro_rules!` or a `use` of the same name, their arguments are ordinary expressions at the call site.
   - Derivable with a bounded assumption: the name means the prelude macro unless the corpus shadows it, which the corpus can check. `matches!` needs its pattern argument skipped.
3. **D1 Deref of a corpus wrapper param** (bevy 3,273: Res 1,607, ResMut 1,311, Local 146, Extract 113, Single 47). One rule, about 14% of bevy's gap.
   - Stop reason: `method_not_found` (3,086).
   - Proof rule: when receiver type R has no inherent method m and no in-scope corpus trait method m, and the corpus has `impl Deref for R { type Target = T }`, look m up on T. The Res and ResMut impls come from the corpus macro `change_detection_impl!(Res<'w, T>, T, ...)` at crates/bevy_ecs/src/change_detection/params.rs:524 and 569, so that macro's impl must be expanded. Extract's impl is a plain one at crates/bevy_render/src/extract_param.rs:120.
   - Derivable with a bounded assumption: no std or dependency trait in scope gives R a method named m. That cannot be seen without std source, so the rule withholds when m matches a std-trait method name list.
4. **T1 blanket trait on fn items and tuples** (bevy 1,449). One trait family, and the method names are bevy-specific.
   - Stop reason: `unknown_receiver_type` (1,369).
   - Proof rule: when the receiver is a path to a corpus fn item, or a tuple of them, and exactly one in-scope corpus trait declares m and has a blanket `impl<..> Trait for F` or `all_tuples!`-generated tuple impls, link to that trait's declaration (truth target `IntoScheduleConfigs#chain()`).
   - Derivable with a bounded assumption: m is unique among the in-scope traits, so the impl's where-clause need not be proven.
5. **B3 field access** (bevy 2,490, pm 227). The field types are declared in the corpus. Most of the gain probably depends on the HashMap question above.
   - Stop reason: `unknown_receiver_type` 1,083 and `method_not_found` 1,035 (bevy); `method_not_found` 143 and `trait_scope_identity` 37 (pm).
   - Proof rule: the type of `base.f` comes from the struct declaration of base's proven type, with generics substituted from that type.
   - Derivable. Withhold when the field type is a generic parameter that the receiver does not fix.
6. **B4 locally explicit type** (bevy 1,106, pm 509). The type of the local is already known.
   - Stop reasons on pm: `trait_receiver_shape` 218, `trait_scope_identity` 166. Bevy is mostly `unknown_receiver_type` and `method_not_found`.
   - On pm the failing step is trait-method lookup on a known type. Example: `CanonicalProjectStore<A>` methods of the `ReqStore` and `TaskStore` traits, implemented in submodules such as port/canonical_store/decision_facade.rs.
   - Proof rule: for a receiver of known corpus type R, collect every corpus `impl Trait for R<..>` whose trait is in scope at the call site or is the only trait declaring m. If exactly one matches, link to it.
   - Derivable. The scope test uses the corpus's `use` items.
7. **P1 path call to a trait method** (bevy 1,264, pm 295).
   - Stop reasons: `function_not_in_registry` 590, `bound_declaration_not_proven` 127, `default_impl_not_proven` 84, `explicit_trait_impl_not_proven` (bevy).
   - It splits in two:
     - `Type::from(x)` and `Type::default()`, where the impl is in the corpus but the target impl must be chosen by argument type.
     - `T::m` on a generic, which links to the bound trait's declaration.
   - Derivable with a bounded assumption for `Trait::m` and `T::m`: target the trait declaration when exactly one bound declares m.
   - `From::from` overloads need the argument's type. Treat them as B2-hard and withhold when there is more than one corpus `impl From<_> for Type`.
8. **P3 cfg-dependent name** (bevy 459, all `cfg_dependent_name`). Truth picks `ops/std_ops/sin` and so on under the default features.
   - Derivable with a bounded assumption, named: the build uses default Cargo features. When the two cfg arms are `feature = "std"` and `not(feature = "std")`, and the default feature set in Cargo.toml (in the corpus) enables std, choose that arm.
   - Small and mechanical, but it carries a configuration assumption, so it ranks below the rules above.
9. **D2 Query methods** (bevy 463, almost all `no_resolution`, 440). The `Query<'w,'s,D,F>` inherent methods are in the corpus (crates/bevy_ecs/src/system/query.rs:678).
   - The `no_resolution` stop suggests the receiver path is never attempted. Cheap if this is a gap in shape handling.
   - Derivable: a param with a declared type means inherent lookup on that type.
10. **B5 const or static receiver** (bevy 245, pm 207, all `unknown_receiver_type`).
    - Proof rule: the declared type of `static`/`const NAME: T` is in the corpus (pm `REQ_DESCRIPTOR: EntityDescriptor`). A unit struct used as a value has type Self.
    - Derivable. The method itself is often a trait method (pm `EntityCliProjection::raw_cli_schema`), so this needs B4's trait lookup too.
11. **P2 free or inherent path** (bevy 464, pm 40).
    - Stop reason: `function_not_in_registry` 359.
    - Example: `Cuboid::from_size` in examples, where the type is re-exported through `bevy::prelude::*`.
    - Proof rule: follow the corpus `pub use` chain from the glob or prelude import to the defining item.
    - Derivable: re-export chains are corpus source. Withhold on glob ambiguity.
12. **B2 previous-call return type** (bevy 3,819, pm 182). Large, but every step depends on a resolved previous call plus generic substitution in its return type (`Option<&T>`, `Self`, associated types).
    - Stop reason: `unknown_receiver_type` 2,395, `method_not_found` 969.
    - Derivable with a bounded assumption when the previous callee is a corpus fn and its declared return type is a nominal corpus type, `Self`, or a generic fixed by the receiver.
    - Not derivable when it goes through std (`.unwrap()` on `Option<T>`) unless a closed built-in table for Option and Result is accepted. Name that table as the assumption.
13. **B1 pattern and closure bindings** (bevy 2,598, pm 178, mostly `unknown_receiver_type`).
    - Proof rule: the element type of `for x in iter`, `if let Some(x) = e` and `|x|` closures needs std `Iterator::Item`, `Option` and `IntoIterator` knowledge. For `Query` iteration it needs `QueryData::Item` (associated-type projection).
    - Not derivable from corpus alone, except for the bounded `Option<T>` and `Result<T,_>` built-in table and corpus `IntoIterator` impls. High work, low certainty.
14. **D3 other typed params** (bevy 613, pm 98). Mixed:
    - generic `dyn` and trait receivers;
    - `Gizmos` (Deref to `GizmoBuffer`, same rule as D1);
    - pm `trait_receiver_shape` 82 on `&mut impl Trait` and generic params.
    - Partly covered by D1 and B4. The remainder needs bound-trait dispatch, which is derivable when exactly one bound declares m.
15. **M3 proc-macro and external macro args** (bevy 1,460, of which `bsn!` and `bsn_list!` are 1,266; pm 113).
    - Stop reason: `macro_call_not_proven`.
    - Not derivable: the `bsn!` grammar is defined by a corpus proc macro (crates/bevy_scene/macros/src/bsn). Treating `ident(args)` tokens inside it as calls is an unproven assumption.
    - `params!` and `json!` are external macros whose arguments are expressions. They would need the same bounded name-list assumption as M1.
16. **X0 callee node absent** (bevy 33, pm 253). Graph extraction, not resolution.
    - pm example: `impl IssueStoreAdapter for InMemoryDecisionStore` methods at crates/pm-fakes/src/decision_store/issue.rs:98 have no node. They are probably generated inside a macro body, and the recorded stop is `method_not_found`.
    - Bevy example: `DebugName::owned` at crates/bevy_utils/src/debug_info.rs:68 is a cfg-gated item, stopped with `associated_item_not_registered`.
    - Derivable for corpus macro_rules bodies (same machinery as M2). The cfg-gated cases need P3's default-feature assumption.

R residue (bevy 182, pm 4) is index receivers (`images[i].clone()`), truth shape `other` and `expr.field`, mostly `unknown_receiver_type`.

## Three sites per class (random, seed 11; format: corpus `file:line` `call text` -> `truth target` (callee def), stop reasons recorded)
#### B1  reasons (top3 of 2776): 2396 call:unknown_receiver_type; 208 call:method_not_found; 76 call:trait_scope_identity
- bevy `crates/bevy_gizmos/src/transform_gizmo.rs:505` `let Ok(ray) = camera.viewport_to_world(cam_tf, cursor_pos) else {` -> `camera/impl#[Camera]viewport_to_world().` (crates/bevy_camera/src/camera.rs:647); stop: call:unknown_receiver_type
- bevy `crates/bevy_ecs/src/reflect/bundle.rs:231` `.take::<B>()` -> `world/entity_access/world_mut/impl#[EntityWorldMut<'w>]take().` (crates/bevy_ecs/src/world/entity_access/world_mut.rs:1279); stop: call:unknown_receiver_type
- bevy `crates/bevy_scene/src/scene.rs:253` `scene_list.register_dependencies(dependencies);` -> `scene_list/SceneList#register_dependencies().` (crates/bevy_scene/src/scene_list.rs:25); stop: call:unknown_receiver_type
#### B2  reasons (top3 of 4001): 2559 call:unknown_receiver_type; 1030 call:method_not_found; 158 call:trait_receiver_shape
- bevy `crates/bevy_world_serialization/src/dynamic_world.rs:322` `.register::<ChildOf>();` -> `type_registry/impl#[TypeRegistry]register().` (crates/bevy_reflect/src/type_registry.rs:202); stop: call:unknown_receiver_type
- bevy `crates/bevy_scene/src/lib.rs:1273` `let name = a.get::<Name>().unwrap();` -> `world/entity_access/entity_ref/impl#[EntityRef<'w>]get().` (crates/bevy_ecs/src/world/entity_access/entity_ref.rs:122); stop: call:method_not_found
- bevy `crates/bevy_ecs/src/bundle/spawner.rs:196` `components: bundle_info.contributed_components(),` -> `bundle/info/impl#[BundleInfo]contributed_components().` (crates/bevy_ecs/src/bundle/info.rs:188); stop: call:unknown_receiver_type
#### B3  reasons (top3 of 2717): 1214 call:method_not_found; 1155 call:unknown_receiver_type; 98 call:trait_impl_key_unreadable
- bevy `crates/bevy_tasks/src/iter/adapters.rs:103` `self.iter.next_batch().map(|b| b.flat_map(self.f.clone()))` -> `iter/ParallelIterator#next_batch().` (crates/bevy_tasks/src/iter/mod.rs:24); stop: call:method_not_found
- bevy `crates/bevy_pbr/src/render/light.rs:1736` `.intersects(view_layers)` -> `visibility/render_layers/impl#[RenderLayers]intersects().` (crates/bevy_camera/src/visibility/render_layers.rs:121); stop: call:unknown_receiver_type
- bevy `crates/bevy_pbr/src/render/light.rs:545` `.retain(|view_entity, _| {` -> `collections/hash_map/impl#[HashMap<K, V, S>]retain().` (crates/bevy_platform/src/collections/hash_map.rs:657); stop: call:method_not_found
#### B4  reasons (top3 of 1615): 553 call:unknown_receiver_type; 456 call:method_not_found; 313 call:trait_receiver_shape
- bevy `crates/bevy_reflect/src/serde/de/tuple_structs.rs:66` `let value = reflect_deserializer.deserialize(deserializer)?;` -> `serde/de/deserializer/impl#[TypedReflectDeserializer<'_, P>][DeserializeSeed<'de>]dese` (crates/bevy_reflect/src/serde/de/deserializer.rs:357); stop: call:trait_receiver_shape
- bevy `crates/bevy_ecs/src/bundle/impls.rs:39` `unsafe { ptr.read() }` -> `impl#[OwningPtr<'a, A>]read().` (crates/bevy_ptr/src/lib.rs:1021); stop: call:bound_declaration_not_proven
- pm `crates/pm-adr-usecase/src/usecase/adr/tests.rs:789` `let fetched = store.get_adr(&uuid).unwrap().unwrap();` -> `port/scoped_mutation/adr/impl#[T][AdrEntityCapabilities]get_adr().` (crates/pm-port/src/port/scoped_mutation/adr.rs:44); stop: call:trait_scope_identity
#### B5  reasons (top3 of 452): 423 call:unknown_receiver_type; 27 call:method_not_found; 2 call:no_resolution
- pm `crates/pm-cli-commands/src/commands/issue/action.rs:96` `.raw_cli_schema()` -> `entity_projection/impl#[EntityDescriptor][EntityCliProjection]raw_cli_schema().` (crates/pm-cli-schema/src/entity_projection.rs:52); stop: call:unknown_receiver_type
- pm `crates/pm-domain/src/state/reason_lifecycle/tests.rs:129` `let r = ReasonStatus::Accepted.transition(ReasonStatus::Deprecated, &agent());` -> `state/reason_lifecycle/impl#[ReasonStatus]transition().` (crates/pm-domain/src/state/reason_lifecycle.rs:88); stop: call:unknown_receiver_type
- bevy `examples/3d/pccm.rs:63` `DefaultPlugins.set(WindowPlugin {` -> `plugin_group/PluginGroup#set().` (crates/bevy_app/src/plugin_group.rs:261); stop: call:unknown_receiver_type
#### D1  reasons (top3 of 3273): 3133 call:method_not_found; 76 call:no_resolution; 44 call:trait_scope_identity
- bevy `examples/ui/text/text_debug.rs:34` `let font = FontSource::from(asset_server.load("fonts/FiraSans-Bold.ttf"));` -> `server/impl#[AssetServer]load().` (crates/bevy_asset/src/server/mod.rs:364); stop: call:method_not_found
- bevy `examples/picking/custom_hit_data.rs:104` `let (config, _) = config_store.config_mut::<DefaultGizmoConfigGroup>();` -> `config/impl#[GizmoConfigStore]config_mut().` (crates/bevy_gizmos/src/config.rs:157); stop: call:method_not_found
- bevy `examples/stress_tests/many_lights.rs:138` `camera_transform.rotate_x(delta);` -> `components/transform/impl#[Transform]rotate_x().` (crates/bevy_transform/src/components/transform.rs:373); stop: call:method_not_found
#### D2  reasons (top3 of 463): 440 call:no_resolution; 9 call:method_not_found; 5 call:unknown_receiver_type
- bevy `examples/ui/layout/size_constraints.rs:286` `previous_query.iter()` -> `system/query/impl#[Query<'w, 's, D, F>]iter().` (crates/bevy_ecs/src/system/query.rs:678); stop: call:no_resolution
- bevy `examples/3d/tonemapping.rs:281` `for (mut visibility, scene) in query.iter_mut() {` -> `system/query/impl#[Query<'w, 's, D, F>]iter_mut().` (crates/bevy_ecs/src/system/query.rs:716); stop: call:no_resolution
- bevy `examples/3d/anisotropy.rs:241` `for light in lights.iter() {` -> `system/query/impl#[Query<'w, 's, D, F>]iter().` (crates/bevy_ecs/src/system/query.rs:678); stop: call:no_resolution
#### D3  reasons (top3 of 711): 481 call:method_not_found; 87 call:trait_receiver_shape; 36 call:no_resolution
- pm `crates/pm-graph/src/model/edge_insertion.rs:217` `edge_kind: edge.kind_name(),` -> `vocabulary/predicate/impl#[Predicate]kind_name().` (crates/pm-domain-vocab/src/vocabulary/predicate.rs:130); stop: call:method_not_found
- pm `crates/pm-git-sync/tests/transport_import_round_trip.rs:2654` `.add_goal_non_goal(&goal_b, "archival retention policy")` -> `port/canonical_store/decision_facade/impl#[CanonicalProjectStore<A>][NonGoalStore]add_go` (crates/pm-port-store/src/port/canonical_store/decision_facade.rs:94); stop: call:trait_receiver_shape
- bevy `examples/tools/scene_viewer/scene_viewer_plugin.rs:113` `.load_state(&scene_handle.gltf_handle)` -> `server/impl#[AssetServer]load_state().` (crates/bevy_asset/src/server/mod.rs:1297); stop: call:method_not_found
#### M1  reasons (top3 of 2471): 765 call:unknown_receiver_type|reference:macro_invocation_not_proven; 367 call:method_not_found; 300 call:macro_call_not_proven|reference:macro_invocation_not_proven
- pm `crates/pm-graph-query/src/query/lineage/tests.rs:329` `lineage_context(&non_req, &StepId::from(step_uuid)),` -> `query/lineage/lineage_context().` (crates/pm-graph-query/src/query/lineage.rs:21); stop: call:macro_call_not_proven|reference:macro_invocation_not_proven
- bevy `crates/bevy_ecs/src/hierarchy.rs:882` `world.entity(child).get::<ChildOf>().unwrap(),` -> `world/entity_access/entity_ref/impl#[EntityRef<'w>]get().` (crates/bevy_ecs/src/world/entity_access/entity_ref.rs:122); stop: call:method_not_found
- bevy `crates/bevy_reflect/src/lib.rs:2744` `assert!(info.item_info().unwrap().is::<usize>());` -> `list/impl#[ListInfo]item_info().` (crates/bevy_reflect/src/list.rs:164); stop: call:unknown_receiver_type|reference:macro_invocation_not_proven
#### M2  reasons (top3 of 5565): 4788 call:macro_call_not_proven|reference:macro_invocation_not_proven; 643 call:macro_call_not_proven; 79 NONE
- pm `crates/pm-domain-vocab/src/vocabulary/predicate/tests.rs:191` `Predicate::DependsOn.help(&[EntityKind::Step]),` -> `vocabulary/predicate/impl#[Predicate]help().` (crates/pm-domain-vocab/src/vocabulary/predicate.rs:240); stop: call:macro_call_not_proven|reference:macro_invocation_not_proven
- pm `crates/pm-graph-walk/src/tests/depth_first.rs:8` `bounded_dfs(&graph, node_a, Depth::Unbounded),` -> `bounded_dfs().` (crates/pm-graph-walk/src/lib.rs:108); stop: call:macro_call_not_proven|reference:macro_invocation_not_proven
- pm `crates/pm-domain/src/allocation/tests/claim_sets.rs:56` `set.first_free_lane().unwrap(),` -> `allocation/claim_set/impl#[LaneClaimSet]first_free_lane().` (crates/pm-domain/src/allocation/claim_set.rs:166); stop: call:macro_call_not_proven|reference:macro_invocation_not_proven
#### M3  reasons (top3 of 1573): 1210 call:macro_call_not_proven|reference:macro_invocation_not_proven; 212 call:macro_call_not_proven; 51 NONE
- bevy `examples/ui/widgets/feathers_number_input.rs:179` `commands.entity(value_change.event_target())` -> `system/commands/impl#[Commands<'w, 's>]entity().` (crates/bevy_ecs/src/system/commands/mod.rs:308); stop: call:macro_call_not_proven|reference:macro_invocation_not_proven
- bevy `crates/bevy_feathers/src/containers/group.rs:60` `row_gap: px(4),` -> `geometry/px().` (crates/bevy_ui/src/geometry.rs:541); stop: call:macro_call_not_proven|reference:macro_invocation_not_proven
- bevy `examples/large_scenes/bevy_city/src/settings.rs:81` `on(checkbox_self_update)` -> `checkbox/checkbox_self_update().` (crates/bevy_ui_widgets/src/checkbox.rs:278); stop: reference:macro_invocation_not_proven
#### P1  reasons (top3 of 1559): 842 call:function_not_in_registry; 238 call:strategy_closed; 129 call:bound_declaration_not_proven
- bevy `crates/bevy_ecs/src/observer/mod.rs:1181` `bevy_ptr::PtrMut::from(&mut event_data),` -> `impl#[PtrMut<'a>][From<&'a mut T>]from().` (crates/bevy_ptr/src/lib.rs:967); stop: call:function_not_in_registry
- bevy `examples/ui/scroll_and_overflow/scrollbars.rs:39` `Children::spawn((Spawn(Text::new("Scrolling")), Spawn(scroll_area_demo()))),` -> `spawn/impl#[T][SpawnRelated]spawn().` (crates/bevy_ecs/src/spawn.rs:443); stop: call:associated_item_not_registered
- bevy `examples/shader/shader_material_screenspace_texture.rs:35` `Mesh3d(meshes.add(Cuboid::default())),` -> `primitives/dim3/impl#[Cuboid][Default]default().` (crates/bevy_math/src/primitives/dim3.rs:700); stop: call:function_not_in_registry
#### P2  reasons (top3 of 504): 478 call:function_not_in_registry; 12 call:module_function_not_registered; 7 call:unresolved_glob_import
- bevy `crates/bevy_platform/tests/aligned_vec.rs:104` `let v = AlignedVec::new(16);` -> `collections/aligned_vec/impl#[AlignedVec]new().` (crates/bevy_platform/src/collections/aligned_vec.rs:76); stop: call:function_not_in_registry
- bevy `examples/testbed/2d.rs:607` `Mesh2d(meshes.add(Rectangle::new(STRIP_WIDTH, STRIP_HEIGHT))),` -> `primitives/dim2/impl#[Rectangle]new().` (crates/bevy_math/src/primitives/dim2.rs:1828); stop: call:function_not_in_registry
- bevy `crates/bevy_ecs/src/schedule/graph/dag.rs:532` `HashMap::with_capacity_and_hasher(capacity, Default::default());` -> `collections/hash_map/impl#[HashMap<K, V, S>]with_capacity_and_hasher().` (crates/bevy_platform/src/collections/hash_map.rs:391); stop: call:function_not_in_registry
#### P3  reasons (top3 of 459): 459 call:cfg_dependent_name
- bevy `examples/shader_advanced/fullscreen_material.rs:83` `let mut intensity = ops::sin(phase);` -> `ops/std_ops/sin().` (crates/bevy_math/src/ops.rs:98); stop: call:cfg_dependent_name
- bevy `crates/bevy_color/src/okcolor_convert.rs:320` `let C = ops::sqrt(lab_a * lab_a + lab_b * lab_b);` -> `ops/std_ops_for_no_std/sqrt().` (crates/bevy_math/src/ops.rs:562); stop: call:cfg_dependent_name
- bevy `examples/3d/transmission.rs:601` `let b = ops::cos(s * 5.0) * 0.0125 + ops::cos(s * 3.0) * 0.025;` -> `ops/std_ops/cos().` (crates/bevy_math/src/ops.rs:106); stop: call:cfg_dependent_name
#### R   reasons (top3 of 186): 139 call:unknown_receiver_type; 34 call:method_not_found; 9 call:trait_receiver_shape
- bevy `crates/bevy_ecs/src/system/builder.rs:1367` `.build_state(&mut world)` -> `system/builder/SystemParamBuilder#build_state().` (crates/bevy_ecs/src/system/builder.rs:139); stop: call:unknown_receiver_type
- bevy `crates/bevy_asset/src/lib.rs:3330` `let new_def_handle = handles[1].clone();` -> `handle/impl#[Handle<T>][Clone]clone().` (crates/bevy_asset/src/handle.rs:179); stop: call:unknown_receiver_type
- bevy `crates/bevy_ecs/src/system/builder.rs:1283` `.build_system(|param: CustomParam| *param.local + param.query.iter().count());` -> `system/builder/SystemParamBuilder#build_system().` (crates/bevy_ecs/src/system/builder.rs:158); stop: call:method_not_found
#### T1  reasons (top3 of 1449): 1369 call:unknown_receiver_type; 67 call:method_not_found; 10 call:method_not_found|call:unknown_receiver_type
- bevy `crates/bevy_ecs/src/schedule/mod.rs:328` `.chain()` -> `schedule/config/IntoScheduleConfigs#chain().` (crates/bevy_ecs/src/schedule/config.rs:565); stop: call:unknown_receiver_type
- bevy `crates/bevy_dev_tools/src/render_debug.rs:96` `.before(ui_pass)` -> `schedule/config/impl#[ScheduleConfigs<T>][IntoScheduleConfigs<T, ()>]before().` (crates/bevy_ecs/src/schedule/config.rs:626); stop: call:unknown_receiver_type
- bevy `crates/bevy_light/src/lib.rs:189` `.after(TransformSystems::Propagate)` -> `schedule/config/impl#[ScheduleConfigs<T>][IntoScheduleConfigs<T, ()>]after().` (crates/bevy_ecs/src/schedule/config.rs:632); stop: call:unknown_receiver_type
#### X0  reasons (top3 of 286): 218 call:method_not_found; 22 call:trait_receiver_partial; 11 call:unknown_receiver_type
- pm `crates/pm-fakes-usecase/src/scoped_mutation_adapter/store_adapters.rs:309` `self.decision.get_goal(id)` -> `decision_store/goal/impl#[InMemoryDecisionStore][GoalStoreAdapter]get_goal().` (crates/pm-fakes/src/decision_store/goal.rs:39); stop: call:method_not_found
- pm `crates/pm-fakes-usecase/src/scoped_mutation_adapter/intent_scope.rs:82` `self.adapter.decision.list_req_quality_attributes(req_id)` -> `decision_store/req/impl#[InMemoryDecisionStore][ReqStoreAdapter]list_req_quality_attribute` (crates/pm-fakes/src/decision_store/req.rs:393); stop: call:method_not_found
- pm `crates/pm-fakes/src/decision_store_tests.rs:799` `store.create_issue(make_issue_input(subject, None)).unwrap();` -> `decision_store/issue/impl#[InMemoryDecisionStore][IssueStoreAdapter]create_issue().` (crates/pm-fakes/src/decision_store/issue.rs:98); stop: call:method_not_found

## Flagged pairs (wrong-or-unconfirmed) on all four corpora: 110 total
Source: `<bench>/rust-base/<c>/wrong_pairs.tsv` and precision.txt (`awk -F'\t' '{print $1"\t"$2}' wrong_pairs.tsv | sort | uniq -c`).

| corpus | count | strategy | finding |
|---|---|---|---|
| codex | 1 | lsp_module_path | NOT IN TRUTH: core/src/windows_sandbox.rs:381 `emit_windows_sandbox_setup_failure_metrics` -> windows-sandbox-rs/src/setup_error.rs:155 `extract_failure`. Windows-only code, so truth from a Linux rust-analyzer run cannot see it. Probably a platform oracle gap, not a resolver error. Rule: keep, but tag such pairs as cfg(windows)-unverifiable. |
| ruff | 1 | lsp_bound_dispatch | WRONG TARGET: crates/ruff_formatter/src/buffer.rs:661 `new` -> buffer.rs:39 `elements`. The caller calls a different `elements`. Bound dispatch picked one impl where the bound allows several. Rule: link bound dispatch to the trait declaration, or withhold when more than one impl could apply. |
| pm | 72 | lsp_constructor | WRONG TARGET (scorer): crates/pm-projection/src/graph/query/tests.rs:226 `CanonicalProjectStore::new(InMemoryDecisionStore::new())` -> crates/pm-fakes/src/decision_store.rs:82 `new` (36), and the same with execution_store.rs:88 (36). For the inner call, truth.tsv has only `path_unresolved` at lines 226 and 227, so rust-analyzer did not resolve the pm-fakes dev-dependency. The graph link matches the written path `InMemoryDecisionStore::new` and is very likely correct, and the WRONG label comes from the oracle. Verify by hand before changing the resolver. |
| pm | 1 | lsp_trait_dispatch | WRONG TARGET: tests.rs:4607 `public_projection_loads_task_addresses_edges_for_graph_walks` -> port/canonical_store/execution_facade.rs:84 `add_task_address`. The trait has more than one impl and dispatch chose the facade impl. Rule: withhold, or target the trait declaration, unless the receiver type is proven. |
| bevy | 31 | lsp_ufcs | WRONG TARGET, a self-loop. Example: crates/bevy_gltf/src/loader/extensions/mod.rs:406 `on_root` -> mod.rs:406 `on_root`, and the same for 11 bevy_gltf extension hooks and 20 bevy_asset/src/io/mod.rs erased-writer and erased-reader methods (`Box::pin(Self::rename(self, ..))` at io/mod.rs:515). Inside `impl<T: Trait> ErasedTrait for T`, `Self::m` means `Trait::m` (the bound), not the enclosing method. This is a real resolver error. Rule: in a blanket impl over a type param, resolve `Self::m` through the param's bounds and never to the enclosing impl's own item. Withhold if several bounds declare m. |
| bevy | 1 | lsp_trait_dispatch | WRONG TARGET: crates/bevy_asset/src/io/web.rs:246 `try_load_from_cache` -> io/file/sync_file_asset.rs:44 `read_to_end`. A trait method dispatched to one arbitrary impl. Same rule as the pm trait_dispatch case. |
| bevy | 3 | lsp_method_dispatch | NOT IN TRUTH: examples/3d/solari_reflections.rs:660 `toggle_pathtracer` -> bevy_ecs/system/commands/mod.rs:1654 `remove`, and crates/bevy_image/src/image.rs:232 `build` -> bevy_asset/processor/mod.rs:747 `register_processor` and :781 `set_default_processor`. The callers sit in cfg- or feature-gated code (image.rs:232 is in a plugin build under features). Probably feature-gated sites the oracle did not compile. Confirm by checking the cfg on those lines. |

Net: the bevy UFCS self-loop (31) and the ruff, pm and bevy single-impl dispatch picks (3) are real wrong links to fix first. The pm constructor flags (72), the codex flag and the 3 bevy NOT-IN-TRUTH flags look like oracle gaps.

## Scratch artefacts
/scratch/agent/tmp/rs-inv/{miss.py,cls.py,bevy.miss.tsv,pm.miss.tsv,bevy.cls.tsv,pm.cls.tsv,bevy.mr.txt,pm.mr.txt,samples.md}. Column 1 of `*.cls.tsv` is the class. Columns 2 to 14 are file, line, scope, shape, wrapper, callee form, name, SCIP symbol, def file:line, status, other-target flag, stop reasons and source text. Column 15 is the macro name or param type.
