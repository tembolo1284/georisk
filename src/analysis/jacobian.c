/**
 * jacobian.c - Jacobian (gradient) computation
 * 
 * The Jacobian captures first-order sensitivity of the pricing function
 * to each dimension of the state space. It answers:
 * 
 *   "If I move slightly in direction i, how much does the price change?"
 * 
 * This is the continuous generalization of the Greeks:
 *   - ∂V/∂S is Delta
 *   - ∂V/∂σ is Vega
 *   - ∂V/∂r is Rho
 *   - etc.
 * 
 * The gradient magnitude tells us overall sensitivity at a point.
 * The gradient direction tells us the direction of steepest price change.
 */

#include "georisk.h"
#include "internal/core.h"
#include "internal/allocator.h"
#include "internal/jacobian.h"
#include "internal/state_space.h"
#include <string.h>
#include <math.h>

/* ============================================================================
 * Jacobian Creation and Destruction
 * ============================================================================ */

GR_API gr_jacobian_t* gr_jacobian_new(gr_context_t* ctx, int num_dims)
{
    if (!ctx) return NULL;
    
    if (num_dims < 1 || num_dims > GR_MAX_DIMENSIONS) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT,
                     "Invalid number of dimensions");
        return NULL;
    }
    
    gr_jacobian_t* jac = GR_CTX_ALLOC(ctx, gr_jacobian_t);
    if (!jac) {
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                     "Failed to allocate Jacobian");
        return NULL;
    }
    
    jac->ctx = ctx;
    jac->num_dims = num_dims;
    jac->valid = 0;
    jac->value = 0.0;
    
    /* Allocate partial derivatives array */
    jac->partials = (double*)gr_ctx_calloc(ctx, (size_t)num_dims, sizeof(double));
    if (!jac->partials) {
        gr_ctx_free(ctx, jac);
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                     "Failed to allocate Jacobian partials");
        return NULL;
    }
    
    /* Allocate point storage */
    jac->point = (double*)gr_ctx_calloc(ctx, (size_t)num_dims, sizeof(double));
    if (!jac->point) {
        gr_ctx_free(ctx, jac->partials);
        gr_ctx_free(ctx, jac);
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                     "Failed to allocate Jacobian point");
        return NULL;
    }
    
    return jac;
}

GR_API void gr_jacobian_free(gr_jacobian_t* jac)
{
    if (!jac) return;
    
    gr_context_t* ctx = jac->ctx;
    
    if (jac->partials) {
        gr_ctx_free(ctx, jac->partials);
    }
    
    if (jac->point) {
        gr_ctx_free(ctx, jac->point);
    }
    
    gr_ctx_free(ctx, jac);
}

/* ============================================================================
 * Jacobian Computation
 * ============================================================================ */

GR_API gr_error_t gr_jacobian_compute(
    gr_jacobian_t*          jac,
    const gr_state_space_t* space,
    const double*           point)
{
    if (!jac) return GR_ERROR_NULL_POINTER;
    if (!space) return GR_ERROR_NULL_POINTER;
    if (!point) return GR_ERROR_NULL_POINTER;
    
    gr_context_t* ctx = jac->ctx;
    
    /* Validate dimensions match */
    if (jac->num_dims != space->num_dims) {
        gr_set_error(ctx, GR_ERROR_DIMENSION_MISMATCH,
                     "Jacobian dimensions don't match state space");
        return GR_ERROR_DIMENSION_MISMATCH;
    }
    
    /* Check that prices have been computed */
    if (!space->prices_valid) {
        gr_set_error(ctx, GR_ERROR_NOT_INITIALIZED,
                     "State space prices not computed");
        return GR_ERROR_NOT_INITIALIZED;
    }
    
    int n = jac->num_dims;
    double h = ctx->bump_size;
    
    /* Store the evaluation point */
    for (int i = 0; i < n; i++) {
        jac->point[i] = point[i];
    }
    
    /* Get the center value using interpolation */
    jac->value = gr_state_space_interpolate_price(space, point);
    
    /* Compute partial derivatives using central differences */
    double* bumped = (double*)gr_ctx_calloc(ctx, (size_t)n, sizeof(double));
    if (!bumped) {
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                     "Failed to allocate bump buffer");
        return GR_ERROR_OUT_OF_MEMORY;
    }
    
    for (int d = 0; d < n; d++) {
        /* Copy point */
        for (int i = 0; i < n; i++) {
            bumped[i] = point[i];
        }
        
        /* Get dimension range for scaling the bump */
        const gr_dimension_internal_t* dim = &space->dims[d];
        double range = dim->max_value - dim->min_value;
        double scaled_h = h * range;  /* Bump proportional to range */
        
        /* f(x + h) */
        bumped[d] = point[d] + scaled_h;
        double f_plus = gr_state_space_interpolate_price(space, bumped);
        
        /* f(x - h) */
        bumped[d] = point[d] - scaled_h;
        double f_minus = gr_state_space_interpolate_price(space, bumped);
        
        /* Central difference */
        jac->partials[d] = (f_plus - f_minus) / (2.0 * scaled_h);
    }
    
    gr_ctx_free(ctx, bumped);
    
    jac->valid = 1;
    
    return GR_SUCCESS;
}

