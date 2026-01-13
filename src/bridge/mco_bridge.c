/**
 * mco_bridge.c - Monte Carlo pricing library bridge
 * 
 * Provides dynamic loading and abstraction for libmcoptions.so.
 * This allows georisk to work with or without the Monte Carlo
 * engine available at runtime.
 * 
 * The bridge uses dlopen/dlsym to load symbols, following Ronacher's
 * principle of clean separation between libraries.
 */

#include "georisk.h"
#include "internal/core.h"
#include "internal/allocator.h"
#include "internal/bridge.h"
#include <string.h>
#include <dlfcn.h>

/* ============================================================================
 * Monte Carlo Engine Configuration
 * ============================================================================ */

/**
 * Configure Monte Carlo simulation parameters.
 */
gr_error_t gr_mco_set_simulations(gr_context_t* ctx, uint32_t num_simulations)
{
    if (!ctx) return GR_ERROR_NULL_POINTER;
    
    if (!ctx->mco_loaded) {
        gr_set_error(ctx, GR_ERROR_NOT_INITIALIZED,
                     "Monte Carlo engine not loaded");
        return GR_ERROR_NOT_INITIALIZED;
    }
    
    if (ctx->mco.context_set_num_simulations) {
        ctx->mco.context_set_num_simulations(ctx->mco_ctx, num_simulations);
    }
    
    return GR_SUCCESS;
}

gr_error_t gr_mco_set_steps(gr_context_t* ctx, uint32_t num_steps)
{
    if (!ctx) return GR_ERROR_NULL_POINTER;
    
    if (!ctx->mco_loaded) {
        gr_set_error(ctx, GR_ERROR_NOT_INITIALIZED,
                     "Monte Carlo engine not loaded");
        return GR_ERROR_NOT_INITIALIZED;
    }
    
    if (ctx->mco.context_set_num_steps) {
        ctx->mco.context_set_num_steps(ctx->mco_ctx, num_steps);
    }
    
    return GR_SUCCESS;
}

gr_error_t gr_mco_set_seed(gr_context_t* ctx, uint64_t seed)
{
    if (!ctx) return GR_ERROR_NULL_POINTER;
    
    if (!ctx->mco_loaded) {
        gr_set_error(ctx, GR_ERROR_NOT_INITIALIZED,
                     "Monte Carlo engine not loaded");
        return GR_ERROR_NOT_INITIALIZED;
    }
    
    if (ctx->mco.context_set_seed) {
        ctx->mco.context_set_seed(ctx->mco_ctx, seed);
    }
    
    return GR_SUCCESS;
}

gr_error_t gr_mco_set_antithetic(gr_context_t* ctx, int enabled)
{
    if (!ctx) return GR_ERROR_NULL_POINTER;
    
    if (!ctx->mco_loaded) {
        gr_set_error(ctx, GR_ERROR_NOT_INITIALIZED,
                     "Monte Carlo engine not loaded");
        return GR_ERROR_NOT_INITIALIZED;
    }
    
    if (ctx->mco.context_set_antithetic) {
        ctx->mco.context_set_antithetic(ctx->mco_ctx, enabled);
    }
    
    return GR_SUCCESS;
}

/* ============================================================================
 * Direct Pricing Functions
 * ============================================================================ */

/**
 * Price a European call option using Monte Carlo.
 */
double gr_mco_european_call(
    gr_context_t* ctx,
    double        spot,
    double        strike,
    double        rate,
    double        volatility,
    double        maturity)
{
    if (!ctx || !ctx->mco_loaded) return 0.0;
    
    if (!ctx->mco.european_call) {
        gr_set_error(ctx, GR_ERROR_NOT_INITIALIZED,
                     "European call not available in mcoptions");
        return 0.0;
    }
    
    return ctx->mco.european_call(
        ctx->mco_ctx,
        spot, strike, rate, volatility, maturity
    );
}

/**
 * Price a European put option using Monte Carlo.
 */
double gr_mco_european_put(
    gr_context_t* ctx,
    double        spot,
    double        strike,
    double        rate,
    double        volatility,
    double        maturity)
{
    if (!ctx || !ctx->mco_loaded) return 0.0;
    
    if (!ctx->mco.european_put) {
        gr_set_error(ctx, GR_ERROR_NOT_INITIALIZED,
                     "European put not available in mcoptions");
        return 0.0;
    }
    
    return ctx->mco.european_put(
        ctx->mco_ctx,
        spot, strike, rate, volatility, maturity
    );
}

/**
 * Price an Asian call option using Monte Carlo.
 */
double gr_mco_asian_call(
    gr_context_t* ctx,
    double        spot,
    double        strike,
    double        rate,
    double        volatility,
    double        maturity)
{
    if (!ctx || !ctx->mco_loaded) return 0.0;
    
    if (!ctx->mco.asian_call) {
        gr_set_error(ctx, GR_ERROR_NOT_INITIALIZED,
                     "Asian call not available in mcoptions");
        return 0.0;
    }
    
    return ctx->mco.asian_call(
        ctx->mco_ctx,
        spot, strike, rate, volatility, maturity
    );
}

