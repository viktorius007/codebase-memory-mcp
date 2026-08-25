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
 * The parser is a small hand-written TOML scanner. It captures Cargo package,
 * workspace, target, and dependency routing data, including dotted keys,
 * target-conditioned dependencies, and dependency subtables. Values outside
 * that routing subset are structurally scanned but not interpreted. */

#ifndef CBM_LSP_RUST_CARGO_H
#define CBM_LSP_RUST_CARGO_H

#include "../cbm.h"
#include <stdbool.h>

#define CBM_CARGO_MAX_DEPS 1024
#define CBM_CARGO_MAX_MEMBERS 512

typedef struct {
    const char* name;       /* declared dependency name */
    const char* path;       /* path = "../foo" if local, else NULL */
    const char *package;    /* package = "real-name" when `name` is an alias */
    bool inherits_workspace; /* member declaration delegates to workspace dependency */
    bool workspace_source;   /* declaration originates in [workspace.dependencies] */
} CBMCargoDep;

typedef struct {
    const char* member_name;   /* directory name */
    const char* member_path;   /* relative path inside workspace root */
} CBMCargoMember;

typedef enum {
    CBM_CARGO_TARGET_LIB = 1,
    CBM_CARGO_TARGET_BIN = 2,
    CBM_CARGO_TARGET_EXAMPLE = 3,
    CBM_CARGO_TARGET_TEST = 4,
    CBM_CARGO_TARGET_BENCH = 5,
    CBM_CARGO_TARGET_BUILD = 6,
} CBMCargoTargetKind;

typedef struct {
    CBMCargoTargetKind kind;
    const char *name;         /* explicit target name; NULL for defaults */
    const char *package_dir;  /* repository-relative owning package directory */
    const char *blocker_root; /* unsupported target module subtree, if any */
    /* Parser output is package-relative. The pipeline manifest builder rebases
     * these to repository-relative paths before the manifest reaches routing. */
    const char *source_path;
} CBMCargoTarget;

typedef struct {
    const char *package_dir;        /* repository-relative caller package, "" for root */
    const char *name;               /* source-level dependency alias */
    const char *target_name;        /* declared package/library name */
    const char *target_package_dir; /* exact admitted local package, NULL for registry deps */
} CBMCargoDependencyRoute;

typedef struct CBMCargoManifest {
    const char* package_name;    /* [package].name, NULL if missing */
    const char* package_version; /* [package].version, NULL if missing */
    bool is_workspace_root;      /* [workspace] section seen */

    CBMCargoDep deps[CBM_CARGO_MAX_DEPS];
    int dep_count;

    CBMCargoMember members[CBM_CARGO_MAX_MEMBERS];
    int member_count;
    CBMCargoMember excludes[CBM_CARGO_MAX_MEMBERS];
    int exclude_count;

    bool autolib;
    bool autobins;
    bool auto_build;
    bool has_lib_table;
    const char *build_path;

    CBMCargoTarget *targets;
    int target_count;
    int target_cap;
    bool targets_complete;
    CBMCargoDependencyRoute *dependency_routes;
    int dependency_route_count;
    int dependency_route_cap;

    /* Parser/cap health is part of the manifest result so diagnostics survive
     * independently of arena allocation and without a parallel log. */
    CBMRustAnalysisHealth health;
} CBMCargoManifest;

/* Parse a Cargo.toml-formatted string. The output strings are
 * arena-allocated (so the caller doesn't need to keep `src` alive). */
void cbm_cargo_parse(CBMArena* arena, const char* src, int src_len,
    CBMCargoManifest* out);

/* Append one typed target path. Arena growth preserves every prior entry. */
bool cbm_cargo_add_target(CBMArena *arena, CBMCargoManifest *manifest, CBMCargoTargetKind kind,
                          const char *source_path);
bool cbm_cargo_add_named_target(CBMArena *arena, CBMCargoManifest *manifest,
                                CBMCargoTargetKind kind, const char *name, const char *source_path);
bool cbm_cargo_add_routed_target(CBMArena *arena, CBMCargoManifest *manifest,
                                 CBMCargoTargetKind kind, const char *name, const char *package_dir,
                                 const char *source_path, const char *blocker_root);
bool cbm_cargo_add_dependency_route(CBMArena *arena, CBMCargoManifest *manifest,
                                    const char *package_dir, const char *name,
                                    const char *target_name, const char *target_package_dir);

/* Cargo package names use '-', while Rust source addresses the crate with '_'. */
bool cbm_cargo_crate_name_eq(const char *cargo_name, const char *source_name);

/* Convenience: does a given path-prefix look like one of the listed
 * dependency names? Used by the resolver to recognise external crate
 * paths. */
bool cbm_cargo_is_known_dep(const CBMCargoManifest* m, const char* head);

/* Find a workspace member by crate name. Returns NULL if absent. */
const CBMCargoMember* cbm_cargo_find_member(const CBMCargoManifest* m,
                                             const char* name);

/* Return the sole local package directory routed by this dependency name. */
const char *cbm_cargo_find_local_dependency_package(const CBMCargoManifest *m,
                                                    const char *name);

#endif /* CBM_LSP_RUST_CARGO_H */
