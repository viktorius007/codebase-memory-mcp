# Offline std method inventory

Run `python3 tools/std-inventory/generate.py` to generate the checked-in header,
or `make -C tools/std-inventory check` to fail on any byte difference.
The check target accepts `HEADER=<fixture>` for the one-row discriminator.
The default input is the installed nightly-2026-08-10 toolchain. `--toolchain`
selects another installed toolchain; format versions other than 61 are rejected.
Python's standard library is the only dependency. No downloads or compiler/LSP
process is needed at index time. Generation runs `rustc --version` by hand only.
Capture date is fixed at 2026-10-14 so regeneration is reproducible. Input hashes,
format version and full toolchain version are embedded in the header.

`receivers.json` maps resolver identities to exact rustdoc definition paths;
public re-exports need not have the same path in rustdoc. AtomicUsize is a type
alias in this toolchain and is followed to Atomic. NONE is a sentinel, not a type.
The three additional enum types Pin, Box and Future are covered too.

Each CBM_STD_METHOD row has receiver identity, method name, reachability, source
rustdoc item's JSON Pointer in its input file, and version. There is exactly one row per name
per identity: the first deterministic witness is retained when several paths
expose the same name. Associated functions without a self parameter are excluded.
CBM_STD_STAMP gives status and reason. Defining either X macro before including
selects those rows; without macros, the header instantiates two const data arrays
in rust_lsp.c. No resolver reads either array.

Reachability labels:

- `inherent`: an attached inherent impl.
- `trait:<path>`: an attached trait impl, including its default methods and
  recursively available supertrait methods, or a trait receiver's own methods.
- `blanket:<path>`: a rustdoc-attached blanket impl; bounds are **not evaluated**.
- `autoref:<class>`: an explicitly documented impl on a borrowed receiver with
  the same head, found in that crate's JSON index.
- `deref:<target>`: methods found by following a concrete Deref::Target in JSON.
  Generic targets cannot be followed; slice heads erase element arguments.

Rows are a conservative union of conditional std names. A trait may require an
import, an impl may require bounds, and a method may be unstable or target-specific.
Listing is useful for withholding a possibly competing project method; it does
not prove that this receiver instantiation can call it, or select a dispatch target.

## Completeness invariant

An absence verdict requires all Rust method lookup candidates and visible traits.
The [Rust Reference method lookup rules](https://doc.rust-lang.org/reference/expressions/method-call-expr.html)
include repeated dereferencing, array-to-slice unsizing, and shared/mutable
borrows of candidates. Inherent methods and visible implemented traits are searched;
generic receivers also search their bounds. The
[prelude rules](https://doc.rust-lang.org/reference/names/preludes.html)
select default imports by edition and no_std. The generator includes documented
traits beyond the prelude, a conservative union rather than an import analysis.

Every stamp is incomplete. The JSON snapshot plus erased resolver identities
cannot certify reference/unsizing closure, generic pointee methods, blanket bound
satisfaction, all imported std traits, or all feature/target/edition combinations.
Even String's fixed deref chain does not remove the snapshot/platform and trait
scope limitations. Never infer "project method" from absence in this inventory.
A later task must retain withholding for every currently unlisted name. Zero
complete stamps is intentional; no type was guessed complete.

The manifest also records 99 additional receiver heads found in the starting
codex/bevy stop records with null definition mappings. These have incomplete
stamps and zero method rows: their sets were not traversed and no identity mapping
was assumed. They allow the recovery measurement to count every stop without
silently dropping receivers outside the required coverage floor. All 27 mapped
identities have methods. Null mappings are not evidence that a type lacks methods.

Run `python3 tools/std-inventory/measure.py <score.db> --sql-out <output.sql>`
to execute the fenced top-20 gap query, check receiver coverage, and count listed,
unlisted-complete and unlisted-incomplete sites. The output SQL embeds exactly the
header's method/stamp rows and the resolver's public identity aliases as TEMP
tables; it opens the graph read-only and changes no persistent tables. Listed site
counts are a ceiling for conservative withholding, not compiler-proven recovery.
