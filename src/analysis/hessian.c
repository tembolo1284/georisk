/**
 * hessian.c - Hessian (curvature) computation
 * 
 * The Hessian matrix captures second-order sensitivity—the curvature
 * of the pricing manifold. It answers:
 * 
 *   "How does the sensitivity itself change as I move?"
 * 
 * This is the continuous generalization of second-order Greeks:
 *   - ∂²V/∂S² is Gamma
 *   - ∂²V/∂σ² is Volga (Vomma)
 *   - ∂²V/∂S∂σ is Vanna
 *   - etc.
 * 
 * High curvature regions are where linear hedges fail and where
 * statistical risk measures (which assume local linearity) break down.
 * 
 * "It is precisely for this reason that [statistical approaches]
 *  systematically fail at points of discontinuity."
 */

#include "georisk.h"
#include "internal/core.h"
#include "internal/allocator.h"
#include "internal/hessian.h"
#include "internal/state_space.h"
#include <string.h>
#include <math.h>

/* ============================================================================
 * Hessian Creation and Destruction
 * ============================================================================ */

GR_API gr_hessian_t* gr_hessian_new(gr_context_t* ctx, int num_dims)
{
    if (!ctx) return NULL;
    
    if (num_dims < 1 || num_dims > GR_MAX_DIMENSIONS) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT,
                     "Invalid number of dimensions");
        return NULL;
    }
    
    gr_hessian_t* hess = GR_CTX_ALLOC(ctx, gr_hessian_t);
    if (!hess) {
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                     "Failed to allocate Hessian");
        return NULL;
    }
    
    hess->ctx = ctx;
    hess->num_dims = num_dims;
    hess->valid = 0;
    hess->eigen_valid = 0;
    
    /* Allocate matrix storage (n x n) */
    size_t matrix_size = (size_t)num_dims * (size_t)num_dims;
    hess->data = (double*)gr_ctx_calloc(ctx, matrix_size, sizeof(double));
    if (!hess->data) {
        gr_ctx_free(ctx, hess);
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                     "Failed to allocate Hessian matrix");
        return NULL;
    }
    
    /* Allocate point storage */
    hess->point = (double*)gr_ctx_calloc(ctx, (size_t)num_dims, sizeof(double));
    if (!hess->point) {
        gr_ctx_free(ctx, hess->data);
        gr_ctx_free(ctx, hess);
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                     "Failed to allocate Hessian point");
        return NULL;
    }
    
    /* Allocate eigenvalue storage */
    hess->eigenvalues = (double*)gr_ctx_calloc(ctx, (size_t)num_dims, sizeof(double));
    if (!hess->eigenvalues) {
        gr_ctx_free(ctx, hess->point);
        gr_ctx_free(ctx, hess->data);
        gr_ctx_free(ctx, hess);
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                     "Failed to allocate Hessian eigenvalues");
        return NULL;
    }
    
    return hess;
}

GR_API void gr_hessian_free(gr_hessian_t* hess)
{
    if (!hess) return;
    
    gr_context_t* ctx = hess->ctx;
    
    if (hess->data) {
        gr_ctx_free(ctx, hess->data);
    }
    
    if (hess->point) {
        gr_ctx_free(ctx, hess->point);
    }
    
    if (hess->eigenvalues) {
        gr_ctx_free(ctx, hess->eigenvalues);
    }
    
    gr_ctx_free(ctx, hess);
}

/* ============================================================================
 * Hessian Computation
 * ============================================================================ */

