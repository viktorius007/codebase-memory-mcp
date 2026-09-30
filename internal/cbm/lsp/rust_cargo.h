/* rust_cargo.h — Cargo.toml parser for the Rust LSP.
 *
 * Per RUST_LSP_FOLLOWUP §A3: we don't run Cargo or build, but we CAN
 * parse `Cargo.toml` and `[workspace] members` to learn:
 *   - the crate name (`[package].name`)
 *   - declared dependencies (`[dependencies]` + `[dev-dependencies]`)
 *   - workspace members + their relative paths
 *
 * The pipeline uses this to map `other_member::foo` → that member's
 * module QN, and to mark calls into known external deps as "external,
 * not local" rather than fully unresolved.
 *
 * The parser is a tiny hand-written TOML subset: handles `[section]`
 * headers, `key = "value"`, `key = { path = "...", … }` (the relevant
 * subset for our needs), arrays `members = ["a", "b"]`. It IGNORES
 * everything it doesn't understand — that's safe because Cargo.toml
 * is much richer than what we use. */

#ifndef CBM_LSP_RUST_CARGO_H
#define CBM_LSP_RUST_CARGO_H

#include "../arena.h"
#include <stdbool.h>

#define CBM_CARGO_MAX_DEPS 256
#define CBM_CARGO_MAX_MEMBERS 1024

typedef enum {
    CBM_CARGO_TARGET_LIB,
    CBM_CARGO_TARGET_BIN,
    CBM_CARGO_TARGET_TEST,
    CBM_CARGO_TARGET_EXAMPLE,
    CBM_CARGO_TARGET_BENCH,
} CBMCargoTargetKind;

typedef struct {
    CBMCargoTargetKind kind;
    const char *name;
    const char *path;
} CBMCargoTarget;

typedef struct {
    const char* name;       /* declared dependency name */
    const char* path;       /* path = "../foo" if local, else NULL */
} CBMCargoDep;

typedef struct {
    const char *member_name; /* directory name */
    const char *member_path; /* relative path inside workspace root */
    const char *crate_name;  /* [package].name of the member manifest, or NULL */
    const char *lib_name;    /* [lib].name of the member manifest, or NULL */
    const CBMCargoTarget *targets;
    int target_count;
} CBMCargoMember;

typedef struct CBMCargoManifest {
    const char *package_name;    /* [package].name, NULL if missing */
    const char *package_version; /* [package].version, NULL if missing */
    const char *lib_name;        /* [lib].name, NULL if missing */
    const char *edition;         /* [package].edition, NULL if Cargo's default */
    const char *workspace_edition; /* [workspace.package].edition */
    bool edition_inherited;      /* edition.workspace = true */
    bool is_workspace_root;      /* [workspace] section seen */
    bool auto_targets[5];
    unsigned auto_target_overrides;

    CBMCargoTarget *targets;
    int target_count;
    int target_capacity;

    CBMCargoDep deps[CBM_CARGO_MAX_DEPS];
    int dep_count;

    CBMCargoMember members[CBM_CARGO_MAX_MEMBERS];
    int member_count;
} CBMCargoManifest;

/* Parse a Cargo.toml-formatted string. The output strings are
 * arena-allocated (so the caller doesn't need to keep `src` alive). */
void cbm_cargo_parse(CBMArena* arena, const char* src, int src_len,
    CBMCargoManifest* out);

/* Record a target Cargo discovered from the package's filesystem layout.
 * Explicit target tables without a path are completed by matching kind/name;
 * otherwise a target is added only when Cargo auto-discovery is enabled. */
void cbm_cargo_record_discovered_target(CBMArena *arena, CBMCargoManifest *manifest,
                                        CBMCargoTargetKind kind, const char *name,
                                        const char *path, const char *inherited_edition);

/* Convenience: does a given path-prefix look like one of the listed
 * dependency names? Used by the resolver to recognise external crate
 * paths. */
bool cbm_cargo_is_known_dep(const CBMCargoManifest* m, const char* head);

/* Find a workspace member by the head of a Rust path: its directory name,
 * its [package].name or its [lib].name, comparing `-` and `_` as equal.
 * Returns NULL if absent. */
const CBMCargoMember *cbm_cargo_find_member(const CBMCargoManifest *m, const char *name);

/* Record the member manifest's package and lib names on the workspace
 * member entry so `crate_name::item` paths can be routed to the member. */
void cbm_cargo_set_member_names(CBMCargoMember *member, const char *crate_name,
                                const char *lib_name, const CBMCargoTarget *targets,
                                int target_count);

#endif /* CBM_LSP_RUST_CARGO_H */
