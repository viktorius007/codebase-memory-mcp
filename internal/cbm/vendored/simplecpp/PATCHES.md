# Local patches to simplecpp

Reapply on every upstream refresh; `scripts/vendored-checksums.txt` covers the
patched files.

## `DUI::includePathsOnly` (header lookup only through named directories)

- `simplecpp.h`: `DUI` gains `bool includePathsOnly{}` (default false, upstream
  behaviour).
- `simplecpp.cpp`: when it is set, `openHeader` (used by `__has_include`) and
  `FileDataCache::get` (used by `#include` in `load` and `preprocess`) skip the
  lookup next to the including file and refuse an absolute header path; only
  `DUI::includePaths` is searched. Each changed line carries `// CBM patch`.

Why: the indexer hands simplecpp an in-memory buffer named by its
repo-relative path. Upstream resolves a quoted include next to that name, i.e.
against the process's working directory, so the expansion of one file depended
on where the indexer ran and on what that directory held at the time.
`internal/cbm/preprocessor.cpp` sets the flag; tests:
`extract_c_preprocessed_pass_ignores_working_directory` (suite extraction).