GR_API gr_error_t gr_hessian_compute(
    gr_hessian_t*           hess,
    const gr_state_space_t* space,
    const double*           point)
{
    if (!hess) return GR_ERROR_NULL_POINTER;
    if (!space) return GR_ERROR_NULL_POINTER;
    if (!point) return GR_ERROR_NULL_POINTER;
    
    gr_context_t* ctx = hess->ctx;
    
    /* Validate dimensions match */
    if (hess->num_dims != space->num_dims) {
        gr_set_error(ctx, GR_ERROR_DIMENSION_MISMATCH,
                     "Hessian dimensions don't match state space");
        return GR_ERROR_DIMENSION_MISMATCH;
    }
    
    /* Check that prices have been computed */
    if (!space->prices_valid) {
        gr_set_error(ctx, GR_ERROR_NOT_INITIALIZED,
                     "State space prices not computed");
        return GR_ERROR_NOT_INITIALIZED;
    }
    
    int n = hess->num_dims;
    double h = ctx->bump_size;
    
    /* Store the evaluation point */
    for (int i = 0; i < n; i++) {
        hess->point[i] = point[i];
    }
    
    /* Allocate working buffer */
    double* bumped = (double*)gr_ctx_calloc(ctx, (size_t)n, sizeof(double));
    if (!bumped) {
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                     "Failed to allocate bump buffer");
        return GR_ERROR_OUT_OF_MEMORY;
    }
    
    /* Get center value */
    double f_center = gr_state_space_interpolate_price(space, point);
    
    /* Compute Hessian elements */
    for (int i = 0; i < n; i++) {
        /* Get dimension range for scaling */
        const gr_dimension_internal_t* dim_i = &space->dims[i];
        double range_i = dim_i->max_value - dim_i->min_value;
        double h_i = h * range_i;
        
        for (int j = i; j < n; j++) {
            double H_ij;
            
            if (i == j) {
                /* Diagonal: ∂²f/∂x_i² */
                for (int k = 0; k < n; k++) bumped[k] = point[k];
                
                bumped[i] = point[i] + h_i;
                double f_plus = gr_state_space_interpolate_price(space, bumped);
                
                bumped[i] = point[i] - h_i;
                double f_minus = gr_state_space_interpolate_price(space, bumped);
                
                H_ij = (f_plus - 2.0 * f_center + f_minus) / (h_i * h_i);
            } else {
                /* Off-diagonal: ∂²f/∂x_i∂x_j */
                const gr_dimension_internal_t* dim_j = &space->dims[j];
                double range_j = dim_j->max_value - dim_j->min_value;
                double h_j = h * range_j;
                
                for (int k = 0; k < n; k++) bumped[k] = point[k];
                
                /* f(x + h_i + h_j) */
                bumped[i] = point[i] + h_i;
                bumped[j] = point[j] + h_j;
                double f_pp = gr_state_space_interpolate_price(space, bumped);
                
                /* f(x + h_i - h_j) */
                bumped[j] = point[j] - h_j;
                double f_pm = gr_state_space_interpolate_price(space, bumped);
                
                /* f(x - h_i - h_j) */
                bumped[i] = point[i] - h_i;
                double f_mm = gr_state_space_interpolate_price(space, bumped);
                
                /* f(x - h_i + h_j) */
                bumped[j] = point[j] + h_j;
                double f_mp = gr_state_space_interpolate_price(space, bumped);
                
                H_ij = (f_pp - f_pm - f_mp + f_mm) / (4.0 * h_i * h_j);
            }
            
            /* Set symmetric elements */
            gr_hessian_set_symmetric(hess, i, j, H_ij);
        }
    }
    
    gr_ctx_free(ctx, bumped);
    
    hess->valid = 1;
    hess->eigen_valid = 0;  /* Eigenvalues need recomputation */
    
    return GR_SUCCESS;
}

/* ============================================================================
 * Hessian Accessors
 * ============================================================================ */

GR_API double gr_hessian_get(const gr_hessian_t* hess, int row, int col)
{
    if (!hess || !hess->valid) return 0.0;
    if (row < 0 || row >= hess->num_dims) return 0.0;
    if (col < 0 || col >= hess->num_dims) return 0.0;
    
    return gr_hessian_get_internal(hess, row, col);
}

/* ============================================================================
 * Eigenvalue Computation
 * ============================================================================ */

