/**
 * context.c - Context management implementation
 * 
 * The context is the central object that holds:
 *   - Custom allocator configuration
 *   - Pricing engine bridges (mcoptions, fdpricing)
 *   - Numerical parameters (bump size, thread count)
 *   - Error state
 * 
 * Following Ronacher's pattern, we avoid global state by requiring
 * a context for all operations.
 */

#include "georisk.h"
#include "internal/core.h"
#include "internal/allocator.h"
#include "internal/bridge.h"
#include <string.h>

/* ============================================================================
 * Context Creation and Destruction
 * ============================================================================ */

GR_API gr_context_t* gr_context_new(void)
{
    gr_context_t* ctx = GR_ALLOC(gr_context_t);
    if (!ctx) {
        return NULL;
    }
    
    /* Initialize to zero (GR_ALLOC uses calloc) */
    
    /* Set defaults */
    ctx->bump_size = GR_DEFAULT_BUMP;
    ctx->num_threads = 1;
    ctx->last_error = GR_SUCCESS;
    ctx->error_msg[0] = '\0';
    
    /* Allocators start as NULL (use global) */
    ctx->use_custom_allocators = 0;
    
    /* Pricing engines start unloaded */
    ctx->mco_loaded = 0;
    ctx->fdp_loaded = 0;
    ctx->mco_ctx = NULL;
    ctx->fdp_ctx = NULL;
    ctx->mco.handle = NULL;
    ctx->fdp.handle = NULL;
    
    return ctx;
}

GR_API void gr_context_free(gr_context_t* ctx)
{
    if (!ctx) return;
    
    /* Unload pricing engines */
    gr_bridge_unload_mco(ctx);
    gr_bridge_unload_fdp(ctx);
    
    /* Free the context itself using global allocator
     * (context is always allocated with global allocator) */
    gr_free_internal(ctx);
}

/* ============================================================================
 * Pricing Engine Configuration
 * ============================================================================ */

GR_API gr_error_t gr_context_set_mco_library(gr_context_t* ctx, const char* path)
{
    if (!ctx) return GR_ERROR_NULL_POINTER;
    if (!path) return GR_ERROR_NULL_POINTER;
    
    gr_clear_error(ctx);
    return gr_bridge_load_mco(ctx, path);
}

GR_API gr_error_t gr_context_set_fdp_library(gr_context_t* ctx, const char* path)
{
    if (!ctx) return GR_ERROR_NULL_POINTER;
    if (!path) return GR_ERROR_NULL_POINTER;
    
    gr_clear_error(ctx);
    return gr_bridge_load_fdp(ctx, path);
}

/* ============================================================================
 * Numerical Configuration
 * ============================================================================ */

GR_API void gr_context_set_bump_size(gr_context_t* ctx, double bump)
{
    if (!ctx) return;
    
    /* Validate bump size */
    if (bump <= 0.0) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT, 
                     "Bump size must be positive");
        return;
    }
    
    if (bump > 0.1) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT,
                     "Bump size too large (max 0.1)");
        return;
    }
    
    ctx->bump_size = bump;
}

GR_API void gr_context_set_num_threads(gr_context_t* ctx, int threads)
{
    if (!ctx) return;
    
    if (threads < 1) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT,
                     "Thread count must be at least 1");
        return;
    }
    
    ctx->num_threads = threads;
    
    /* Update Monte Carlo engine if loaded */
    if (ctx->mco_loaded && ctx->mco.context_set_num_threads) {
        ctx->mco.context_set_num_threads(ctx->mco_ctx, threads);
    }
}

/* ============================================================================
 * Error Handling
 * ============================================================================ */

GR_API gr_error_t gr_context_get_last_error(const gr_context_t* ctx)
{
    if (!ctx) return GR_ERROR_NULL_POINTER;
    return ctx->last_error;
}

GR_API const char* gr_context_get_error_message(const gr_context_t* ctx)
{
    if (!ctx) return "Null context";
    
    if (ctx->error_msg[0] != '\0') {
        return ctx->error_msg;
    }
    
    return gr_error_string(ctx->last_error);
}

/* ============================================================================
 * Context Allocator Configuration
 * ============================================================================ */

/*
 * Note: Per-context allocators are set directly on the context struct.
 * This is an internal API used by subsystems that need context-aware allocation.
 * 
 * For simplicity, we don't expose a public API for per-context allocators
 * in v0.1. The global gr_set_allocators() is sufficient for most use cases.
 * 
 * If needed, add:
 *   gr_context_set_allocators(ctx, malloc_fn, realloc_fn, free_fn)
 */

/* ============================================================================
 * Convenience: Check if pricing engines are available
 * ============================================================================ */

/*
 * These could be added to the public API if useful:
 *
 * GR_API int gr_context_has_mco(const gr_context_t* ctx) {
 *     return ctx && ctx->mco_loaded;
 * }
 * 
 * GR_API int gr_context_has_fdp(const gr_context_t* ctx) {
 *     return ctx && ctx->fdp_loaded;
 * }
 */
