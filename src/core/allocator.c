/**
 * allocator.c - Global allocator implementation
 * 
 * Provides customizable memory allocation following Ronacher's pattern.
 * Users can override malloc/realloc/free globally or per-context.
 */

#include "georisk.h"
#include "internal/allocator.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Global Allocator State
 * ============================================================================ */

struct gr_allocators_s gr_global_allocators = {
    .f_malloc  = malloc,
    .f_realloc = realloc,
    .f_free    = free
};

/* ============================================================================
 * Public API - Global Allocator Configuration
 * ============================================================================ */

GR_API void gr_set_allocators(
    void* (*f_malloc)(size_t),
    void* (*f_realloc)(void*, size_t),
    void  (*f_free)(void*))
{
    if (f_malloc && f_realloc && f_free) {
        gr_global_allocators.f_malloc  = f_malloc;
        gr_global_allocators.f_realloc = f_realloc;
        gr_global_allocators.f_free    = f_free;
    } else {
        /* Reset to defaults */
        gr_global_allocators.f_malloc  = malloc;
        gr_global_allocators.f_realloc = realloc;
        gr_global_allocators.f_free    = free;
    }
}

/* ============================================================================
 * Public API - Allocation Functions
 * ============================================================================ */

GR_API void* gr_malloc(size_t size)
{
    if (size == 0) return NULL;
    return gr_global_allocators.f_malloc(size);
}

GR_API void* gr_realloc(void* ptr, size_t size)
{
    return gr_global_allocators.f_realloc(ptr, size);
}

GR_API void* gr_calloc(size_t count, size_t size)
{
    size_t total = count * size;
    if (total == 0) return NULL;
    
    void* ptr = gr_global_allocators.f_malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

GR_API void gr_free(void* ptr)
{
    if (ptr) {
        gr_global_allocators.f_free(ptr);
    }
}

GR_API char* gr_strdup(const char* str)
{
    if (!str) return NULL;
    
    size_t len = strlen(str) + 1;
    char* dup = (char*)gr_global_allocators.f_malloc(len);
    if (dup) {
        memcpy(dup, str, len);
    }
    return dup;
}
