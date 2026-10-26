STATUS: DONE. Four mechanisms landed; G042, G022, G033, G036, G037 and G050 were not reached (see OPEN)
COMMIT: 193061ec (lane/w5-path on main 05649859; review fixes on top of d439733f)
CHANGED: internal/cbm/lsp/rust_lsp.c; tests/test_rust_lsp.c (one test replaced, see 6ab0079c); fixtures tests/fixtures/graph/rust/{selfbound,bracketpath,boundpath}
CLOSED (uncredited bevy sites, wave-4 ids, measured on the pre-rebase chain): G018 173->71 (first cut; the final restriction gives back part of this, bevy -24); G026 87->84; G004 355->~318 (bracketed `<T>::f`); 31 flagged self-links removed (bevy flagged 36->5 before the +4 below)
OPEN: G042 `Default::default()`: the dump shows 780 of the default_impl_not_proven calls have no expected type at all (rust_expected_type_qn gives NULL), so this is expected-type flow (binding/field typing), not a path rule. Also open: G018 residue `D::init_fetch`/`F::filter_fetch` (QueryData/QueryFilter carry rust_nominal_identity_ambiguous because each is also a derive name); G022/G033/G036/G037/G050 not analysed; G004 `Type::from(arg)` impl selection; bitflags!-declared types
BLOCKERS: none. One ruling is needed: 6ab0079c reverses the tested rule "an unread bound withholds T::f" (see below)
SCORES: bevy 75498/84295 f9; pm 45169/47116 f87; ruff 104673/112300 f1; codex 226363/229953 f1 (tip d439733f, each corpus scored on its own). Main 05649859 was not scored here. Reference figures for main c3ceda65 come from w5-bind's measure: bevy 74,899 f36. Same-base comparison on 1b64143f: tip bevy 74728 f9 vs main 74536 f36; pm 45167 = main; ruff 104670 vs 104669; codex 226360 = main f1, with an identical flagged pair set
---
## 7e8fa45e: `Self::f` in a blanket impl over a bounded parameter names the bound's declaration
- Mechanism: inside `impl<T: AssetReader> ErasedAssetReader for T`, `Self::read(self, ..)` linked the blanket impl's own method, i.e. a self-call (lsp_ufcs). rust_impl_has_type_param does not recognise tree-sitter's `type_parameter` node, so `T` fell through to `<module>.T.read`.
- Rule: when the self type is exactly an impl type parameter and its bounds declare `name`, link that declaration (rustc's where-clause candidate wins over impls).
- Gain: bevy 74,536 -> 74,543, flagged 36 -> 5. The 31 removed pairs are all WRONG TARGET self-links (18 in bevy_asset io/mod.rs, 13 in bevy_gltf loader/extensions/mod.rs). Example: io/mod.rs:282 now links io/AssetReader#read().
- Fixture: selfbound. Main gives 3 WRONG; now the declaration is linked, which the fixture checker reports as withheld.
## 9e0f4b13: `<T>::f` and `<T<A>>::f` resolve as `T::f`
- Gain: bevy +103 (21 `<HashMap<_, _>>::default`, 12 `<HashSet<_>>::default`, plus chains). Flagged +4: at bevy_core_pipeline core_2d:423 and core_3d:763, pbr transmission/texture.rs:30 and render view/mod.rs:1310, rust-analyzer's own inference fails and its truth names core Default#default. The link is correct Rust, and the 21 identical sites rust-analyzer does infer are credited. No removed pairs.
- Fixture: bracketpath. Main gives FALSE-STOP on both sites; now 2 right.
## 6ab0079c: `T::f` links the bound declaring f even when another bound is unread
- Mechanism: rust_bound_method treats any unread bound or supertrait (std Send/Debug/Copy, Pod, "?" names) as a possible competing declaration.
- Rule: walk each read bound on its own. A declaration found is the target, because a second declaration would make `T::f` ambiguous (E0034).
- Gain: bevy +57, flagged unchanged, no removed pairs. Example: bevy_settings lib.rs:196 `T::settings_group_name()` now links SettingsGroup#settings_group_name().
- RULING NEEDED: the existing test rustlsp_generic_static_unknown_bound_withholds_declaration asserted the opposite rule. It is replaced by rustlsp_generic_static_unread_bound_links_declaration. Dropping this commit restores the old rule (and its test).
- Fixture: boundpath. Main withholds; now right.
## d439733f: `Trait::f(x)` with a generic x links the declaration through x's bound
- Mechanism: `IntoSystem::into_system(handler)` had three vetoes: `fn into_system(this: Self)` is not a `self` receiver, the supertrait Sized is unread, and the blanket `impl<T: System> IntoSystem for T` vetoed the link.
- Rule: a first parameter typed `Self` qualifies; an unread supertrait does not veto; and a type-parameter self bounded by the path's trait itself skips the blanket veto. Trait objects, and parameters bounded only by another trait, keep the veto. The first cut lacked that last restriction and gave codex +1 flagged at core/src/context/world_state/test_support.rs:80; it is fixed and covered by the fixture.
- Gain: bevy 74,703 -> 74,728 (+25), flagged unchanged. Example: bevy_remote lib.rs:625 links system/IntoSystem#into_system().
## Final tip checks
- Build: `make -f Makefile.cbm cbm BUILD_DIR=/scratch/agent/build/w5-path -j4` builds with no warnings.
- Fixtures: full target (FIXTURES_WORK under /scratch): 92 fixtures x 2 modes, 97 known, 0 failures.
- Suite: `scripts/test.sh --suites "rust_lsp" BUILD_DIR=build/t-w5-path` on a fresh dir: EXIT=0, 837 passed, 0 failed.
## 193061ec: review fixes (the coordinator's four items)
- 1: `<T>::f` with T an enclosing generic parameter never enters the module tree, and rust_ufcs_target compares the normalised spelling.
- 2 and 3: rust_bound_trait_declarations gained an `unread` out-flag. `ambiguous` (two read declarations, or a trait of unsure identity) now withholds in rust_bound_path_declaration and in the `Trait::f(x)` path.
- 4: `Tr::f(x)` with a reference argument withholds when a corpus impl of Tr for a reference self (core.reference bucket) defines f.
- Tests added: rustlsp_generic_static_two_supertrait_declarations_withhold, rustlsp_trait_path_reference_arg_with_reference_impl_withholds, rustlsp_bracketed_generic_param_shadows_module_struct. They assert that no admitted link exists. I did not check that they fail against d439733f (not run).
- Checks: build has no warnings; selfbound/bracketpath/boundpath fixtures give 0 failures; rust_lsp gives EXIT=0, 840 passed, 0 failed; bevy 75501/84295 f9. pm, ruff and codex were not rescored after the fixes.
