/*
 * arena.c — Bump allocator implementation.
 *
 * Restructured from internal/cbm/arena.c with additions:
 *   - cbm_arena_init_sized() for custom block sizes
 *   - cbm_arena_calloc() for zero-initialized allocations
 *   - cbm_arena_reset() for reuse without full destroy
 *   - cbm_arena_total() for allocation tracking
 *
 * Arena blocks use malloc/free (= mimalloc in production builds).
 */
#include "arena.h"
#include "foundation/constants.h"

enum { ARENA_ALIGN = 7, ARENA_GROW_OK = 1 };
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

static void arena_mark_unavailable(CBMArena *a) {
    if (a) {
        a->status = CBM_ARENA_STATUS_ALLOCATION_UNAVAILABLE;
    }
}

void cbm_arena_init(CBMArena *a) {
    cbm_arena_init_sized(a, CBM_ARENA_DEFAULT_BLOCK_SIZE);
}

void cbm_arena_init_sized(CBMArena *a, size_t block_size) {
    memset(a, 0, sizeof(*a));
    if (block_size < CBM_SZ_64) {
        block_size = CBM_SZ_64; /* minimum sanity */
    }
    a->block_size = block_size;
    a->blocks[0] = (char *)malloc(block_size);
    if (a->blocks[0]) {
        a->block_sizes[0] = block_size;
        a->nblocks = SKIP_ONE;
    } else {
        arena_mark_unavailable(a);
    }
}

static int arena_grow(CBMArena *a, size_t min_size) {
    if (a->nblocks >= CBM_ARENA_MAX_BLOCKS) {
        arena_mark_unavailable(a);
        return 0;
    }
    if (a->block_size > SIZE_MAX / PAIR_LEN) {
        arena_mark_unavailable(a);
        return 0;
    }
    size_t new_size = a->block_size * PAIR_LEN;
    if (new_size < min_size) {
        new_size = min_size;
    }
    char *block = (char *)malloc(new_size);
    if (!block) {
        arena_mark_unavailable(a);
        return 0;
    }
    a->blocks[a->nblocks] = block;
    a->block_sizes[a->nblocks] = new_size;
    a->nblocks++;
    a->block_size = new_size;
    a->used = 0;
    return ARENA_GROW_OK;
}

void *cbm_arena_alloc_class(CBMArena *a, size_t n, CBMArenaAllocationClass allocation_class) {
    if (!a || n == 0) {
        return NULL;
    }
#ifdef CBM_ENABLE_TEST_SEAMS
    if (a->test_class_failure_enabled && a->test_failure_class == allocation_class) {
        a->test_class_failure_enabled = 0;
        arena_mark_unavailable(a);
        return NULL;
    }
    if (a->test_failure_enabled && a->test_successes_before_failure == 0) {
        a->test_failure_enabled = 0;
        arena_mark_unavailable(a);
        return NULL;
    }
#endif
    /* 8-byte alignment */
    if (n > SIZE_MAX - ARENA_ALIGN) {
        arena_mark_unavailable(a);
        return NULL;
    }
    n = (n + ARENA_ALIGN) & ~(size_t)ARENA_ALIGN;
    if (a->nblocks == 0) {
        arena_mark_unavailable(a);
        return NULL;
    }
    if (a->used > a->block_size || n > a->block_size - a->used) {
        if (!arena_grow(a, n)) {
            return NULL;
        }
    }
    char *ptr = a->blocks[a->nblocks - SKIP_ONE] + a->used;
    a->used += n;
    a->total_alloc += n;
#ifdef CBM_ENABLE_TEST_SEAMS
    if (a->test_failure_enabled) {
        a->test_successes_before_failure--;
    }
#endif
    return ptr;
}

void *cbm_arena_alloc(CBMArena *a, size_t n) {
    return cbm_arena_alloc_class(a, n, CBM_ARENA_ALLOCATION_GENERAL);
}

void *cbm_arena_calloc(CBMArena *a, size_t n) {
    void *p = cbm_arena_alloc(a, n);
    if (p) {
        memset(p, 0, n);
    }
    return p;
}

char *cbm_arena_strdup_class(CBMArena *a, const char *s,
                             CBMArenaAllocationClass allocation_class) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *dst = (char *)cbm_arena_alloc_class(a, len + SKIP_ONE, allocation_class);
    if (dst) {
        memcpy(dst, s, len + SKIP_ONE);
    }
    return dst;
}

char *cbm_arena_strdup(CBMArena *a, const char *s) {
    return cbm_arena_strdup_class(a, s, CBM_ARENA_ALLOCATION_GENERAL);
}

char *cbm_arena_strndup(CBMArena *a, const char *s, size_t len) {
    if (!s) {
        return NULL;
    }
    char *dst = (char *)cbm_arena_alloc(a, len + SKIP_ONE);
    if (dst) {
        memcpy(dst, s, len);
        dst[len] = '\0';
    }
    return dst;
}

char *cbm_arena_sprintf(CBMArena *a, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed < 0) {
        return NULL;
    }

    char *dst = (char *)cbm_arena_alloc(a, (size_t)needed + SKIP_ONE);
    if (!dst) {
        return NULL;
    }

    va_start(args, fmt);
    vsnprintf(dst, (size_t)needed + SKIP_ONE, fmt, args);
    va_end(args);
    return dst;
}

void cbm_arena_reset(CBMArena *a) {
    /* Keep first block, free the rest */
    for (int i = SKIP_ONE; i < a->nblocks; i++) {
        free(a->blocks[i]);
        a->blocks[i] = NULL;
        a->block_sizes[i] = 0;
    }
    if (a->nblocks > SKIP_ONE) {
        a->nblocks = SKIP_ONE;
    }
    a->used = 0;
    a->total_alloc = 0;
    /* Reset block_size to match surviving block — prevents overflow if
     * block_size grew during previous allocations (e.g., CBM_SZ_128 → CBM_SZ_256). */
    if (a->nblocks == SKIP_ONE) {
        a->block_size = a->block_sizes[0];
    }
}

void cbm_arena_destroy(CBMArena *a) {
    for (int i = 0; i < a->nblocks; i++) {
        free(a->blocks[i]);
    }
    memset(a, 0, sizeof(*a));
}

size_t cbm_arena_total(const CBMArena *a) {
    return a->total_alloc;
}

CBMArenaStatus cbm_arena_status(const CBMArena *a) {
    return a ? a->status : CBM_ARENA_STATUS_ALLOCATION_UNAVAILABLE;
}

#ifdef CBM_ENABLE_TEST_SEAMS
void cbm_arena_test_fail_after(CBMArena *a, size_t successful_allocations) {
    if (!a) {
        return;
    }
    a->test_successes_before_failure = successful_allocations;
    a->test_failure_enabled = 1;
}


void cbm_arena_test_fail_class(CBMArena *a, CBMArenaAllocationClass allocation_class) {
    if (!a || allocation_class == CBM_ARENA_ALLOCATION_GENERAL) {
        return;
    }
    a->test_failure_class = allocation_class;
    a->test_class_failure_enabled = 1;
}
#endif