/**
 * Price an Asian put option using Monte Carlo.
 */
double gr_mco_asian_put(
    gr_context_t* ctx,
    double        spot,
    double        strike,
    double        rate,
    double        volatility,
    double        maturity)
{
    if (!ctx || !ctx->mco_loaded) return 0.0;
    
    if (!ctx->mco.asian_put) {
        gr_set_error(ctx, GR_ERROR_NOT_INITIALIZED,
                     "Asian put not available in mcoptions");
        return 0.0;
    }
    
    return ctx->mco.asian_put(
        ctx->mco_ctx,
        spot, strike, rate, volatility, maturity
    );
}

/* ============================================================================
 * Query Functions
 * ============================================================================ */

/**
 * Check if Monte Carlo engine is loaded and available.
 */
int gr_mco_is_available(const gr_context_t* ctx)
{
    return ctx && ctx->mco_loaded;
}

/**
 * Check if a specific pricing function is available.
 */
int gr_mco_has_european(const gr_context_t* ctx)
{
    return ctx && ctx->mco_loaded && 
           ctx->mco.european_call && ctx->mco.european_put;
}

int gr_mco_has_asian(const gr_context_t* ctx)
{
    return ctx && ctx->mco_loaded &&
           ctx->mco.asian_call && ctx->mco.asian_put;
}

/* ============================================================================
 * Pricing Function Adapter for State Space
 * ============================================================================ */

/**
 * Create a pricing function suitable for state space mapping.
 * 
 * This wraps the Monte Carlo pricer to work with the gr_pricing_fn
 * interface expected by gr_state_space_map_prices().
 */
typedef struct gr_mco_adapter_data {
    gr_context_t*     ctx;
    gr_option_style_t style;
    gr_option_type_t  type;
    double            strike;
    int               dim_spot;
    int               dim_vol;
    int               dim_rate;
    int               dim_maturity;
    double            default_spot;
    double            default_vol;
    double            default_rate;
    double            default_maturity;
} gr_mco_adapter_data_t;

static double gr_mco_adapter_fn(
    const double* coordinates,
    int           num_dims,
    void*         user_data)
{
    gr_mco_adapter_data_t* data = (gr_mco_adapter_data_t*)user_data;
    (void)num_dims;
    
    double spot = (data->dim_spot >= 0) 
        ? coordinates[data->dim_spot] : data->default_spot;
    double vol = (data->dim_vol >= 0)
        ? coordinates[data->dim_vol] : data->default_vol;
    double rate = (data->dim_rate >= 0)
        ? coordinates[data->dim_rate] : data->default_rate;
    double maturity = (data->dim_maturity >= 0)
        ? coordinates[data->dim_maturity] : data->default_maturity;
    
    if (data->style == GR_STYLE_EUROPEAN) {
        if (data->type == GR_TYPE_CALL) {
            return gr_mco_european_call(data->ctx, spot, data->strike, rate, vol, maturity);
        } else {
            return gr_mco_european_put(data->ctx, spot, data->strike, rate, vol, maturity);
        }
    } else if (data->style == GR_STYLE_ASIAN) {
        if (data->type == GR_TYPE_CALL) {
            return gr_mco_asian_call(data->ctx, spot, data->strike, rate, vol, maturity);
        } else {
            return gr_mco_asian_put(data->ctx, spot, data->strike, rate, vol, maturity);
        }
    }
    
    return 0.0;
}

/**
 * Get the adapter function and create data structure.
 * Caller must free the returned data when done.
 */
gr_pricing_fn gr_mco_get_adapter(
    gr_context_t*      ctx,
    gr_option_style_t  style,
    gr_option_type_t   type,
    double             strike,
    void**             out_data)
{
    if (!ctx || !out_data) return NULL;
    
    gr_mco_adapter_data_t* data = GR_CTX_ALLOC(ctx, gr_mco_adapter_data_t);
    if (!data) return NULL;
    
    data->ctx = ctx;
    data->style = style;
    data->type = type;
    data->strike = strike;
    
    /* Default: all dimensions from coordinates */
    data->dim_spot = 0;
    data->dim_vol = 1;
    data->dim_rate = 2;
    data->dim_maturity = 3;
    
    /* Default values if dimension not in state space */
    data->default_spot = 100.0;
    data->default_vol = 0.2;
    data->default_rate = 0.05;
    data->default_maturity = 1.0;
    
    *out_data = data;
    return gr_mco_adapter_fn;
}

void gr_mco_free_adapter_data(gr_context_t* ctx, void* data)
{
    if (data) {
        gr_ctx_free(ctx, data);
    }
}
