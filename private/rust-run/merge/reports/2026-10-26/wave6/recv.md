STATUS: DONE. Five mechanisms landed. G022 and the `Type::from(arg)` sites were measured but nothing landed for them (see OPEN)
COMMIT: 8d0b875e (lane/w6-recv rebased on main e1df45de, which merged lane/w5-path; review fixes on top of a30e2240)
CHANGED: internal/cbm/lsp/rust_lsp.c; fixtures tests/fixtures/graph/rust/{blanketpath,defaultpath,explicitimpl,variantlit}
CLOSED (uncredited bevy sites against my main build): G050 44->5; G037 59->39; G036 63->35; G033 68->61
OPEN:
- `Type::from(arg)`: when the argument type is known, the existing rust_trait_impl_by_argument already selects the impl. A debug run showed `LinearRgba::from(Color)` linked and credited. The remaining 188 G004 `from` sites stop for these reasons:
  - the argument is a const (`LinearRgba::from(ANTIQUE_WHITE)`, where the css palette consts are untyped);
  - the impl is a generic `impl<T: Into<Color>> From<T> for BorderColor`;
  - the argument has a generic return type (`FontSource::from(asset_server.load(..))`).
  These are argument/const typing, not impl selection.
- G022 (133, unknown_receiver_type on path/const receivers): not attempted; const typing is the same gap.
- Method-call form of the G050/G037 rules (`UiSystems::Prepare.after(..)`): tried, then dropped. The default-declaration rule on method calls gained +212 bevy but raised flagged 36->69: NOT IN TRUTH build_system and to_dynamic_struct links, and WRONG len/iter/build_template/and_then/sample_iter. Blanket-only gained nothing and added +3 flagged.
- G037 residue 39, G036 residue 35: not analysed.
BLOCKERS: none
SCORES: rebased tip 8d0b875e: bevy 75604/84295 f9 (one score, as asked). Before the rebase (b9c19ac2 on 05649859) each corpus was scored one at a time: bevy 75410 f36, pm 45237 f87, ruff 104703 f1, codex 226455 f1. Main 05649859 (my build), same method: bevy 75306 f36, pm 45169, ruff 104672, codex 226363. Flagged pair sets were identical on pm, ruff and codex
---
## ae4f9060: `Type::f` reached only through a blanket impl links that impl's method (G050)
- Mechanism: `Children::spawn(..)`: Children registers no `spawn`; it comes from `impl<T: RelationshipTarget> SpawnRelated for T`. A second in-scope trait, SpawnableList, also declares `spawn`, with impls on other heads.
- Rule: among the in-scope corpus traits declaring `name`, Type implements none itself, and exactly one has a blanket over a bare parameter `T` written beside the trait that defines `name`. Named assumptions: no dependency trait declares `name`; blankets use `T` beside their trait.
- Result: bevy +44, flagged unchanged, nothing removed. Fixture blanketpath: main FALSE-STOP, now right.
## 32be5aff: unseen impl inheriting a default body links the declaration (G037)
- Mechanism: `SimpleState::register_state(..)`, where the impl is derived and invisible.
- Rule: the sole in-scope trait declaring `name` with a default body, no blanket defining it, and no corpus impl overriding it. Named assumption: a derive never writes that method.
- Result: bevy +20, flagged unchanged. Fixture defaultpath (impls from a macro_rules! standing in for the derive): main FALSE-STOP, now right.
## 31466d1c: `<Type as Trait<A>>::f` links the self type's one impl of the trait head (G036)
- Mechanism: `CreateTypeData<T>` against `impl<S> CreateTypeData<S, ()>`, and `<&SubEntity<L> as WorldQuery>` against `impl<T> WorldQuery for &T`.
- Rule: when there is no exact match, exactly one impl in the self bucket whose self and trait args may match settles the call.
- Result: bevy +30, flagged unchanged, no site lost. Fixture explicitimpl: main withholds 2, now right 2.
## b9c19ac2: a struct-variant literal is typed as its enum (G033 part)
- Mechanism: `GltfAssetLabel::Primitive { .. }.from_asset(..)` typed its receiver as `GltfAssetLabel.Primitive`.
- Rule: when the literal's path names no type and its head is a registered project struct or enum (`Self` included), the literal is that head.
- Result: bevy +10, flagged unchanged. Fixture variantlit: main FALSE-STOP method_not_found, now right.
## Checks on b9c19ac2
- Build: no warnings.
- Fixtures (full, FIXTURES_WORK under /scratch): 93 x 2 modes, 97 known, 0 failures.
- rust_lsp on a fresh dir: EXIT=0, 837 passed, 0 failed.
- Not done: rebase onto main after lane/w5-path merges (waiting for the coordinator's word, per instruction).
## Rebase onto e1df45de
- One conflict in rust_lsp.c: w5-path's rust_unbracketed_path and this lane's forward declarations were at the same spot. Resolved by keeping both.
## 8d0b875e: review fixes (the two CONFIRMED items)
- Blanket rule: another in-scope trait declaring `name` with a default body and no defining blanket now withholds. An impl that defines nothing (`impl<T> B for T {}`) registers no method.
- Explicit-impl fallback: runs only when the trait requires `name` (no default body). Otherwise an undecided argument comparison withholds.
- Fixtures:
  - explicitimpl `inherit`: the pre-fix binary gives WRONG -> S.f[Tr<u8, u16>]; now withheld.
  - blanketpath `attach`: now withheld, listed in known.tsv as W6-RECV R1. Before the fix it was linked, which is correct in that compiling shape; the fixture shows the withhold, it does not prove a wrong link.
- Checks on 8d0b875e:
  - build has no warnings;
  - full fixtures: 96 x 2 modes, 98 known, 0 failures;
  - rust_lsp on a fresh dir: EXIT=0, 840 passed, 0 failed;
  - bevy 75604/84295 f9.
