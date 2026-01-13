/**
 * fdp_bridge.c - Finite Difference pricing library bridge
 * 
 * Provides dynamic loading and abstraction for libfdpricing.so.
 * The FD engine is preferred for European and American options
 * due to higher accuracy and deterministic results.
 */

#include "georisk.h"
#include "internal/core.h"
#include "internal/allocator.h"
#include "internal/bridge.h"
#include <string.h>
#include <dlfcn.h>

/* ============================================================================
 * Direct Pricing Functions
 * ============================================================================ */

/**
 * Price a European call option using Finite Difference.
 */
double gr_fdp_european_call(
    gr_context_t* ctx,
    double        spot,
    double        strike,
    double        rate,
    double        volatility,
    double        maturity)
{
    if (!ctx || !ctx->fdp_loaded) return 0.0;
    
    if (!ctx->fdp.price_european_call) {
        gr_set_error(ctx, GR_ERROR_NOT_INITIALIZED,
                     "European call not available in fdpricing");
        return 0.0;
    }
    
    return ctx->fdp.price_european_call(
        ctx->fdp_ctx,
        spot, strike, rate, volatility, maturity
    );
}

/**
 * Price a European put option using Finite Difference.
 */
double gr_fdp_european_put(
    gr_context_t* ctx,
    double        spot,
    double        strike,
    double        rate,
    double        volatility,
    double        maturity)
{
    if (!ctx || !ctx->fdp_loaded) return 0.0;
    
    if (!ctx->fdp.price_european_put) {
        gr_set_error(ctx, GR_ERROR_NOT_INITIALIZED,
                     "European put not available in fdpricing");
        return 0.0;
    }
    
    return ctx->fdp.price_european_put(
        ctx->fdp_ctx,
        spot, strike, rate, volatility, maturity
    );
}

/**
 * Price an American call option using Finite Difference.
 */
double gr_fdp_american_call(
    gr_context_t* ctx,
    double        spot,
    double        strike,
    double        rate,
    double        volatility,
    double        maturity)
{
    if (!ctx || !ctx->fdp_loaded) return 0.0;
    
    if (!ctx->fdp.price_american_call) {
        gr_set_error(ctx, GR_ERROR_NOT_INITIALIZED,
                     "American call not available in fdpricing");
        return 0.0;
    }
    
    return ctx->fdp.price_american_call(
        ctx->fdp_ctx,
        spot, strike, rate, volatility, maturity
    );
}

/**
 * Price an American put option using Finite Difference.
 */
double gr_fdp_american_put(
    gr_context_t* ctx,
    double        spot,
    double        strike,
    double        rate,
    double        volatility,
    double        maturity)
{
    if (!ctx || !ctx->fdp_loaded) return 0.0;
    
    if (!ctx->fdp.price_american_put) {
        gr_set_error(ctx, GR_ERROR_NOT_INITIALIZED,
                     "American put not available in fdpricing");
        return 0.0;
    }
    
    return ctx->fdp.price_american_put(
        ctx->fdp_ctx,
        spot, strike, rate, volatility, maturity
    );
}

/* ============================================================================
 * Query Functions
 * ============================================================================ */

/**
 * Check if Finite Difference engine is loaded and available.
 */
int gr_fdp_is_available(const gr_context_t* ctx)
{
    return ctx && ctx->fdp_loaded;
}

/**
 * Check if specific pricing functions are available.
 */
int gr_fdp_has_european(const gr_context_t* ctx)
{
    return ctx && ctx->fdp_loaded &&
           ctx->fdp.price_european_call && ctx->fdp.price_european_put;
}

int gr_fdp_has_american(const gr_context_t* ctx)
{
    return ctx && ctx->fdp_loaded &&
           ctx->fdp.price_american_call && ctx->fdp.price_american_put;
}

/* ============================================================================
 * Pricing Function Adapter for State Space
 * ============================================================================ */

typedef struct gr_fdp_adapter_data {
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
} gr_fdp_adapter_data_t;

static double gr_fdp_adapter_fn(
    const double* coordinates,
    int           num_dims,
    void*         user_data)
{
    gr_fdp_adapter_data_t* data = (gr_fdp_adapter_data_t*)user_data;
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
            return gr_fdp_european_call(data->ctx, spot, data->strike, rate, vol, maturity);
        } else {
            return gr_fdp_european_put(data->ctx, spot, data->strike, rate, vol, maturity);
        }
    } else if (data->style == GR_STYLE_AMERICAN) {
        if (data->type == GR_TYPE_CALL) {
            return gr_fdp_american_call(data->ctx, spot, data->strike, rate, vol, maturity);
        } else {
            return gr_fdp_american_put(data->ctx, spot, data->strike, rate, vol, maturity);
        }
    }
    
    return 0.0;
}

