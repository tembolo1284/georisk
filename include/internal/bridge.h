/**
 * internal/bridge.h - Pricing engine bridge definitions
 * 
 * This module provides the interface to external pricing libraries:
 *   - libmcoptions.so (Monte Carlo pricing)
 *   - libfdpricing.so (Finite Difference pricing)
 */

#ifndef GR_INTERNAL_BRIDGE_H
#define GR_INTERNAL_BRIDGE_H

#include "georisk.h"
#include "core.h"
#include <dlfcn.h>

/* 
 * Suppress pedantic warning for dlsym casts.
 * POSIX requires casting void* to function pointers, which ISO C forbids.
 * This is standard practice for any code using dlsym.
 */
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

/* ============================================================================
 * Monte Carlo Bridge (libmcoptions.so)
 * ============================================================================ */

static inline gr_error_t gr_bridge_load_mco(
    gr_context_t* ctx,
    const char*   library_path)
{
    if (!ctx || !library_path) {
        return GR_ERROR_NULL_POINTER;
    }
    
    if (ctx->mco.handle) {
        dlclose(ctx->mco.handle);
        ctx->mco.handle = NULL;
        ctx->mco_loaded = 0;
    }
    
    ctx->mco.handle = dlopen(library_path, RTLD_NOW | RTLD_LOCAL);
    if (!ctx->mco.handle) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT, dlerror());
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    gr_mco_vtable_t* vt = &ctx->mco;
    
    /* Context management - required */
    *(void**)(&vt->context_new) = dlsym(vt->handle, "mco_context_new");
    *(void**)(&vt->context_free) = dlsym(vt->handle, "mco_context_free");
    
    if (!vt->context_new || !vt->context_free) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT, 
                     "mcoptions: missing required context functions");
        dlclose(vt->handle);
        vt->handle = NULL;
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    /* Context configuration - optional */
    *(void**)(&vt->context_set_seed) = dlsym(vt->handle, "mco_context_set_seed");
    *(void**)(&vt->context_set_num_simulations) = dlsym(vt->handle, "mco_context_set_num_simulations");
    *(void**)(&vt->context_set_num_steps) = dlsym(vt->handle, "mco_context_set_num_steps");
    *(void**)(&vt->context_set_antithetic) = dlsym(vt->handle, "mco_context_set_antithetic");
    *(void**)(&vt->context_set_num_threads) = dlsym(vt->handle, "mco_context_set_num_threads");
    
    /* Pricing functions */
    *(void**)(&vt->european_call) = dlsym(vt->handle, "mco_european_call");
    *(void**)(&vt->european_put) = dlsym(vt->handle, "mco_european_put");
    *(void**)(&vt->asian_call) = dlsym(vt->handle, "mco_asian_call");
    *(void**)(&vt->asian_put) = dlsym(vt->handle, "mco_asian_put");
    
    /* Create internal pricing context */
    ctx->mco_ctx = vt->context_new();
    if (!ctx->mco_ctx) {
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                     "mcoptions: failed to create context");
        dlclose(vt->handle);
        vt->handle = NULL;
        return GR_ERROR_OUT_OF_MEMORY;
    }
    
    /* Configure defaults */
    if (vt->context_set_num_simulations) {
        vt->context_set_num_simulations(ctx->mco_ctx, 100000);
    }
    if (vt->context_set_num_steps) {
        vt->context_set_num_steps(ctx->mco_ctx, 252);
    }
    if (vt->context_set_antithetic) {
        vt->context_set_antithetic(ctx->mco_ctx, 1);
    }
    if (vt->context_set_num_threads && ctx->num_threads > 0) {
        vt->context_set_num_threads(ctx->mco_ctx, ctx->num_threads);
    }
    
    ctx->mco_loaded = 1;
    return GR_SUCCESS;
}

static inline void gr_bridge_unload_mco(gr_context_t* ctx)
{
    if (!ctx) return;
    
    if (ctx->mco_ctx && ctx->mco.context_free) {
        ctx->mco.context_free(ctx->mco_ctx);
        ctx->mco_ctx = NULL;
    }
    
    if (ctx->mco.handle) {
        dlclose(ctx->mco.handle);
        ctx->mco.handle = NULL;
    }
    
    ctx->mco_loaded = 0;
}

