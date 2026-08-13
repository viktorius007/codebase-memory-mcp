/*
 * arena.h — Bump allocator with block-based growth.
 *
 * All memory is freed at once via cbm_arena_destroy(). Individual frees are
 * not supported — this is by design for per-file extraction where all data
 * has the same lifetime.
 *
 * Restructured from internal/cbm/arena.h for the pure C rewrite.
 * New additions: cbm_arena_reset() for reuse without realloc.
 */
#ifndef CBM_ARENA_H
#define CBM_ARENA_H

#include <stddef.h>
#include <stdarg.h>

#define CBM_ARENA_MAX_BLOCKS 256
#define CBM_ARENA_DEFAULT_BLOCK_SIZE ((size_t)64 * 1024) /* 64KB */

typedef enum {
    CBM_ARENA_STATUS_AVAILABLE = 0,
    CBM_ARENA_STATUS_ALLOCATION_UNAVAILABLE = 1,
} CBMArenaStatus;

typedef enum {
    CBM_ARENA_ALLOCATION_GENERAL = 0,
    CBM_ARENA_ALLOCATION_RUST_DEFINITION_RETURN = 1,
    CBM_ARENA_ALLOCATION_RUST_AST_RETURN_PATCH = 2,
    CBM_ARENA_ALLOCATION_RUST_DERIVE_EMBEDDED = 3,
    CBM_ARENA_ALLOCATION_RUST_DERIVE_RETURN = 4,
    CBM_ARENA_ALLOCATION_RUST_IMPL_RETURN = 5,
    CBM_ARENA_ALLOCATION_RUST_CROSS_RETURN_ARRAY = 6,
    CBM_ARENA_ALLOCATION_RUST_CROSS_RETURN_BUFFER = 7,
} CBMArenaAllocationClass;

typedef struct {
    char *blocks[CBM_ARENA_MAX_BLOCKS];
    size_t block_sizes[CBM_ARENA_MAX_BLOCKS]; /* per-block sizes (for stats) */
    int nblocks;
    size_t block_size;  /* current block capacity */
    size_t used;        /* bytes used in current block */
    size_t total_alloc; /* cumulative bytes allocated (for stats) */
    CBMArenaStatus status; /* sticky: never returns to AVAILABLE after allocation loss */
    /* ABI-stable seam state: the mutator remains test-build-only, but unity
     * objects compiled without that define must retain the identical layout. */
    size_t test_successes_before_failure;
    int test_failure_enabled;
    CBMArenaAllocationClass test_failure_class;
    int test_class_failure_enabled;
} CBMArena;

/* Initialize arena with default block size. */
void cbm_arena_init(CBMArena *a);

/* Initialize arena with a custom initial block size. */
void cbm_arena_init_sized(CBMArena *a, size_t block_size);

/* Allocate n bytes (8-byte aligned). Returns NULL on OOM. */
void *cbm_arena_alloc(CBMArena *a, size_t n);

/* Allocate with a diagnostic class. Classes do not change production allocation
 * behavior; they provide an exact failure seam for otherwise indistinguishable
 * in-flight requests. */
void *cbm_arena_alloc_class(CBMArena *a, size_t n, CBMArenaAllocationClass allocation_class);

/* Allocate n bytes, zero-initialized. */
void *cbm_arena_calloc(CBMArena *a, size_t n);

/* Duplicate a NUL-terminated string. */
char *cbm_arena_strdup(CBMArena *a, const char *s);
char *cbm_arena_strdup_class(CBMArena *a, const char *s, CBMArenaAllocationClass allocation_class);

/* Duplicate a string of known length, NUL-terminate. */
char *cbm_arena_strndup(CBMArena *a, const char *s, size_t len);

/* sprintf into arena memory. */
char *cbm_arena_sprintf(CBMArena *a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Reset arena for reuse: keeps first block, frees the rest. */
void cbm_arena_reset(CBMArena *a);

/* Free all blocks. Arena is zeroed after this. */
void cbm_arena_destroy(CBMArena *a);

/* Return total bytes allocated (for diagnostics). */
size_t cbm_arena_total(const CBMArena *a);

/* Return the sticky allocation status. NULL arenas are unavailable. */
CBMArenaStatus cbm_arena_status(const CBMArena *a);

#ifdef CBM_ENABLE_TEST_SEAMS
/* Fail this arena after exactly N further successful allocation requests. */
void cbm_arena_test_fail_after(CBMArena *a, size_t successful_allocations);
void cbm_arena_test_fail_class(CBMArena *a, CBMArenaAllocationClass allocation_class);
#endif

#endif /* CBM_ARENA_H */