/**
 * Get the adapter function and create data structure.
 */
gr_pricing_fn gr_fdp_get_adapter(
    gr_context_t*      ctx,
    gr_option_style_t  style,
    gr_option_type_t   type,
    double             strike,
    void**             out_data)
{
    if (!ctx || !out_data) return NULL;
    
    gr_fdp_adapter_data_t* data = GR_CTX_ALLOC(ctx, gr_fdp_adapter_data_t);
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
    
    /* Default values */
    data->default_spot = 100.0;
    data->default_vol = 0.2;
    data->default_rate = 0.05;
    data->default_maturity = 1.0;
    
    *out_data = data;
    return gr_fdp_adapter_fn;
}

void gr_fdp_free_adapter_data(gr_context_t* ctx, void* data)
{
    if (data) {
        gr_ctx_free(ctx, data);
    }
}

/* ============================================================================
 * High-Level Analysis Integration
 * ============================================================================ */

/**
 * Compute Greeks using finite differences on the FD pricer.
 * This gives us "Greeks of Greeks" - sensitivity of sensitivities.
 */
gr_error_t gr_fdp_compute_greeks(
    gr_context_t*     ctx,
    gr_option_style_t style,
    gr_option_type_t  type,
    double            spot,
    double            strike,
    double            rate,
    double            volatility,
    double            maturity,
    double*           out_price,
    double*           out_delta,
    double*           out_gamma,
    double*           out_vega,
    double*           out_theta,
    double*           out_rho)
{
    if (!ctx) return GR_ERROR_NULL_POINTER;
    if (!ctx->fdp_loaded) {
        gr_set_error(ctx, GR_ERROR_NOT_INITIALIZED,
                     "Finite Difference engine not loaded");
        return GR_ERROR_NOT_INITIALIZED;
    }
    
    double h = ctx->bump_size;
    
    /* Select pricing function */
    double (*price_fn)(gr_context_t*, double, double, double, double, double);
    
    if (style == GR_STYLE_EUROPEAN) {
        price_fn = (type == GR_TYPE_CALL) ? gr_fdp_european_call : gr_fdp_european_put;
    } else if (style == GR_STYLE_AMERICAN) {
        price_fn = (type == GR_TYPE_CALL) ? gr_fdp_american_call : gr_fdp_american_put;
    } else {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT, "Unsupported option style");
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    /* Base price */
    double price = price_fn(ctx, spot, strike, rate, volatility, maturity);
    if (out_price) *out_price = price;
    
    /* Delta: ∂V/∂S */
    if (out_delta) {
        double h_spot = h * spot;
        double v_up = price_fn(ctx, spot + h_spot, strike, rate, volatility, maturity);
        double v_dn = price_fn(ctx, spot - h_spot, strike, rate, volatility, maturity);
        *out_delta = (v_up - v_dn) / (2.0 * h_spot);
    }
    
    /* Gamma: ∂²V/∂S² */
    if (out_gamma) {
        double h_spot = h * spot;
        double v_up = price_fn(ctx, spot + h_spot, strike, rate, volatility, maturity);
        double v_dn = price_fn(ctx, spot - h_spot, strike, rate, volatility, maturity);
        *out_gamma = (v_up - 2.0 * price + v_dn) / (h_spot * h_spot);
    }
    
    /* Vega: ∂V/∂σ (per 1% vol move) */
    if (out_vega) {
        double h_vol = 0.01;  /* 1% vol bump */
        double v_up = price_fn(ctx, spot, strike, rate, volatility + h_vol, maturity);
        double v_dn = price_fn(ctx, spot, strike, rate, volatility - h_vol, maturity);
        *out_vega = (v_up - v_dn) / 2.0;  /* Per 1% move */
    }
    
    /* Theta: -∂V/∂T (per day) */
    if (out_theta) {
        double h_time = 1.0 / 365.0;  /* 1 day */
        if (maturity > h_time) {
            double v_later = price_fn(ctx, spot, strike, rate, volatility, maturity - h_time);
            *out_theta = v_later - price;  /* P&L from 1 day passing */
        } else {
            *out_theta = 0.0;
        }
    }
    
    /* Rho: ∂V/∂r (per 1% rate move) */
    if (out_rho) {
        double h_rate = 0.01;  /* 1% rate bump */
        double v_up = price_fn(ctx, spot, strike, rate + h_rate, volatility, maturity);
        double v_dn = price_fn(ctx, spot, strike, rate - h_rate, volatility, maturity);
        *out_rho = (v_up - v_dn) / 2.0;  /* Per 1% move */
    }
    
    return GR_SUCCESS;
}
