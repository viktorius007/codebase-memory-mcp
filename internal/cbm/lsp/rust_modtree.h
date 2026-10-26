/* rust_modtree.h — Rust crate module tree and path name resolution.
 *
 * Rust module identity is declared, not inferred from file layout: each crate
 * root grows a tree through `mod` declarations (out-of-line `mod x;` loading
 * `x.rs` / `x/mod.rs` or a `#[path]` file, and inline `mod x { .. }`), and
 * names enter a module through its items, `use` imports (named and glob) and
 * `extern crate`. This layer builds that tree once per project from the
 * crate roots Cargo declares and answers "what does path `a::b::f` name, as
 * written in file F at byte B" by walking the tree and the `use` graph.
 *
 * Graph identities: an item declared in a module (inline or not) is
 * published as `<file module QN>.<name>`, so a resolved function or type is
 * reported by that QN; the caller verifies it against the registry.
 *
 * The tree is immutable after cbm_rust_modtree_finish(): resolution is
 * read-only and safe to call from several threads. */
#ifndef CBM_LSP_RUST_MODTREE_H
#define CBM_LSP_RUST_MODTREE_H

#include "../arena.h"
#include "../cbm.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct CBMRustModTree CBMRustModTree;

enum { CBM_RUST_PRELUDE_DISABLED = 1, CBM_RUST_PRELUDE_CORE_ONLY = 2 };

/* Prelude restrictions written on this source module (inner attributes and
 * attributes on its enclosing `mod`); conditional restrictions are conservative. */
uint8_t cbm_rust_prelude_restrictions(CBMArena *arena, const char *source, TSNode scope);
/* True only when every crate mount at this occurrence enables the named
 * standard-prelude trait, including restrictions inherited from parent modules. */
bool cbm_rust_modtree_prelude_allows(const CBMRustModTree *tree, const char *module_qn,
                                     uint32_t offset, const char *name);

/* The `#[path = "..."]` value attached to a `mod` item, "" for an empty
 * literal, NULL when absent or not a plain string. */
char *cbm_rust_path_attribute(CBMArena *arena, const char *source, TSNode mod_item);

/* Digest of every fact one file contributes to the tree (its `mod`, `use`,
 * `extern crate` and named items, at every inline-module depth, and whether
 * it has item-position macro calls), independent of byte offsets. Equal
 * digests mean an edit cannot change how another file resolves through the
 * tree. `root` is the file's tree-sitter root node. */
uint64_t cbm_rust_modtree_file_digest(const char *source, TSNode root);

/* The facts one file contributes to the tree, in source order: its `mod`,
 * `use` and `extern crate` items, named items and item-position macro calls,
 * at every inline-module depth. A pure function of the file's bytes, so
 * files are scanned independently (and concurrently) before the tree is
 * built from them. */
typedef struct CBMRustModFacts CBMRustModFacts;

/* Parse `source` and collect its tree facts. NULL when it cannot be parsed.
 * Safe to call from several threads at once. */
CBMRustModFacts *cbm_rust_modfacts_scan(const char *source, int len);
void cbm_rust_modfacts_free(CBMRustModFacts *facts);

typedef enum {
    CBM_RUST_PATH_UNKNOWN = 0,     /* the tree cannot decide; caller may try other evidence */
    CBM_RUST_PATH_FUNCTION,        /* `qn` is a free function */
    CBM_RUST_PATH_ASSOC,           /* `qn` is a type or trait; `assoc` is its associated item */
    CBM_RUST_PATH_NOT_FUNCTION,    /* proven to name no in-project function (variable, type,
                                    * constant, variant, external crate item); for a type
                                    * query: proven to name a type outside the project */
    CBM_RUST_PATH_EDITION_UNKNOWN, /* crate edition is unreadable: do not use fallback paths */
    CBM_RUST_PATH_TYPE,            /* type query: `qn` is a type or trait declared in the project */
    CBM_RUST_PATH_AMBIGUOUS,       /* value query: two or more distinct items carry the name in
                                    * this module (only cfg-gated alternatives can, e.g. a local
                                    * `#[cfg(unix)] fn f` beside `#[cfg(windows)] use w::f`);
                                    * which one a build sees depends on its configuration */
} CBMRustPathKind;

typedef struct {
    CBMRustPathKind kind;
    const char *qn;    /* Declaring QN when known; ASSOC: the type or trait QN. */
    const char *assoc; /* ASSOC: the associated item name */
    bool is_trait;     /* ASSOC, TYPE: `qn` is a trait */
    bool conditional;  /* TYPE: declaration/import requires an unproved cfg */
    /* FUNCTION: 1-based line of the declaring `fn`, 0 when two declarations
     * of `qn` stand in one module (cfg twins). Inline modules have their own QN. */
    uint32_t line;
} CBMRustPathResult;

