/**
 * internal/bridge.h - Pricing engine bridge definitions
 * 
 * This module provides the interface to external pricing libraries:
 *   - libmcoptions.so (Monte Carlo pricing)
 *   - libfdpricing.so (Finite Difference pricing)
 * 
 * The bridge uses dlopen/dlsym to dynamically load symbols, allowing
 * the georisk library to work with or without the pricing engines
 * available at runtime.
 * 
 * Following Ronacher's pattern, we maintain clean separation between
 * the georisk API and the underlying pricing implementations.
 */

#ifndef GR_INTERNAL_BRIDGE_H
#define GR_INTERNAL_BRIDGE_H

#include "georisk.h"
#include "core.h"
#include <dlfcn.h>

/* ============================================================================
 * Symbol Loading Macros
 * ============================================================================ */

#define GR_LOAD_SYMBOL(handle, vtable, name, type) \
    do { \
        (vtable)->name = (type)dlsym((handle), #name); \
        if (!(vtable)->name) { \
            /* Symbol not found - not fatal, just unavailable */ \
        } \
    } while(0)

#define GR_LOAD_SYMBOL_REQUIRED(handle, vtable, name, type, err_label) \
    do { \
        (vtable)->name = (type)dlsym((handle), #name); \
        if (!(vtable)->name) { \
            goto err_label; \
        } \
    } while(0)

/* ============================================================================
 * Monte Carlo Bridge (libmcoptions.so)
 * ============================================================================ */

/**
 * Load the Monte Carlo pricing library.
 * 
 * Expected symbols (based on your mcoptions library):
 *   - mco_context_new
 *   - mco_context_free
 *   - mco_context_set_seed
 *   - mco_context_set_num_simulations
 *   - mco_context_set_num_steps
 *   - mco_context_set_antithetic
 *   - mco_context_set_num_threads
 *   - mco_european_call
 *   - mco_european_put
 *   - mco_asian_call
 *   - mco_asian_put
 */
static inline gr_error_t gr_bridge_load_mco(
    gr_context_t* ctx,
    const char*   library_path)
{
    if (!ctx || !library_path) {
        return GR_ERROR_NULL_POINTER;
    }
    
    /* Close existing handle if any */
    if (ctx->mco.handle) {
        dlclose(ctx->mco.handle);
        ctx->mco.handle = NULL;
        ctx->mco_loaded = 0;
    }
    
    /* Open library */
    ctx->mco.handle = dlopen(library_path, RTLD_NOW | RTLD_LOCAL);
    if (!ctx->mco.handle) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT, dlerror());
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    /* Load symbols */
    gr_mco_vtable_t* vt = &ctx->mco;
    
    /* Context management - required */
    vt->context_new = (void* (*)(void))dlsym(vt->handle, "mco_context_new");
    vt->context_free = (void (*)(void*))dlsym(vt->handle, "mco_context_free");
    
    if (!vt->context_new || !vt->context_free) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT, 
                     "mcoptions: missing required context functions");
        dlclose(vt->handle);
        vt->handle = NULL;
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    /* Context configuration - optional */
    vt->context_set_seed = (void (*)(void*, uint64_t))
        dlsym(vt->handle, "mco_context_set_seed");
    vt->context_set_num_simulations = (void (*)(void*, uint32_t))
        dlsym(vt->handle, "mco_context_set_num_simulations");
    vt->context_set_num_steps = (void (*)(void*, uint32_t))
        dlsym(vt->handle, "mco_context_set_num_steps");
    vt->context_set_antithetic = (void (*)(void*, int))
        dlsym(vt->handle, "mco_context_set_antithetic");
    vt->context_set_num_threads = (void (*)(void*, int))
        dlsym(vt->handle, "mco_context_set_num_threads");
    
    /* Pricing functions */
    vt->european_call = (double (*)(void*, double, double, double, double, double))
        dlsym(vt->handle, "mco_european_call");
    vt->european_put = (double (*)(void*, double, double, double, double, double))
        dlsym(vt->handle, "mco_european_put");
    vt->asian_call = (double (*)(void*, double, double, double, double, double))
        dlsym(vt->handle, "mco_asian_call");
    vt->asian_put = (double (*)(void*, double, double, double, double, double))
        dlsym(vt->handle, "mco_asian_put");
    
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
        vt->context_set_num_steps(ctx->mco_ctx, 252);  /* Daily steps for 1 year */
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

/**
 * Unload the Monte Carlo library.
 */
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

/**
 * Load the Finite Difference pricing library.
 * 
 * Expected symbols (based on your fdpricing library):
 *   - fdp_context_new
 *   - fdp_context_free
 *   - fdp_price_european_call
 *   - fdp_price_european_put
 *   - fdp_price_american_call
 *   - fdp_price_american_put
 */
static inline gr_error_t gr_bridge_load_fdp(
    gr_context_t* ctx,
    const char*   library_path)
{
    if (!ctx || !library_path) {
        return GR_ERROR_NULL_POINTER;
    }
    
    /* Close existing handle if any */
    if (ctx->fdp.handle) {
        dlclose(ctx->fdp.handle);
        ctx->fdp.handle = NULL;
        ctx->fdp_loaded = 0;
    }
    
    /* Open library */
    ctx->fdp.handle = dlopen(library_path, RTLD_NOW | RTLD_LOCAL);
    if (!ctx->fdp.handle) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT, dlerror());
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    /* Load symbols */
    gr_fdp_vtable_t* vt = &ctx->fdp;
    
    /* Context management - required */
    vt->context_new = (void* (*)(void))dlsym(vt->handle, "fdp_context_new");
    vt->context_free = (void (*)(void*))dlsym(vt->handle, "fdp_context_free");
    
    if (!vt->context_new || !vt->context_free) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT,
                     "fdpricing: missing required context functions");
        dlclose(vt->handle);
        vt->handle = NULL;
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    /* Pricing functions */
    vt->price_european_call = (double (*)(void*, double, double, double, double, double))
        dlsym(vt->handle, "fdp_price_european_call");
    vt->price_european_put = (double (*)(void*, double, double, double, double, double))
        dlsym(vt->handle, "fdp_price_european_put");
    vt->price_american_call = (double (*)(void*, double, double, double, double, double))
        dlsym(vt->handle, "fdp_price_american_call");
    vt->price_american_put = (double (*)(void*, double, double, double, double, double))
        dlsym(vt->handle, "fdp_price_american_put");
    
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

