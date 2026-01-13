/**
 * internal/hessian.h - Hessian (second-order curvature) internals
 * 
 * The Hessian matrix captures the curvature of the pricing manifold.
 * It is the matrix of second partial derivatives:
 * 
 *   H_ij = ∂²f / ∂x_i ∂x_j
 * 
 * Key insights from the Hessian:
 * 
 *   - Eigenvalues reveal principal curvatures (convexity/concavity)
 *   - Large eigenvalues = high curvature = nonlinear region
 *   - Condition number = ratio of max/min eigenvalue = numerical fragility
 *   - Where curvature is high, linear approximations (delta, beta) fail
 * 
 * "Statistical risk is assumed linear (or locally linear).
 *  The Hessian tells us where that assumption breaks."
 */

#ifndef GR_INTERNAL_HESSIAN_H
#define GR_INTERNAL_HESSIAN_H

#include "georisk.h"
#include "core.h"
#include <math.h>

/* ============================================================================
 * Hessian Structure
 * ============================================================================ */

struct gr_hessian_s {
    gr_context_t* ctx;          /* Parent context (not owned) */
    int           num_dims;     /* Dimension n of state space */
    double*       data;         /* [n x n] symmetric matrix, row-major */
    double*       point;        /* [n] coordinates where computed */
    double*       eigenvalues;  /* [n] cached eigenvalues (sorted descending) */
    int           eigen_valid;  /* Have eigenvalues been computed? */
    int           valid;        /* Has compute() been called successfully? */
};

/* ============================================================================
 * Matrix Access Helpers
 * ============================================================================ */

/**
 * Get element H[i][j] from row-major storage.
 */
static inline double gr_hessian_get_internal(
    const gr_hessian_t* hess,
    int                 i,
    int                 j)
{
    return hess->data[i * hess->num_dims + j];
}

/**
 * Set element H[i][j] in row-major storage.
 */
static inline void gr_hessian_set_internal(
    gr_hessian_t* hess,
    int           i,
    int           j,
    double        value)
{
    hess->data[i * hess->num_dims + j] = value;
}

/**
 * Set symmetric element H[i][j] = H[j][i] = value.
 */
static inline void gr_hessian_set_symmetric(
    gr_hessian_t* hess,
    int           i,
    int           j,
    double        value)
{
    hess->data[i * hess->num_dims + j] = value;
    hess->data[j * hess->num_dims + i] = value;
}

/* ============================================================================
 * Second Derivative Computation
 * ============================================================================ */

/**
 * Compute diagonal second derivative ∂²f/∂x_i² using central difference.
 * 
 * ∂²f/∂x_i² ≈ [f(x+h) - 2f(x) + f(x-h)] / h²
 */
static inline double gr_second_partial_diagonal(
    gr_pricing_fn fn,
    void*         user_data,
    double*       point,
    int           num_dims,
    int           dim,
    double        h,
    double        f_center)
{
    double original = point[dim];
    
    point[dim] = original + h;
    double f_plus = fn(point, num_dims, user_data);
    
    point[dim] = original - h;
    double f_minus = fn(point, num_dims, user_data);
    
    point[dim] = original;
    
    return (f_plus - 2.0 * f_center + f_minus) / (h * h);
}

/**
 * Compute mixed partial derivative ∂²f/∂x_i∂x_j using central difference.
 * 
 * ∂²f/∂x_i∂x_j ≈ [f(x+h_i+h_j) - f(x+h_i-h_j) - f(x-h_i+h_j) + f(x-h_i-h_j)] / (4h²)
 */
static inline double gr_second_partial_mixed(
    gr_pricing_fn fn,
    void*         user_data,
    double*       point,
    int           num_dims,
    int           dim_i,
    int           dim_j,
    double        h)
{
    double orig_i = point[dim_i];
    double orig_j = point[dim_j];
    
    /* f(x + h_i + h_j) */
    point[dim_i] = orig_i + h;
    point[dim_j] = orig_j + h;
    double f_pp = fn(point, num_dims, user_data);
    
    /* f(x + h_i - h_j) */
    point[dim_j] = orig_j - h;
    double f_pm = fn(point, num_dims, user_data);
    
    /* f(x - h_i - h_j) */
    point[dim_i] = orig_i - h;
    double f_mm = fn(point, num_dims, user_data);
    
    /* f(x - h_i + h_j) */
    point[dim_j] = orig_j + h;
    double f_mp = fn(point, num_dims, user_data);
    
    /* Restore */
    point[dim_i] = orig_i;
    point[dim_j] = orig_j;
    
    return (f_pp - f_pm - f_mp + f_mm) / (4.0 * h * h);
}

/* ============================================================================
 * Matrix Analysis Helpers
 * ============================================================================ */

/**
 * Compute trace (sum of diagonal elements = sum of eigenvalues).
 * Measures total curvature.
 */
static inline double gr_hessian_compute_trace(const gr_hessian_t* hess)
{
    double trace = 0.0;
    for (int i = 0; i < hess->num_dims; i++) {
        trace += gr_hessian_get_internal(hess, i, i);
    }
    return trace;
}

/**
 * Compute Frobenius norm: sqrt(Σ H_ij²).
 * Measures overall magnitude of curvature matrix.
 */
static inline double gr_hessian_compute_frobenius(const gr_hessian_t* hess)
{
    double sum_sq = 0.0;
    int n = hess->num_dims;
    for (int i = 0; i < n * n; i++) {
        sum_sq += hess->data[i] * hess->data[i];
    }
    return sqrt(sum_sq);
}

/**
 * Check if Hessian is positive definite (all eigenvalues > 0).
 * Indicates convex region of pricing surface.
 */