/* ============================================================================
 * Finite Difference Bridge (libfdpricing.so)
 * ============================================================================ */

static inline gr_error_t gr_bridge_load_fdp(
    gr_context_t* ctx,
    const char*   library_path)
{
    if (!ctx || !library_path) {
        return GR_ERROR_NULL_POINTER;
    }
    
    if (ctx->fdp.handle) {
        dlclose(ctx->fdp.handle);
        ctx->fdp.handle = NULL;
        ctx->fdp_loaded = 0;
    }
    
    ctx->fdp.handle = dlopen(library_path, RTLD_NOW | RTLD_LOCAL);
    if (!ctx->fdp.handle) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT, dlerror());
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    gr_fdp_vtable_t* vt = &ctx->fdp;
    
    /* Context management - required */
    *(void**)(&vt->context_new) = dlsym(vt->handle, "fdp_context_new");
    *(void**)(&vt->context_free) = dlsym(vt->handle, "fdp_context_free");
    
    if (!vt->context_new || !vt->context_free) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT,
                     "fdpricing: missing required context functions");
        dlclose(vt->handle);
        vt->handle = NULL;
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    /* Pricing functions */
    *(void**)(&vt->price_european_call) = dlsym(vt->handle, "fdp_price_european_call");
    *(void**)(&vt->price_european_put) = dlsym(vt->handle, "fdp_price_european_put");
    *(void**)(&vt->price_american_call) = dlsym(vt->handle, "fdp_price_american_call");
    *(void**)(&vt->price_american_put) = dlsym(vt->handle, "fdp_price_american_put");
    
    /* Create internal pricing context */
    ctx->fdp_ctx = vt->context_new();
    if (!ctx->fdp_ctx) {
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                     "fdpricing: failed to create context");
        dlclose(vt->handle);
        vt->handle = NULL;
        return GR_ERROR_OUT_OF_MEMORY;
    }
    
    ctx->fdp_loaded = 1;
    return GR_SUCCESS;
}