/**
 * Unload the Finite Difference library.
 */
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

/* ============================================================================
 * Unified Pricing Interface
 * 
 * These functions provide a unified interface to price options using
 * whichever engine is available, with automatic fallback.
 * ============================================================================ */

typedef enum gr_pricing_engine {
    GR_ENGINE_AUTO,     /* Use best available */
    GR_ENGINE_MCO,      /* Force Monte Carlo */
    GR_ENGINE_FDP       /* Force Finite Difference */
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

/**
 * Price a vanilla option using the specified or best available engine.
 */
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
    
    /* Determine which engine to use */
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
            /* Prefer FDP for European/American, MCO for Asian */
            if (style == GR_STYLE_ASIAN) {
                use_mco = ctx->mco_loaded;
                if (!use_mco) use_fdp = ctx->fdp_loaded;
            } else {
                use_fdp = ctx->fdp_loaded;
                if (!use_fdp) use_mco = ctx->mco_loaded;
            }
            break;
    }
    
    /* Price using selected engine */
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
                /* FDP doesn't handle Asian well, fall through to MCO */
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
                /* MCO can do American via LSM, but we don't have that symbol yet */
                /* Fall back to European approximation */
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
    
    /* No engine available */
    gr_set_error(ctx, GR_ERROR_PRICING_ENGINE_FAILED, "No pricing engine available");
    return 0.0;
}

/* ============================================================================
 * Adapter for State Space Mapping
 * 
 * This creates a gr_pricing_fn compatible callback that uses the bridge
 * to price options based on coordinates in the state space.
 * ============================================================================ */

typedef struct gr_bridge_pricing_params {
    gr_context_t*       ctx;
    gr_pricing_engine_t engine;
    gr_option_style_t   style;
    gr_option_type_t    type;
    double              strike;     /* Fixed strike */
    
    /* Dimension mapping: which coordinate index maps to which parameter */
    int                 dim_spot;       /* -1 if not in state space */
    int                 dim_volatility;
    int                 dim_rate;
    int                 dim_maturity;
    
    /* Default values for parameters not in state space */
    double              default_spot;
    double              default_volatility;
    double              default_rate;
    double              default_maturity;
} gr_bridge_pricing_params_t;

/**
 * Pricing function adapter for state space mapping.
 */