/* ============================================================================
 * Jacobian Accessors
 * ============================================================================ */

GR_API double gr_jacobian_get(const gr_jacobian_t* jac, int dim)
{
    if (!jac || !jac->valid) return 0.0;
    if (dim < 0 || dim >= jac->num_dims) return 0.0;
    
    return jac->partials[dim];
}

GR_API double gr_jacobian_norm(const gr_jacobian_t* jac)
{
    if (!jac || !jac->valid) return 0.0;
    
    return gr_jacobian_compute_norm(jac);
}

/* ============================================================================
 * Additional Jacobian Analysis Functions
 * ============================================================================ */

/**
 * Compute Jacobian at a point using a pricing function directly
 * (without requiring a pre-computed state space grid).
 * 
 * This is more flexible but slower for repeated evaluations.
 */
gr_error_t gr_jacobian_compute_direct(
    gr_jacobian_t* jac,
    gr_pricing_fn  fn,
    void*          user_data,
    const double*  point,
    double         bump_size)
{
    if (!jac) return GR_ERROR_NULL_POINTER;
    if (!fn) return GR_ERROR_NULL_POINTER;
    if (!point) return GR_ERROR_NULL_POINTER;
    
    gr_context_t* ctx = jac->ctx;
    int n = jac->num_dims;
    double h = bump_size > 0.0 ? bump_size : ctx->bump_size;
    
    /* Store the evaluation point */
    for (int i = 0; i < n; i++) {
        jac->point[i] = point[i];
    }
    
    /* Evaluate at center */
    jac->value = fn(point, n, user_data);
    
    /* Allocate working buffer */
    double* bumped = (double*)gr_ctx_calloc(ctx, (size_t)n, sizeof(double));
    if (!bumped) {
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                     "Failed to allocate bump buffer");
        return GR_ERROR_OUT_OF_MEMORY;
    }
    
    /* Compute each partial derivative */
    for (int d = 0; d < n; d++) {
        /* Copy point */
        for (int i = 0; i < n; i++) {
            bumped[i] = point[i];
        }
        
        /* Use central difference */
        jac->partials[d] = gr_partial_central(fn, user_data, bumped, n, d, h);
    }
    
    gr_ctx_free(ctx, bumped);
    
    jac->valid = 1;
    
    return GR_SUCCESS;
}

/**
 * Get the unit direction vector of steepest ascent.
 * out_direction must have space for num_dims doubles.
 */
gr_error_t gr_jacobian_direction(
    const gr_jacobian_t* jac,
    double*              out_direction)
{
    if (!jac || !jac->valid) return GR_ERROR_NOT_INITIALIZED;
    if (!out_direction) return GR_ERROR_NULL_POINTER;
    
    double norm = gr_jacobian_compute_norm(jac);
    
    if (norm < 1e-15) {
        /* Gradient is essentially zero - no preferred direction */
        for (int i = 0; i < jac->num_dims; i++) {
            out_direction[i] = 0.0;
        }
        return GR_SUCCESS;
    }
    
    for (int i = 0; i < jac->num_dims; i++) {
        out_direction[i] = jac->partials[i] / norm;
    }
    
    return GR_SUCCESS;
}
