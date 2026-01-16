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

    /* Center value */
    double f_center = gr_state_space_interpolate_price(space, point);

    /* Mutable copy of point */
    double* x = (double*)gr_ctx_malloc(ctx, (size_t)n * sizeof(double));
    if (!x) {
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY, "Failed to allocate temp array");
        return GR_ERROR_OUT_OF_MEMORY;
    }
    for (int i = 0; i < n; i++) {
        x[i] = point[i];
    }

    /*
     * IMPORTANT:
     * Use per-dimension grid spacing for finite differences.
     * Using ctx->bump_size blindly can mismatch the sampled grid and
     * explode second derivatives (exactly what your failing test showed).
     */
    double* hstep = (double*)gr_ctx_malloc(ctx, (size_t)n * sizeof(double));
    if (!hstep) {
        gr_ctx_free(ctx, x);
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY, "Failed to allocate step array");
        return GR_ERROR_OUT_OF_MEMORY;
    }

    for (int d = 0; d < n; d++) {
        const gr_dimension_internal_t* dim = &space->dims[d];

        double hd = 0.0;
        int np = dim->num_points;
        double range = dim->max_value - dim->min_value;

        if (np > 1 && fabs(range) > 1e-15) {
            hd = range / (double)(np - 1);
        } else {
            hd = ctx->bump_size;
            if (hd <= 0.0) hd = 0.01;
        }

        if (fabs(hd) < 1e-12) hd = 1e-6;
        hstep[d] = hd;
    }

    /* Diagonal terms */
    for (int i = 0; i < n; i++) {
        double orig = x[i];
        double hi = hstep[i];

        x[i] = orig + hi;
        double f_plus = gr_state_space_interpolate_price(space, x);

        x[i] = orig - hi;
        double f_minus = gr_state_space_interpolate_price(space, x);

        x[i] = orig;

        hess->data[i * n + i] = (f_plus - 2.0 * f_center + f_minus) / (hi * hi);
    }

    /* Mixed partials: 4-corner stencil */
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            double orig_i = x[i];
            double orig_j = x[j];

            double hi = hstep[i];
            double hj = hstep[j];

            x[i] = orig_i + hi; x[j] = orig_j + hj;
            double f_pp = gr_state_space_interpolate_price(space, x);

            x[i] = orig_i + hi; x[j] = orig_j - hj;
            double f_pm = gr_state_space_interpolate_price(space, x);

            x[i] = orig_i - hi; x[j] = orig_j + hj;
            double f_mp = gr_state_space_interpolate_price(space, x);

            x[i] = orig_i - hi; x[j] = orig_j - hj;
            double f_mm = gr_state_space_interpolate_price(space, x);

            x[i] = orig_i;
            x[j] = orig_j;

            double d2f = (f_pp - f_pm - f_mp + f_mm) / (4.0 * hi * hj);

            hess->data[i * n + j] = d2f;
            hess->data[j * n + i] = d2f;
        }
    }

    gr_ctx_free(ctx, hstep);
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

    double* M = (double*)gr_ctx_malloc(ctx, matrix_size * sizeof(double));
    if (!M) return GR_ERROR_OUT_OF_MEMORY;

    memcpy(M, hess->data, matrix_size * sizeof(double));

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

    gr_error_t err = compute_eigenvalues_internal((gr_hessian_t*)hess);
    if (err != GR_SUCCESS) return err;

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