static inline double gr_bridge_pricing_adapter(
    const double* coordinates,
    int           num_dims,
    void*         user_data)
{
    gr_bridge_pricing_params_t* params = (gr_bridge_pricing_params_t*)user_data;
    (void)num_dims;  /* Used for validation in debug builds */
    
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

#endif /* GR_INTERNAL_BRIDGE_H *//**
 * internal/bridge.h - Pricing engine bridge definitions
 * 
 * This module provides the interface to external pricing libraries:
 *   - libmcoptions.so (Monte Carlo pricing)
 *   - libfdpricing.so (Finite Difference pricing)
 * 
 * The bridge uses dlopen/dlsym to dynamically load symbols, allowing
 * the georisk library to work with or without the pricing engines
 * available at runtime.
 * 
 * Following Ronacher's pattern, we maintain clean separation between
 * the georisk API and the underlying pricing implementations.
 */

#ifndef GR_INTERNAL_BRIDGE_H
#define GR_INTERNAL_BRIDGE_H

#include "georisk.h"
#include "core.h"
#include <dlfcn.h>

/* ============================================================================
 * Symbol Loading Macros
 * ============================================================================ */

#define GR_LOAD_SYMBOL(handle, vtable, name, type) \
    do { \
        (vtable)->name = (type)dlsym((handle), #name); \
        if (!(vtable)->name) { \
            /* Symbol not found - not fatal, just unavailable */ \
        } \
    } while(0)

#define GR_LOAD_SYMBOL_REQUIRED(handle, vtable, name, type, err_label) \
    do { \
        (vtable)->name = (type)dlsym((handle), #name); \
        if (!(vtable)->name) { \
            goto err_label; \
        } \
    } while(0)

/* ============================================================================
 * Monte Carlo Bridge (libmcoptions.so)
 * ============================================================================ */

/**
 * Load the Monte Carlo pricing library.
 * 
 * Expected symbols (based on your mcoptions library):
 *   - mco_context_new
 *   - mco_context_free
 *   - mco_context_set_seed
 *   - mco_context_set_num_simulations
 *   - mco_context_set_num_steps
 *   - mco_context_set_antithetic
 *   - mco_context_set_num_threads
 *   - mco_european_call
 *   - mco_european_put
 *   - mco_asian_call
 *   - mco_asian_put
 */
static inline gr_error_t gr_bridge_load_mco(
    gr_context_t* ctx,
    const char*   library_path)
{
    if (!ctx || !library_path) {
        return GR_ERROR_NULL_POINTER;
    }
    
    /* Close existing handle if any */
    if (ctx->mco.handle) {
        dlclose(ctx->mco.handle);
        ctx->mco.handle = NULL;
        ctx->mco_loaded = 0;
    }
    
    /* Open library */
    ctx->mco.handle = dlopen(library_path, RTLD_NOW | RTLD_LOCAL);
    if (!ctx->mco.handle) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT, dlerror());
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    /* Load symbols */
    gr_mco_vtable_t* vt = &ctx->mco;
    
    /* Context management - required */
    vt->context_new = (void* (*)(void))dlsym(vt->handle, "mco_context_new");
    vt->context_free = (void (*)(void*))dlsym(vt->handle, "mco_context_free");
    
    if (!vt->context_new || !vt->context_free) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT, 
                     "mcoptions: missing required context functions");
        dlclose(vt->handle);
        vt->handle = NULL;
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    /* Context configuration - optional */
    vt->context_set_seed = (void (*)(void*, uint64_t))
        dlsym(vt->handle, "mco_context_set_seed");
    vt->context_set_num_simulations = (void (*)(void*, uint32_t))
        dlsym(vt->handle, "mco_context_set_num_simulations");
    vt->context_set_num_steps = (void (*)(void*, uint32_t))
        dlsym(vt->handle, "mco_context_set_num_steps");
    vt->context_set_antithetic = (void (*)(void*, int))
        dlsym(vt->handle, "mco_context_set_antithetic");
    vt->context_set_num_threads = (void (*)(void*, int))
        dlsym(vt->handle, "mco_context_set_num_threads");
    
    /* Pricing functions */
    vt->european_call = (double (*)(void*, double, double, double, double, double))
        dlsym(vt->handle, "mco_european_call");
    vt->european_put = (double (*)(void*, double, double, double, double, double))
        dlsym(vt->handle, "mco_european_put");
    vt->asian_call = (double (*)(void*, double, double, double, double, double))
        dlsym(vt->handle, "mco_asian_call");
    vt->asian_put = (double (*)(void*, double, double, double, double, double))
        dlsym(vt->handle, "mco_asian_put");
    
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
        vt->context_set_num_steps(ctx->mco_ctx, 252);  /* Daily steps for 1 year */
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

/**
 * Unload the Monte Carlo library.
 */
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

/**
 * Load the Finite Difference pricing library.
 * 
 * Expected symbols (based on your fdpricing library):
 *   - fdp_context_new
 *   - fdp_context_free
 *   - fdp_price_european_call
 *   - fdp_price_european_put
 *   - fdp_price_american_call
 *   - fdp_price_american_put
 */
static inline gr_error_t gr_bridge_load_fdp(
    gr_context_t* ctx,
    const char*   library_path)
{
    if (!ctx || !library_path) {
        return GR_ERROR_NULL_POINTER;
    }
    
    /* Close existing handle if any */
    if (ctx->fdp.handle) {
        dlclose(ctx->fdp.handle);
        ctx->fdp.handle = NULL;
        ctx->fdp_loaded = 0;
    }
    
    /* Open library */
    ctx->fdp.handle = dlopen(library_path, RTLD_NOW | RTLD_LOCAL);
    if (!ctx->fdp.handle) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT, dlerror());
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    /* Load symbols */
    gr_fdp_vtable_t* vt = &ctx->fdp;
    
    /* Context management - required */
    vt->context_new = (void* (*)(void))dlsym(vt->handle, "fdp_context_new");
    vt->context_free = (void (*)(void*))dlsym(vt->handle, "fdp_context_free");
    
    if (!vt->context_new || !vt->context_free) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT,
                     "fdpricing: missing required context functions");
        dlclose(vt->handle);
        vt->handle = NULL;
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    /* Pricing functions */
    vt->price_european_call = (double (*)(void*, double, double, double, double, double))
        dlsym(vt->handle, "fdp_price_european_call");
    vt->price_european_put = (double (*)(void*, double, double, double, double, double))
        dlsym(vt->handle, "fdp_price_european_put");
    vt->price_american_call = (double (*)(void*, double, double, double, double, double))
        dlsym(vt->handle, "fdp_price_american_call");
    vt->price_american_put = (double (*)(void*, double, double, double, double, double))
        dlsym(vt->handle, "fdp_price_american_put");
    
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

/**
 * Unload the Finite Difference library.
 */
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

/* ============================================================================
 * Unified Pricing Interface
 * 
 * These functions provide a unified interface to price options using
 * whichever engine is available, with automatic fallback.
 * ============================================================================ */

typedef enum gr_pricing_engine {
    GR_ENGINE_AUTO,     /* Use best available */
    GR_ENGINE_MCO,      /* Force Monte Carlo */
    GR_ENGINE_FDP       /* Force Finite Difference */
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

/**
 * Price a vanilla option using the specified or best available engine.
 */
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
    
    /* Determine which engine to use */
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
            /* Prefer FDP for European/American, MCO for Asian */
            if (style == GR_STYLE_ASIAN) {
                use_mco = ctx->mco_loaded;
                if (!use_mco) use_fdp = ctx->fdp_loaded;
            } else {
                use_fdp = ctx->fdp_loaded;
                if (!use_fdp) use_mco = ctx->mco_loaded;
            }
            break;
    }
    
    /* Price using selected engine */
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
                /* FDP doesn't handle Asian well, fall through to MCO */
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
                /* MCO can do American via LSM, but we don't have that symbol yet */
                /* Fall back to European approximation */
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
    
    /* No engine available */
    gr_set_error(ctx, GR_ERROR_PRICING_ENGINE_FAILED, "No pricing engine available");
    return 0.0;
}

/* ============================================================================
 * Adapter for State Space Mapping
 * 
 * This creates a gr_pricing_fn compatible callback that uses the bridge
 * to price options based on coordinates in the state space.
 * ============================================================================ */

typedef struct gr_bridge_pricing_params {
    gr_context_t*       ctx;
    gr_pricing_engine_t engine;
    gr_option_style_t   style;
    gr_option_type_t    type;
    double              strike;     /* Fixed strike */
    
    /* Dimension mapping: which coordinate index maps to which parameter */
    int                 dim_spot;       /* -1 if not in state space */
    int                 dim_volatility;
    int                 dim_rate;
    int                 dim_maturity;
    
    /* Default values for parameters not in state space */
    double              default_spot;
    double              default_volatility;
    double              default_rate;
    double              default_maturity;
} gr_bridge_pricing_params_t;

/**
 * Pricing function adapter for state space mapping.
 */
static inline double gr_bridge_pricing_adapter(
    const double* coordinates,
    int           num_dims,
    void*         user_data)
{
    gr_bridge_pricing_params_t* params = (gr_bridge_pricing_params_t*)user_data;
    (void)num_dims;  /* Used for validation in debug builds */
    
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
