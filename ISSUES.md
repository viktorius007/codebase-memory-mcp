# ISSUES — open defects and gaps

This is the canonical backlog of current issues. Remove resolved items; history
lives in git.

## CLI cache validation fails in restricted sandboxes and hides the cause

`codebase-memory-mcp cli …` can exit with
`secure CLI coordination could not be created (cache-private)` before executing
the requested command. Reported in the OpenAI Codex CLI sandbox; reproduced on
2026-09-02 with the installed `bf6b3c2f8f32…` build under a macOS sandbox denying
writes to an existing owner-private temporary cache. The same installed binary
successfully lists projects outside that restriction. The exact original Codex
sandbox policy has not been reproduced.

Current source still has both relevant behaviors:

- `src/daemon/ipc.c` → `private_directory_tree_open` attempts `mkdirat` while
  walking existing directories, then unconditionally attempts `fchmod(0700)`
  and ACL clearing on the cache root. Validation can therefore fail when the
  sandbox allows reading an already-private cache but denies these writes.
- `src/main.c` → the local CLI coordination failure branch prints only
  `cache-private`, discarding the path and failed-check detail available through
  `cbm_daemon_ipc_validation_detail()`.

Expose the failed check and path in CLI errors, and establish a supported way
for sandboxed callers to use the CLI while preserving cache and coordination
security. `CBM_CACHE_DIR` already overrides the cache root; whether an allowed
private cache resolves the original Codex sandbox failure remains unverified.

## Empty cross-file Java registry performs null-pointer arithmetic

`internal/cbm/lsp/java_lsp.c:3898`, in `cbm_java_build_cross_registry`, evaluates
`jvm + type_count` when `def_count == 0`, leaving `jvm == NULL` and
`type_count == 0`. The function explicitly supports an empty corpus, but adding
even zero to a null pointer is undefined behavior.

The current-source sanitizer run on 2026-09-02
(`scripts/test.sh --suites rust_lsp,pipeline,mcp,cli,daemon_ipc`) reported
`runtime error: applying zero offset to null pointer` at that expression.
Handle the empty registry without pointer arithmetic on NULL and cover that
valid input under the sanitizer.