GR_API gr_error_t gr_hessian_eigenvalues(
    const gr_hessian_t* hess,
    double*             eigenvalues,
    int                 num_eigenvalues)
{
    if (!hess) return GR_ERROR_NULL_POINTER;
    if (!eigenvalues) return GR_ERROR_NULL_POINTER;
    if (!hess->valid) return GR_ERROR_NOT_INITIALIZED;
    
    gr_context_t* ctx = hess->ctx;
    int n = hess->num_dims;
    
    /* Clamp requested count */
    if (num_eigenvalues > n) num_eigenvalues = n;
    if (num_eigenvalues < 1) return GR_ERROR_INVALID_ARGUMENT;
    
    /* Compute eigenvalues if not cached */
    if (!hess->eigen_valid) {
        /* Make a copy of the matrix (Jacobi destroys it) */
        size_t matrix_size = (size_t)n * (size_t)n;
        double* matrix_copy = (double*)gr_ctx_calloc(ctx, matrix_size, sizeof(double));
        if (!matrix_copy) {
            gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                         "Failed to allocate matrix copy for eigenvalues");
            return GR_ERROR_OUT_OF_MEMORY;
        }
        
        for (size_t i = 0; i < matrix_size; i++) {
            matrix_copy[i] = hess->data[i];
        }
        
        /* Compute eigenvalues using Jacobi method */
        gr_error_t err = gr_eigenvalues_jacobi(
            matrix_copy, 
            n, 
            ((gr_hessian_t*)hess)->eigenvalues  /* Cast away const for caching */
        );
        
        gr_ctx_free(ctx, matrix_copy);
        
        if (err != GR_SUCCESS) {
            gr_set_error(ctx, err, "Eigenvalue computation failed");
            return err;
        }
        
        ((gr_hessian_t*)hess)->eigen_valid = 1;
    }
    
    /* Copy requested eigenvalues */
    for (int i = 0; i < num_eigenvalues; i++) {
        eigenvalues[i] = hess->eigenvalues[i];
    }
    
    return GR_SUCCESS;
}

/* ============================================================================
 * Hessian Analysis
 * ============================================================================ */

GR_API double gr_hessian_trace(const gr_hessian_t* hess)
{
    if (!hess || !hess->valid) return 0.0;
    return gr_hessian_compute_trace(hess);
}

GR_API double gr_hessian_frobenius_norm(const gr_hessian_t* hess)
{
    if (!hess || !hess->valid) return 0.0;
    return gr_hessian_compute_frobenius(hess);
}

GR_API double gr_hessian_condition_number(const gr_hessian_t* hess)
{
    if (!hess || !hess->valid) return 0.0;
    
    /* Ensure eigenvalues are computed */
    if (!hess->eigen_valid) {
        double dummy[GR_MAX_DIMENSIONS];
        gr_error_t err = gr_hessian_eigenvalues(hess, dummy, hess->num_dims);
        if (err != GR_SUCCESS) return 0.0;
    }
    
    return gr_hessian_compute_condition(hess);
}

/* ============================================================================
 * Direct Computation (without pre-computed state space)
 * ============================================================================ */

gr_error_t gr_hessian_compute_direct(
    gr_hessian_t*  hess,
    gr_pricing_fn  fn,
    void*          user_data,
    const double*  point,
    double         bump_size)
{
    if (!hess) return GR_ERROR_NULL_POINTER;
    if (!fn) return GR_ERROR_NULL_POINTER;
    if (!point) return GR_ERROR_NULL_POINTER;
    
    gr_context_t* ctx = hess->ctx;
    int n = hess->num_dims;
    double h = bump_size > 0.0 ? bump_size : ctx->bump_size;
    
    /* Store the evaluation point */
    for (int i = 0; i < n; i++) {
        hess->point[i] = point[i];
    }
    
    /* Allocate working buffer */
    double* bumped = (double*)gr_ctx_calloc(ctx, (size_t)n, sizeof(double));
    if (!bumped) {
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                     "Failed to allocate bump buffer");
        return GR_ERROR_OUT_OF_MEMORY;
    }
    
    /* Get center value */
    double f_center = fn(point, n, user_data);
    
    /* Compute Hessian elements */
    for (int i = 0; i < n; i++) {
        for (int j = i; j < n; j++) {
            double H_ij;
            
            /* Reset bumped to point */
            for (int k = 0; k < n; k++) bumped[k] = point[k];
            
            if (i == j) {
                /* Diagonal */
                H_ij = gr_second_partial_diagonal(fn, user_data, bumped, n, i, h, f_center);
            } else {
                /* Off-diagonal */
                H_ij = gr_second_partial_mixed(fn, user_data, bumped, n, i, j, h);
            }
            
            gr_hessian_set_symmetric(hess, i, j, H_ij);
        }
    }
    
    gr_ctx_free(ctx, bumped);
    
    hess->valid = 1;
    hess->eigen_valid = 0;
    
    return GR_SUCCESS;
}
