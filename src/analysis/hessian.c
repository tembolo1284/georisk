/**
 * hessian.c - Hessian (second-order curvature) computation
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
    if (num_dims <= 0 || num_dims > GR_MAX_DIMENSIONS) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT, "Invalid number of dimensions");
        return NULL;
    }
    
    gr_hessian_t* hess = GR_CTX_ALLOC(ctx, gr_hessian_t);
    if (!hess) {
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY, "Failed to allocate Hessian");
        return NULL;
    }
    
    hess->ctx = ctx;
    hess->num_dims = num_dims;
    hess->valid = 0;
    hess->eigen_valid = 0;
    
    size_t matrix_size = (size_t)num_dims * (size_t)num_dims;
    
    hess->data = (double*)gr_ctx_calloc(ctx, matrix_size, sizeof(double));
    hess->point = (double*)gr_ctx_calloc(ctx, (size_t)num_dims, sizeof(double));
    hess->eigenvalues = (double*)gr_ctx_calloc(ctx, (size_t)num_dims, sizeof(double));
    
    if (!hess->data || !hess->point || !hess->eigenvalues) {
        gr_hessian_free(hess);
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY, "Failed to allocate Hessian arrays");
        return NULL;
    }
    
    return hess;
}

GR_API void gr_hessian_free(gr_hessian_t* hess)
{
    if (!hess) return;
    
    gr_context_t* ctx = hess->ctx;
    
    if (hess->data) gr_ctx_free(ctx, hess->data);
    if (hess->point) gr_ctx_free(ctx, hess->point);
    if (hess->eigenvalues) gr_ctx_free(ctx, hess->eigenvalues);
    
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
    if (!hess || !space || !point) {
        return GR_ERROR_NULL_POINTER;
    }
    
    gr_context_t* ctx = hess->ctx;
    int n = hess->num_dims;
    
    if (n != space->num_dims) {
        gr_set_error(ctx, GR_ERROR_DIMENSION_MISMATCH, "Dimension mismatch");
        return GR_ERROR_DIMENSION_MISMATCH;
    }
    
    /* Store the evaluation point */
    for (int i = 0; i < n; i++) {
        hess->point[i] = point[i];
    }
    
    /* Invalidate cached eigenvalues */
    hess->eigen_valid = 0;
    
    /* Use grid spacing for numerical stability */
    double h = ctx->bump_size;
    if (h <= 0.0 || h < 1e-6) {
        if (space->num_dims > 0 && space->dims[0].num_points > 1) {
            double range = space->dims[0].max_value - space->dims[0].min_value;
            h = range / (double)(space->dims[0].num_points - 1);
        } else {
            h = 0.01;
        }
    }
    
    /* Get function value at center point */
    double f_center = gr_state_space_interpolate_price(space, point);
    
    /* Create a mutable copy of the point for finite differences */
    double* x = (double*)gr_ctx_malloc(ctx, (size_t)n * sizeof(double));
    if (!x) {
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY, "Failed to allocate temp array");
        return GR_ERROR_OUT_OF_MEMORY;
    }
    
    for (int i = 0; i < n; i++) {
        x[i] = point[i];
    }
    
    /* Compute diagonal elements (second partial derivatives) */
    for (int i = 0; i < n; i++) {
        double orig = x[i];
        
        x[i] = orig + h;
        double f_plus = gr_state_space_interpolate_price(space, x);
        
        x[i] = orig - h;
        double f_minus = gr_state_space_interpolate_price(space, x);
        
        x[i] = orig;
        
        double d2f = (f_plus - 2.0 * f_center + f_minus) / (h * h);
        hess->data[i * n + i] = d2f;
    }
    
    /* Compute off-diagonal elements (mixed partial derivatives) */
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            double orig_i = x[i];
            double orig_j = x[j];
            
            x[i] = orig_i + h;
            x[j] = orig_j + h;
            double f_pp = gr_state_space_interpolate_price(space, x);
            
            x[j] = orig_j - h;
            double f_pm = gr_state_space_interpolate_price(space, x);
            
            x[i] = orig_i - h;
            double f_mm = gr_state_space_interpolate_price(space, x);
            
            x[j] = orig_j + h;
            double f_mp = gr_state_space_interpolate_price(space, x);
            
            x[i] = orig_i;
            x[j] = orig_j;
            
            double d2f = (f_pp - f_pm - f_mp + f_mm) / (4.0 * h * h);
            
            /* Hessian is symmetric */
            hess->data[i * n + j] = d2f;
            hess->data[j * n + i] = d2f;
        }
    }
    
    gr_ctx_free(ctx, x);
    
    hess->valid = 1;
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
    
    return hess->data[row * hess->num_dims + col];
}

