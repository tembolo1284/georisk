/**
 * internal/allocator.h - Memory allocation internals
 * 
 * Custom allocator support following Ronacher's pattern.
 * Global allocators can be overridden, and contexts can have their own.
 */

#ifndef GR_INTERNAL_ALLOCATOR_H
#define GR_INTERNAL_ALLOCATOR_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Global Allocator State
 * ============================================================================ */

struct gr_allocators_s {
    void* (*f_malloc)(size_t);
    void* (*f_realloc)(void*, size_t);
    void  (*f_free)(void*);
};

/* Defined in allocator.c */
extern struct gr_allocators_s gr_global_allocators;

/* ============================================================================
 * Inline Allocation Functions (for internal use)
 * 
 * These use the global allocators. For context-specific allocation,
 * use the gr_ctx_* variants below.
 * ============================================================================ */

static inline void* gr_malloc_internal(size_t size) {
    return gr_global_allocators.f_malloc(size);
}

static inline void* gr_realloc_internal(void* ptr, size_t size) {
    return gr_global_allocators.f_realloc(ptr, size);
}

static inline void gr_free_internal(void* ptr) {
    gr_global_allocators.f_free(ptr);
}

static inline void* gr_calloc_internal(size_t count, size_t size) {
    size_t total = count * size;
    void* ptr = gr_global_allocators.f_malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

static inline char* gr_strdup_internal(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* dup = (char*)gr_global_allocators.f_malloc(len);
    if (dup) {
        memcpy(dup, str, len);
    }
    return dup;
}

/* ============================================================================
 * Context-Aware Allocation (prefers context allocator, falls back to global)
 * ============================================================================ */

#include "core.h"  /* For gr_context_s definition */

static inline void* gr_ctx_malloc(gr_context_t* ctx, size_t size) {
    if (ctx && ctx->use_custom_allocators && ctx->allocators.f_malloc) {
        return ctx->allocators.f_malloc(size);
    }
    return gr_global_allocators.f_malloc(size);
}

static inline void* gr_ctx_realloc(gr_context_t* ctx, void* ptr, size_t size) {
    if (ctx && ctx->use_custom_allocators && ctx->allocators.f_realloc) {
        return ctx->allocators.f_realloc(ptr, size);
    }
    return gr_global_allocators.f_realloc(ptr, size);
}

static inline void gr_ctx_free(gr_context_t* ctx, void* ptr) {
    if (!ptr) return;
    if (ctx && ctx->use_custom_allocators && ctx->allocators.f_free) {
        ctx->allocators.f_free(ptr);
    } else {
        gr_global_allocators.f_free(ptr);
    }
}

static inline void* gr_ctx_calloc(gr_context_t* ctx, size_t count, size_t size) {
    size_t total = count * size;
    void* ptr = gr_ctx_malloc(ctx, total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

static inline char* gr_ctx_strdup(gr_context_t* ctx, const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* dup = (char*)gr_ctx_malloc(ctx, len);
    if (dup) {
        memcpy(dup, str, len);
    }
    return dup;
}

/* ============================================================================
 * Convenience Macros
 * ============================================================================ */

/* Allocate array of N elements of type T using global allocator */
#define GR_ALLOC_ARRAY(T, n) \
    ((T*)gr_calloc_internal((n), sizeof(T)))

/* Allocate array using context allocator */
#define GR_CTX_ALLOC_ARRAY(ctx, T, n) \
    ((T*)gr_ctx_calloc((ctx), (n), sizeof(T)))

/* Allocate single struct of type T */
#define GR_ALLOC(T) \
    ((T*)gr_calloc_internal(1, sizeof(T)))

/* Allocate single struct using context allocator */
#define GR_CTX_ALLOC(ctx, T) \
    ((T*)gr_ctx_calloc((ctx), 1, sizeof(T)))

/* Free with context awareness */
#define GR_CTX_FREE(ctx, ptr) \
    do { gr_ctx_free((ctx), (ptr)); (ptr) = NULL; } while(0)

/* Free using global allocator */
#define GR_FREE(ptr) \
    do { gr_free_internal(ptr); (ptr) = NULL; } while(0)

#endif /* GR_INTERNAL_ALLOCATOR_H */