CBMRustModTree *cbm_rust_modtree_new(void);
void cbm_rust_modtree_free(CBMRustModTree *tree);

/* Register a Rust source file. `rel_path` is '/'-separated and relative to
 * the project root; `module_qn` is the file's graph module QN. Returns the
 * file id used by the loader. */
int cbm_rust_modtree_add_file(CBMRustModTree *tree, const char *rel_path, const char *module_qn);

typedef enum {
    CBM_RUST_EDITION_MODERN = 0,
    CBM_RUST_EDITION_2015 = 1,
    CBM_RUST_EDITION_UNKNOWN = 2,
    CBM_RUST_EDITION_2021 = 3,
    CBM_RUST_EDITION_2024 = 4,
} CBMRustEdition;

/* Register a crate root. `extern_name` is the name other crates use for it
 * (NULL for targets nobody can depend on: binaries, tests, examples). The
 * returned crate id receives extern-prelude entries below. UNKNOWN keeps
 * the module ownership but refuses path links into or out of the crate. */
int cbm_rust_modtree_add_crate(CBMRustModTree *tree, const char *root_rel_path,
                               const char *extern_name, CBMRustEdition edition);

/* Record the Cargo features enabled for `crate_id`. A crate given a set
 * decides `#[cfg(feature = "..")]` on its items and `use` declarations:
 * a false predicate drops the item, a true one makes it unconditional.
 * Every other cfg predicate stays undecided. */
void cbm_rust_modtree_set_crate_features(CBMRustModTree *tree, int crate_id,
                                         const char *const *features, int count);

/* Make `name` visible in `crate_id`'s extern prelude. `target_crate` is the
 * library crate it names, or -1 for a crate outside the project. */
void cbm_rust_modtree_add_extern(CBMRustModTree *tree, int crate_id, const char *name,
                                 int target_crate);

/* Number of registered files; file ids are 0 .. count-1. */
int cbm_rust_modtree_file_count(const CBMRustModTree *tree);

/* Build every crate's module tree and resolve every `use`. `facts[id]` holds
 * file `id`'s scanned facts, NULL when the file could not be read or parsed
 * (its modules may then declare unseen names). The tree keeps no pointer
 * into `facts`. */
void cbm_rust_modtree_finish(CBMRustModTree *tree, const CBMRustModFacts *const *facts);

/* True when the file with this graph module QN belongs to at least one crate. */
bool cbm_rust_modtree_has_file(const CBMRustModTree *tree, const char *module_qn);

/* True when, in every crate that compiles the file with graph module QN
 * `module_qn`, `name` is an extern-prelude entry naming a crate outside the
 * project (a registry or git dependency). False when the file belongs to no
 * crate or any of its crates lacks that entry or maps it into the project. */
bool cbm_rust_modtree_extern_outside(const CBMRustModTree *tree, const char *module_qn,
                                     const char *name);

/* Graph module QN of the project library root file that `name` denotes in the
 * extern prelude of every crate compiling the file `module_qn`; NULL when the
 * name is no project library there, or when those crates disagree. */
const char *cbm_rust_modtree_extern_root(const CBMRustModTree *tree, const char *module_qn,
                                         const char *name);

/* The library name of the crate whose root file has graph module QN
 * `root_qn`; NULL when no crate or crates of different names root there. */
const char *cbm_rust_modtree_crate_name(const CBMRustModTree *tree, const char *root_qn);

/* Whether every module of the file `caller_qn` lies outside every module of
 * the file `owner_qn`, in every crate that compiles the caller: no module of
 * the owner file is the caller's module or one of its ancestors. That is when
 * an item private to the owner file's modules (a method of an inherent impl
 * without `pub`) cannot be named by the caller. False when either file
 * belongs to no crate, or when any caller module sits inside the owner. */
bool cbm_rust_modtree_file_outside(const CBMRustModTree *tree, const char *caller_qn,
                                   const char *owner_qn);
/* Resolve a call path (`f`, `a::b::f`, `crate::m::f`, `Type::f`) written at
 * byte `offset` of the file whose graph module QN is `module_qn`. `extra_uses`
 * are `use` bindings declared in enclosing blocks (local name, path text),
 * innermost last. A file reached from several crates must resolve to the
 * same identity in each, otherwise the result is UNKNOWN. Strings in the
 * result live in `arena` or the tree. */
CBMRustPathResult cbm_rust_modtree_resolve(const CBMRustModTree *tree, CBMArena *arena,
                                           const char *module_qn, uint32_t offset, const char *path,
                                           const char *const *extra_names,
                                           const char *const *extra_paths, int extra_count);

/* QN of the struct, enum or union that type path `path` names in the file
 * with graph module QN `module_qn`, from every module of that file in which
 * the path's head is in scope. NULL unless all of them name the same one: an
 * alias, a trait, an item outside the project or an undecidable name gives
 * NULL. The answer does not depend on where in the file the path is
 * written, so an extracted definition that carries no byte offset (an impl
 * block's self type) gets the same answer as its source position. */
