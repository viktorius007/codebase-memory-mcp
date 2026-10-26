STATUS: DONE (bevy +27 only; the G002/G003/G009 residue is a long tail with no single corpus-provable mechanism above about 40 sites; 75,631 of the 75,866 needed)
COMMIT: 449ab010 (lane/w7-bind, one commit on main 38074ddf)
CHANGED: internal/cbm/lsp/rust_lsp.c (std family), internal/cbm/lsp/rust_std_methods.h; fixture tests/fixtures/graph/rust/nonnullref
CLOSED: 27 bevy sites (G009 let-over-NonNull and its G002/G003 cascades); 0 lost
OPEN (re-cut on main 38074ddf, with the exact mechanism; the counts are uncredited bevy sites whose binding line stops there):
- `x.iter()` on an unknown receiver (70): upstream cascades, mostly bindings under ResMut or Res parameters whose items come from macro-generated Deref (change_detection_impl!), and text_layout_info loops in bevy_ui_render.
- `Foo::type_info()` from `#[derive(Reflect)]` (47 binding cascades): the derived Typed impl has no source, so associated_item_not_registered. The truth does not list the call itself. Typing it needs a derive table row for Reflect whose return is the corpus Typed declaration's `&'static TypeInfo`. This is bevy-specific; not done.
- index_output_not_proven (32): Vec/slice range indexing (`&mut conditions[range]`), `labels[i]` over collected Vecs, and `ui_phase.items[i]` with a generic item parameter; split across several small mechanisms.
- `curve.by_ref()` (31): prelude_trait_method_not_proven on FunctionCurve. A probe crate resolves Curve::by_ref; in bevy the blanket `impl<T, C, D: Deref<Target = C>> Curve<T> for D` keeps the default method unproven. Needs a blanket-impl applicability proof (Deref bound not satisfied by FunctionCurve).
- ComputeTaskPool/IoTaskPool::get (23) and TaskPool::scope (18 method_not_found on the crate-root placeholder): the taskpool! macro and `pub use task_pool::{..}` inside the cfg::multi_threaded! branches. The wave-5 rule mounts the `mod`s; the branch `use`s name the same items from both branches, so reading them needs the feature selection.
- `debug_checked_unwrap()` (39 miss lines): dispatch resolves (56 rows), but its return `Self::Item` comes from `impl<T> DebugCheckedUnwrap for Option<T> { type Item = T; }`. The module tree marks every generic impl's associated type unreadable. Reading it needs the modtree to record impls whose self arguments are exactly their parameters, plus positional substitution in rust_project_impl_assoc. That is a multi-file change; not done in this window.
- Closures passed as fn items (`add.into_function()`, `get_function_info`, `func.call`: 57): fn-item and closure receivers through blanket IntoFunction impls (the G013 fn-item group).
- QueryBuilder `&mut Self` chains: `data::<X>()` returns `&mut Self`, which the first pass reads as head `mut`. The cross pass resolves build(); the remaining builder.rs sites stop on FilteredEntityMut items via `single_mut`, which the w5 entity-access rule now covers where the state type is known.
BLOCKERS: none
SCORES: bevy 75631/84295 f9; pm 45237/47116 f87; codex 226452/229953 f1; ruff 104704/112300 f1
---
Main 38074ddf (own build): bevy 75,604 f9, pm 45,237 f87, codex 226,452 f1, ruff 104,704 f1. Tip: bevy +27, the others equal. No CALLS removed on any corpus (bevy +18 CALLS edges; pm, ruff and codex identical edge sets).

## Commit 449ab010
NonNull<T>::as_ref -> &T and as_mut -> &mut T as std-table rows for a new family, RUST_STD_NONNULL (core.ptr.NonNull, core.ptr.non_null.NonNull, std.ptr.NonNull). It closes `let table = unsafe { self.table.as_mut() }` bindings in bevy_ecs bundle/spawner/insert code. Bevy 75,604 -> 75,631. Example: crates/bevy_ecs/src/bundle/insert.rs:203 -> archetype/impl#[Archetype]table_id(). Fixture nonnullref: right 2, main 0.

## Checks
- Build: no warnings.
- Fixtures: 97 x 2, 0 failures.
- rust_lsp: 840 passed, 0 failed.
- Scores: run one at a time under /scratch/agent/tmp/w7.

## Re-cut table (bevy uncredited sites on main 38074ddf; inventory scripts rerun on the main dump)
total 8691 groups 200
G001	1118	12.9	unres:macro_call_not_proven	macro_arg!bsn	-	macro=1118	held: - 1118
G002	747	21.5	unres:unknown_receiver_type	ident/local.field	pattern/closure binding	macro=85	held: - 676, Result 44, Item 22, QueryIter 2
G003	671	29.2	unres:unknown_receiver_type	cascade	upstream unknown_receiver_type	macro=284	held: - 671
G004	331	33.0	unres:unknown_receiver_type	ident/local.field	not-local (imported item)	macro=23	held: - 331
G005	321	36.7	unres:function_not_in_registry	path call	truth trait-impl	macro=10	held: - 321
G006	305	40.2	unres:method_not_found	ident/local.field	let = call/ctor (type known)	macro=136	held: World 57, Result 33, A 32, Box 25
G007	256	43.1	unres:unknown_receiver_type	cascade	upstream method_not_found	macro=89	held: - 256
G008	239	45.9	unres:unknown_receiver_type	cascade	upstream function_not_in_registry	macro=31	held: - 239
G009	224	48.5	unres:unknown_receiver_type	ident/local.field	let = other expr	macro=19	held: - 224
G010	223	51.0	unres:method_not_found	ident/local.field	fn-param	macro=5	held: Single 45, Gizmos 45, SourceAsset 33, TaskPool 20
G011	200	53.3	unres:method_not_found	call	inner-ok	macro=38	held: Output 108, Ref 17, PartialReflect 11, Mut 8
G012	178	55.4	unres:macro_call_not_proven	macro_arg!(non-bsn)	-	macro=178	held: - 178
G013	167	57.3	unres:unknown_receiver_type	ident/local.field	fn-item	macro=5	held: - 167
G014	163	59.2	unres:method_not_found	ident/local.field	pattern/closure binding	macro=4	held: RenderMeshQueueData 32, PartialReflect 20, Box 16, Mut 13
G015	158	61.0	unres:unknown_receiver_type	ident/local.field	field of pattern/closure binding	macro=29	held: - 150, MessageIterator 4, Result 2, Item 1
