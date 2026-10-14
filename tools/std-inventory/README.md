# Offline std method inventory

`python3 tools/std-inventory/generate.py` writes the committed header;
`make -C tools/std-inventory check` rejects any byte difference. `HEADER=<fixture>`
selects a comparison fixture. `--audit-out <file.json>` exports every conditional
class, method-item pointer, implementation witness, retained row count and stamp.
The installed nightly-2026-08-10 toolchain is the default; `--toolchain` selects
another installed toolchain. Format versions other than 61 are rejected. Python's
standard library is the only dependency. `rustc --version` runs during manual
regeneration only; indexing needs neither a compiler nor an LSP. The fixed capture
date, full version and input SHA256s make the snapshot reproducible.

`receivers.json` maps resolver heads and public re-exports to audited definition
paths. Primitive definitions win over same-path modules. Primitive implementations
from all three crates are combined under `core.<primitive>`. Type aliases are
followed, including AtomicUsize to Atomic; erased atomic arguments deliberately
union all specializations. The two `Option.Inner`/`Option.Item` heads have no
concrete receiver definition and remain incomplete. NONE is a sentinel, not a type.

Each `CBM_STD_METHOD` row holds receiver, method, reachability, the method item's
JSON Pointer, and version. There is one row per receiver/name. Selection priority
is inherent, trait-impl, blanket, reference-impl, then deref; the audit export
retains the overlapping class sets as well. Associated functions without a `self`
parameter are excluded. `CBM_STD_STAMP` holds receiver, status, reason. Including
the header with neither macro defined instantiates two const arrays in rust_lsp.c;
no resolver logic currently reads them.

## Enumeration and absence

The [Rust method lookup rules](https://doc.rust-lang.org/reference/expressions/method-call-expr.html)
build candidates by repeated dereferencing, array-to-slice unsizing, then adding
shared and mutable references to each candidate. Inherent methods precede visible
trait methods. A project can import any std trait, so this inventory unions all
trait scopes and editions, including the array IntoIterator edition exception.
It does not decide dispatch, bound satisfaction, mutability or stability.

The generator enumerates these classes without filtering cfg, visibility,
stability, feature, edition, target, bounds or generic arguments:

- `inherent`: every attached or whole-index impl with the receiver's definition
  head, including specialized generic arguments and dyn-trait variants.
- `trait-impl:<trait>`: explicit implementations, trait methods, defaults and
  recursive supertrait methods, plus the own methods of a trait receiver.
- `blanket:<trait>`: attached rustdoc blanket impls and **every** generic-self impl
  across alloc/core/std, regardless of bounds. Private specialization traits are
  also retained; this intentionally broad superset cannot authorize a false link.
- `reference-impl:<class>`: shared/mutable/nested borrowed receiver impls found
  across all three indices, including every generic-reference impl. This adds
  candidates such as `Iterator for &mut I` even when rustdoc did not attach them
  to a concrete type. Generic reference Deref returns the same candidate, not a
  new unknown pointee.
- `deref:<Target>`: the union at a concrete Deref target, recursively. String
  reaches str, PathBuf reaches Path, CString reaches CStr, Vec reaches slice.
  Array implementations also join the slice head as a safe over-approximation
  of the unsizing candidate. Tuple heads union all arities.

A listed name only licenses withholding a competing project link. Over-listing
is safe; private, impossible-bound and target-inapplicable witnesses can reduce
recovery, never create a wrong link. Conditional methods are kept even when their
cfg cannot hold on the JSON's aarch64-apple-darwin target. The snapshot includes
Windows/WASI/Unix extension traits and unstable items; target metadata is not a
filter. Completeness concerns this pinned std snapshot, not future toolchains or
third-party extension traits.

`complete` means all these JSON candidate classes were enumerated as a superset.
`complete_except_deref` means the own union is complete but a generic
Deref::Target/projection or an erased pointer/container element is deferred to its
own receiver inventory. This includes Box/Rc/Arc/Pin/Cow/ManuallyDrop and
RefCell/Mutex/RwLock/Cell/OnceLock/Weak/NonNull. Vec remains complete: its target
head is the enumerable slice, and slice lookup does not dereference its elements.
The Deref/DerefMut trait heads themselves defer their associated Target.

`incomplete` identifies a concrete unavailable JSON class. The pinned JSON lacks
inherent dyn Any and dyn Error methods (`is`/`downcast_ref`); their documented
trait items are present but their inherent blocks cannot be enumerated. These two
trait heads remain incomplete. A referenced method/trait or concrete target
missing from the inputs also produces an incomplete reason; it is never silently
skipped into a complete stamp. Bounds, imports and generic pointees alone do not
make an own-set stamp incomplete.

## Measurement

`python3 tools/std-inventory/measure.py <score.db> --sql-out <output.sql>` executes
the fenced top-20 gap query and exports the exact membership SQL. It opens the
graph read-only, loads only TEMP tables, and reports listed, unlisted-complete,
unlisted-except-deref, unlisted-incomplete and uncovered site counts. SQL keys
are receiver/method, not provenance rows; overlapping reachability does not
multiply sites. Resolver public identity aliases come from rust_lsp.c. Counts
measure membership and potential recovery, not compiler-proven dispatch.