const char *cbm_rust_modtree_resolve_nominal_type(const CBMRustModTree *tree, CBMArena *arena,
                                                  const char *module_qn, const char *path);

/* Resolve a path written in a type position (`T`, `a::b::T`, `crate::m::T`)
 * in the type namespace, with the same scoping as cbm_rust_modtree_resolve.
 * TYPE when it names exactly one type, enum, union, alias or trait of the
 * project; NOT_FUNCTION when it names an item outside the project, with `qn`
 * the dotted path the `use` graph reaches it by (NULL when unknown); UNKNOWN
 * otherwise (generic parameters, associated types, undecidable names). */
CBMRustPathResult cbm_rust_modtree_resolve_type(const CBMRustModTree *tree, CBMArena *arena,
                                                const char *module_qn, uint32_t offset,
                                                const char *path, const char *const *extra_names,
                                                const char *const *extra_paths, int extra_count);

/* Resolve a macro path (`m`, `a::m`, `crate::m`) written at byte `offset`
 * in the macro namespace, with the same scoping as cbm_rust_modtree_resolve:
 * `use` imports and glob chains, and `#[macro_export]` macro_rules! items at
 * their crate root. NOT_FUNCTION with `qn` `<crate root file QN>.<name>` when
 * exactly one project macro answers; UNKNOWN or AMBIGUOUS otherwise. A
 * macro_rules! without `#[macro_export]` is in textual scope only and never
 * answers here. */
CBMRustPathResult cbm_rust_modtree_resolve_macro(const CBMRustModTree *tree, CBMArena *arena,
                                                 const char *module_qn, uint32_t offset,
                                                 const char *path, const char *const *extra_names,
                                                 const char *const *extra_paths, int extra_count);

/* Prove a trait binding at this site, including renamed and anonymous
 * imports reached through module globs. Extra globs are raw block-use paths. */
bool cbm_rust_modtree_trait_in_scope(const CBMRustModTree *tree, CBMArena *arena,
                                      const char *module_qn, uint32_t offset, const char *qn,
                                      const char *const *extra_names,
                                      const char *const *extra_paths, int extra_count,
                                      const char *const *extra_globs, int glob_count);

/* Physical module containing a declared item QN, or NULL if unregistered. */
const char *cbm_rust_modtree_item_module(const CBMRustModTree *tree, CBMArena *arena,
                                         const char *item_qn);

/* Resolve a definition header in the exact physical module QN, including
 * inline modules. Every crate mount of that module must agree. */
CBMRustPathResult cbm_rust_modtree_resolve_type_in_file(const CBMRustModTree *tree, CBMArena *arena,
                                                        const char *module_qn, const char *path);

/* The project items a named `use` written in the registered file `rel_path`
 * binds: `local_name` is the name it introduces and `path` the
 * path it spells, both as cbm_extract_rust_use_tree reports them. A `use`
 * declared at module level answers from the module that declares it; one
 * declared in a block is resolved from every module of the file. Writes up
 * to `max` item QNs (a name can bind one item per namespace) and returns
 * their count; 0 when the path is proven to name nothing the project
 * declares (an item of an external crate or of the language). Returns -1
 * when the tree cannot decide: an unresolved path, a module, an enum
 * variant, or modules of the file that disagree. */
int cbm_rust_modtree_use_items(const CBMRustModTree *tree, CBMArena *arena, const char *rel_path,
                               const char *local_name, const char *path, const char **out_qns,
                               int max);

/* Same answer for a `use` written at byte `offset` of the file, inside an
 * inline module or a block: the innermost module at that offset decides,
 * from the `use` it declares, else by resolving the path in its scope. */
int cbm_rust_modtree_use_items_at(const CBMRustModTree *tree, CBMArena *arena, const char *rel_path,
                                  uint32_t offset, const char *local_name, const char *path,
                                  const char **out_qns, int max);

/* One `type name = value;` written in a project trait impl
 * (`impl Trait for Self { .. }`): the impl's self type as written, the value
 * as written, the impl's physical module, and whether the value depends on
 * impl or associated-type parameters the record does not carry. */
typedef struct {
    const char *self_text;
    const char *value;
    const char *module_qn;
    bool unreadable;
} CBMRustImplAssoc;

/* Every such record of `name` on the project type `self_qn`, at most `max`,
 * into `out`. Returns their count, 0 when none, -1 when more than `max`. */
int cbm_rust_modtree_impl_assocs(const CBMRustModTree *tree, CBMArena *arena,
                                 const char *self_qn, const char *name, CBMRustImplAssoc *out,
                                 int max);

#endif /* CBM_LSP_RUST_MODTREE_H */