static inline void gr_bridge_unload_fdp(gr_context_t* ctx)
{
    if (!ctx) return;
    
    if (ctx->fdp_ctx && ctx->fdp.context_free) {
        ctx->fdp.context_free(ctx->fdp_ctx);
        ctx->fdp_ctx = NULL;
    }
    
    if (ctx->fdp.handle) {
        dlclose(ctx->fdp.handle);
        ctx->fdp.handle = NULL;
    }
    
    ctx->fdp_loaded = 0;
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

/* ============================================================================
 * Unified Pricing Interface
 * ============================================================================ */

typedef enum gr_pricing_engine {
    GR_ENGINE_AUTO,
    GR_ENGINE_MCO,
    GR_ENGINE_FDP
} gr_pricing_engine_t;

typedef enum gr_option_style {
    GR_STYLE_EUROPEAN,
    GR_STYLE_AMERICAN,
    GR_STYLE_ASIAN
} gr_option_style_t;

typedef enum gr_option_type {
    GR_TYPE_CALL,
    GR_TYPE_PUT
} gr_option_type_t;

static inline double gr_bridge_price_vanilla(
    gr_context_t*       ctx,
    gr_pricing_engine_t engine,
    gr_option_style_t   style,
    gr_option_type_t    type,
    double              spot,
    double              strike,
    double              rate,
    double              volatility,
    double              maturity)
{
    if (!ctx) return 0.0;
    
    int use_mco = 0;
    int use_fdp = 0;
    
    switch (engine) {
        case GR_ENGINE_MCO:
            use_mco = ctx->mco_loaded;
            break;
        case GR_ENGINE_FDP:
            use_fdp = ctx->fdp_loaded;
            break;
        case GR_ENGINE_AUTO:
        default:
            if (style == GR_STYLE_ASIAN) {
                use_mco = ctx->mco_loaded;
                if (!use_mco) use_fdp = ctx->fdp_loaded;
            } else {
                use_fdp = ctx->fdp_loaded;
                if (!use_fdp) use_mco = ctx->mco_loaded;
            }
            break;
    }
    
    if (use_fdp && ctx->fdp_loaded) {
        gr_fdp_vtable_t* vt = &ctx->fdp;
        
        switch (style) {
            case GR_STYLE_EUROPEAN:
                if (type == GR_TYPE_CALL && vt->price_european_call) {
                    return vt->price_european_call(ctx->fdp_ctx, spot, strike, rate, volatility, maturity);
                }
                if (type == GR_TYPE_PUT && vt->price_european_put) {
                    return vt->price_european_put(ctx->fdp_ctx, spot, strike, rate, volatility, maturity);
                }
                break;
                
            case GR_STYLE_AMERICAN:
                if (type == GR_TYPE_CALL && vt->price_american_call) {
                    return vt->price_american_call(ctx->fdp_ctx, spot, strike, rate, volatility, maturity);
                }
                if (type == GR_TYPE_PUT && vt->price_american_put) {
                    return vt->price_american_put(ctx->fdp_ctx, spot, strike, rate, volatility, maturity);
                }
                break;
                
            case GR_STYLE_ASIAN:
                break;
        }
    }
    
    if (use_mco && ctx->mco_loaded) {
        gr_mco_vtable_t* vt = &ctx->mco;
        
        switch (style) {
            case GR_STYLE_EUROPEAN:
                if (type == GR_TYPE_CALL && vt->european_call) {
                    return vt->european_call(ctx->mco_ctx, spot, strike, rate, volatility, maturity);
                }
                if (type == GR_TYPE_PUT && vt->european_put) {
                    return vt->european_put(ctx->mco_ctx, spot, strike, rate, volatility, maturity);
                }
                break;
                
            case GR_STYLE_AMERICAN:
                if (type == GR_TYPE_CALL && vt->european_call) {
                    return vt->european_call(ctx->mco_ctx, spot, strike, rate, volatility, maturity);
                }
                if (type == GR_TYPE_PUT && vt->european_put) {
                    return vt->european_put(ctx->mco_ctx, spot, strike, rate, volatility, maturity);
                }
                break;
                
            case GR_STYLE_ASIAN:
                if (type == GR_TYPE_CALL && vt->asian_call) {
                    return vt->asian_call(ctx->mco_ctx, spot, strike, rate, volatility, maturity);
                }
                if (type == GR_TYPE_PUT && vt->asian_put) {
                    return vt->asian_put(ctx->mco_ctx, spot, strike, rate, volatility, maturity);
                }
                break;
        }
    }
    
    gr_set_error(ctx, GR_ERROR_PRICING_ENGINE_FAILED, "No pricing engine available");
    return 0.0;
}

/* ============================================================================
 * Adapter for State Space Mapping
 * ============================================================================ */

typedef struct gr_bridge_pricing_params {
    gr_context_t*       ctx;
    gr_pricing_engine_t engine;
    gr_option_style_t   style;
    gr_option_type_t    type;
    double              strike;
    int                 dim_spot;
    int                 dim_volatility;
    int                 dim_rate;
    int                 dim_maturity;
    double              default_spot;
    double              default_volatility;
    double              default_rate;
    double              default_maturity;
} gr_bridge_pricing_params_t;

static inline double gr_bridge_pricing_adapter(
    const double* coordinates,
    int           num_dims,
    void*         user_data)
{
    gr_bridge_pricing_params_t* params = (gr_bridge_pricing_params_t*)user_data;
    (void)num_dims;
    
    double spot = (params->dim_spot >= 0) 
        ? coordinates[params->dim_spot] 
        : params->default_spot;
    
    double vol = (params->dim_volatility >= 0)
        ? coordinates[params->dim_volatility]
        : params->default_volatility;
    
    double rate = (params->dim_rate >= 0)
        ? coordinates[params->dim_rate]
        : params->default_rate;
    
    double maturity = (params->dim_maturity >= 0)
        ? coordinates[params->dim_maturity]
        : params->default_maturity;
    
    return gr_bridge_price_vanilla(
        params->ctx,
        params->engine,
        params->style,
        params->type,
        spot,
        params->strike,
        rate,
        vol,
        maturity
    );
}

#endif /* GR_INTERNAL_BRIDGE_H */