static inline int gr_hessian_is_positive_definite(const gr_hessian_t* hess)
{
    if (!hess->eigen_valid) return 0;
    
    for (int i = 0; i < hess->num_dims; i++) {
        if (hess->eigenvalues[i] <= 0.0) return 0;
    }
    return 1;
}

/**
 * Check if Hessian is negative definite (all eigenvalues < 0).
 * Indicates concave region of pricing surface.
 */
static inline int gr_hessian_is_negative_definite(const gr_hessian_t* hess)
{
    if (!hess->eigen_valid) return 0;
    
    for (int i = 0; i < hess->num_dims; i++) {
        if (hess->eigenvalues[i] >= 0.0) return 0;
    }
    return 1;
}

/**
 * Check if Hessian is indefinite (has both positive and negative eigenvalues).
 * Indicates saddle point region - particularly unstable for hedging.
 */
static inline int gr_hessian_is_indefinite(const gr_hessian_t* hess)
{
    if (!hess->eigen_valid) return 0;
    
    int has_positive = 0;
    int has_negative = 0;
    
    for (int i = 0; i < hess->num_dims; i++) {
        if (hess->eigenvalues[i] > 0.0) has_positive = 1;
        if (hess->eigenvalues[i] < 0.0) has_negative = 1;
    }
    return has_positive && has_negative;
}

/**
 * Compute condition number: |λ_max| / |λ_min|.
 * High condition number = ill-conditioned = numerically fragile.
 */
static inline double gr_hessian_compute_condition(const gr_hessian_t* hess)
{
    if (!hess->eigen_valid || hess->num_dims == 0) return 0.0;
    
    double max_abs = 0.0;
    double min_abs = fabs(hess->eigenvalues[0]);
    
    for (int i = 0; i < hess->num_dims; i++) {
        double abs_val = fabs(hess->eigenvalues[i]);
        if (abs_val > max_abs) max_abs = abs_val;
        if (abs_val < min_abs && abs_val > 1e-15) min_abs = abs_val;
    }
    
    if (min_abs < 1e-15) return INFINITY;  /* Singular or near-singular */
    return max_abs / min_abs;
}

/* ============================================================================
 * Eigenvalue Computation (Jacobi Method for Symmetric Matrices)
 * 
 * Simple but robust. For production, consider linking LAPACK (dsyev).
 * ============================================================================ */

#define GR_JACOBI_MAX_ITER 100
#define GR_JACOBI_TOL 1e-12

/**
 * Compute eigenvalues of symmetric matrix using Jacobi iteration.
 * Modifies the input matrix (makes a copy internally in real implementation).
 * 
 * Returns GR_SUCCESS on convergence, GR_ERROR_NUMERICAL_INSTABILITY otherwise.
 */
static inline gr_error_t gr_eigenvalues_jacobi(
    double* A,          /* [n x n] symmetric matrix, will be destroyed */
    int     n,
    double* eigenvalues /* [n] output, sorted descending */
)
{
    /* Off-diagonal Frobenius norm */
    double off_diag_norm(double* M, int size) {
        double sum = 0.0;
        for (int i = 0; i < size; i++) {
            for (int j = i + 1; j < size; j++) {
                sum += M[i * size + j] * M[i * size + j];
            }
        }
        return sqrt(2.0 * sum);
    }
    
    for (int iter = 0; iter < GR_JACOBI_MAX_ITER; iter++) {
        /* Check convergence */
        double off = off_diag_norm(A, n);
        if (off < GR_JACOBI_TOL) {
            /* Extract diagonal as eigenvalues */
            for (int i = 0; i < n; i++) {
                eigenvalues[i] = A[i * n + i];
            }
            /* Sort descending */
            for (int i = 0; i < n - 1; i++) {
                for (int j = i + 1; j < n; j++) {
                    if (eigenvalues[j] > eigenvalues[i]) {
                        double tmp = eigenvalues[i];
                        eigenvalues[i] = eigenvalues[j];
                        eigenvalues[j] = tmp;
                    }
                }
            }
            return GR_SUCCESS;
        }
        
        /* Find largest off-diagonal element */
        int p = 0, q = 1;
        double max_val = fabs(A[0 * n + 1]);
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                if (fabs(A[i * n + j]) > max_val) {
                    max_val = fabs(A[i * n + j]);
                    p = i;
                    q = j;
                }
            }
        }
        
        if (max_val < GR_JACOBI_TOL) break;
        
        /* Compute rotation angle */
        double app = A[p * n + p];
        double aqq = A[q * n + q];
        double apq = A[p * n + q];
        
        double theta;
        if (fabs(app - aqq) < 1e-15) {
            theta = M_PI / 4.0;
        } else {
            theta = 0.5 * atan2(2.0 * apq, aqq - app);
        }
        
        double c = cos(theta);
        double s = sin(theta);
        
        /* Apply Jacobi rotation */
        for (int i = 0; i < n; i++) {
            if (i != p && i != q) {
                double aip = A[i * n + p];
                double aiq = A[i * n + q];
                A[i * n + p] = A[p * n + i] = c * aip - s * aiq;
                A[i * n + q] = A[q * n + i] = s * aip + c * aiq;
            }
        }
        
        A[p * n + p] = c * c * app - 2.0 * s * c * apq + s * s * aqq;
        A[q * n + q] = s * s * app + 2.0 * s * c * apq + c * c * aqq;
        A[p * n + q] = A[q * n + p] = 0.0;
    }
    
    return GR_ERROR_NUMERICAL_INSTABILITY;
}

#endif /* GR_INTERNAL_HESSIAN_H */