/* ============================================================================
 * Eigenvalue Computation
 * ============================================================================ */

static gr_error_t compute_eigenvalues_internal(gr_hessian_t* hess)
{
    if (hess->eigen_valid) return GR_SUCCESS;
    if (!hess->valid) return GR_ERROR_NOT_INITIALIZED;
    
    gr_context_t* ctx = hess->ctx;
    int n = hess->num_dims;
    size_t matrix_size = (size_t)n * (size_t)n;
    
    /* Create a copy of the matrix for Jacobi iteration */
    double* M = (double*)gr_ctx_malloc(ctx, matrix_size * sizeof(double));
    if (!M) return GR_ERROR_OUT_OF_MEMORY;
    
    memcpy(M, hess->data, matrix_size * sizeof(double));
    
    /* Run Jacobi eigenvalue algorithm */
    gr_error_t err = gr_eigenvalues_jacobi(M, n, hess->eigenvalues);
    
    gr_ctx_free(ctx, M);
    
    if (err == GR_SUCCESS) {
        hess->eigen_valid = 1;
    }
    
    return err;
}

GR_API gr_error_t gr_hessian_eigenvalues(
    const gr_hessian_t* hess,
    double*             eigenvalues,
    int                 num_eigenvalues)
{
    if (!hess || !eigenvalues) return GR_ERROR_NULL_POINTER;
    if (!hess->valid) return GR_ERROR_NOT_INITIALIZED;
    if (num_eigenvalues <= 0) return GR_ERROR_INVALID_ARGUMENT;
    
    /* Compute eigenvalues if not already done */
    gr_error_t err = compute_eigenvalues_internal((gr_hessian_t*)hess);
    if (err != GR_SUCCESS) return err;
    
    /* Copy eigenvalues to output */
    int count = (num_eigenvalues < hess->num_dims) ? num_eigenvalues : hess->num_dims;
    for (int i = 0; i < count; i++) {
        eigenvalues[i] = hess->eigenvalues[i];
    }
    
    return GR_SUCCESS;
}

/* ============================================================================
 * Hessian Analysis Functions
 * ============================================================================ */

GR_API double gr_hessian_trace(const gr_hessian_t* hess)
{
    if (!hess || !hess->valid) return 0.0;
    
    double trace = 0.0;
    for (int i = 0; i < hess->num_dims; i++) {
        trace += hess->data[i * hess->num_dims + i];
    }
    return trace;
}

GR_API double gr_hessian_frobenius_norm(const gr_hessian_t* hess)
{
    if (!hess || !hess->valid) return 0.0;
    
    double sum = 0.0;
    int n = hess->num_dims;
    for (int i = 0; i < n * n; i++) {
        sum += hess->data[i] * hess->data[i];
    }
    return sqrt(sum);
}

GR_API double gr_hessian_condition_number(const gr_hessian_t* hess)
{
    if (!hess || !hess->valid) return 0.0;
    
    /* Compute eigenvalues if not already done */
    gr_error_t err = compute_eigenvalues_internal((gr_hessian_t*)hess);
    if (err != GR_SUCCESS) return 0.0;
    
    double max_abs = 0.0;
    double min_abs = 1e300;
    
    for (int i = 0; i < hess->num_dims; i++) {
        double abs_val = fabs(hess->eigenvalues[i]);
        if (abs_val > max_abs) max_abs = abs_val;
        if (abs_val > 1e-15 && abs_val < min_abs) min_abs = abs_val;
    }
    
    if (min_abs < 1e-15) return 1e15;
    
    return max_abs / min_abs;
}
