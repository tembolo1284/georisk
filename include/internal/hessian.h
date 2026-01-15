/**
 * internal/hessian.h - Hessian (curvature) internals
 */

#ifndef GR_INTERNAL_HESSIAN_H
#define GR_INTERNAL_HESSIAN_H

#define _USE_MATH_DEFINES
#include "georisk.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ============================================================================
 * Hessian Structure
 * ============================================================================ */

struct gr_hessian_s {
    gr_context_t* ctx;
    int           num_dims;
    double*       data;
    double*       point;
    double*       eigenvalues;
    int           valid;
    int           eigen_valid;
};

/* ============================================================================
 * Matrix Access Helpers
 * ============================================================================ */

static inline double gr_hessian_get_internal(const gr_hessian_t* h, int row, int col)
{
    return h->data[row * h->num_dims + col];
}

static inline void gr_hessian_set_internal(gr_hessian_t* h, int row, int col, double val)
{
    h->data[row * h->num_dims + col] = val;
}

static inline void gr_hessian_set_symmetric(gr_hessian_t* h, int i, int j, double val)
{
    h->data[i * h->num_dims + j] = val;
    h->data[j * h->num_dims + i] = val;
}

/* ============================================================================
 * Analysis Functions
 * ============================================================================ */

static inline double gr_hessian_compute_trace(const gr_hessian_t* h)
{
    double trace = 0.0;
    for (int i = 0; i < h->num_dims; i++) {
        trace += h->data[i * h->num_dims + i];
    }
    return trace;
}

static inline double gr_hessian_compute_frobenius(const gr_hessian_t* h)
{
    double sum = 0.0;
    int n = h->num_dims;
    for (int i = 0; i < n * n; i++) {
        sum += h->data[i] * h->data[i];
    }
    return sqrt(sum);
}

static inline double gr_hessian_compute_condition(const gr_hessian_t* h)
{
    if (!h->eigen_valid) return 0.0;
    
    double max_abs = 0.0;
    double min_abs = 1e300;
    
    for (int i = 0; i < h->num_dims; i++) {
        double abs_val = fabs(h->eigenvalues[i]);
        if (abs_val > max_abs) max_abs = abs_val;
        if (abs_val > 1e-15 && abs_val < min_abs) min_abs = abs_val;
    }
    
    if (min_abs < 1e-15) return 1e15;
    return max_abs / min_abs;
}

/* ============================================================================
 * Numerical Differentiation for Second Derivatives
 * ============================================================================ */

static inline double gr_second_partial_diagonal(
    gr_pricing_fn fn, void* data, double* x, int n, int i, double h, double f_center)
{
    double orig = x[i];
    
    x[i] = orig + h;
    double f_plus = fn(x, n, data);
    
    x[i] = orig - h;
    double f_minus = fn(x, n, data);
    
    x[i] = orig;
    
    return (f_plus - 2.0 * f_center + f_minus) / (h * h);
}

static inline double gr_second_partial_mixed(
    gr_pricing_fn fn, void* data, double* x, int n, int i, int j, double h)
{
    double orig_i = x[i];
    double orig_j = x[j];
    
    x[i] = orig_i + h;
    x[j] = orig_j + h;
    double f_pp = fn(x, n, data);
    
    x[j] = orig_j - h;
    double f_pm = fn(x, n, data);
    
    x[i] = orig_i - h;
    double f_mm = fn(x, n, data);
    
    x[j] = orig_j + h;
    double f_mp = fn(x, n, data);
    
    x[i] = orig_i;
    x[j] = orig_j;
    
    return (f_pp - f_pm - f_mp + f_mm) / (4.0 * h * h);
}

/* ============================================================================
 * Jacobi Eigenvalue Algorithm (no nested functions)
 * ============================================================================ */

#define GR_JACOBI_MAX_ITER 100
#define GR_JACOBI_TOL 1e-12

static inline double gr_jacobi_off_diag_norm(double* M, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            sum += M[i * n + j] * M[i * n + j];
        }
    }
    return sqrt(2.0 * sum);
}

static inline void gr_jacobi_find_max_off_diag(double* M, int n, int* out_p, int* out_q)
{
    double max_val = 0.0;
    *out_p = 0;
    *out_q = 1;
    
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            double val = fabs(M[i * n + j]);
            if (val > max_val) {
                max_val = val;
                *out_p = i;
                *out_q = j;
            }
        }
    }
}

static inline gr_error_t gr_eigenvalues_jacobi(double* M, int n, double* eigenvalues)
{
    for (int iter = 0; iter < GR_JACOBI_MAX_ITER; iter++) {
        double off = gr_jacobi_off_diag_norm(M, n);
        if (off < GR_JACOBI_TOL) {
            for (int i = 0; i < n; i++) {
                eigenvalues[i] = M[i * n + i];
            }
            
            for (int i = 0; i < n - 1; i++) {
                for (int j = i + 1; j < n; j++) {
                    if (fabs(eigenvalues[j]) > fabs(eigenvalues[i])) {
                        double tmp = eigenvalues[i];
                        eigenvalues[i] = eigenvalues[j];
                        eigenvalues[j] = tmp;
                    }
                }
            }
            return GR_SUCCESS;
        }
        
        int p, q;
        gr_jacobi_find_max_off_diag(M, n, &p, &q);
        
        double app = M[p * n + p];
        double aqq = M[q * n + q];
        double apq = M[p * n + q];
        
        double theta;
        if (fabs(app - aqq) < 1e-15) {
            theta = M_PI / 4.0;
        } else {
            theta = 0.5 * atan2(2.0 * apq, aqq - app);
        }
        
        double c = cos(theta);
        double s = sin(theta);
        
        for (int i = 0; i < n; i++) {
            if (i != p && i != q) {
                double Mip = M[i * n + p];
                double Miq = M[i * n + q];
                M[i * n + p] = c * Mip - s * Miq;
                M[p * n + i] = M[i * n + p];
                M[i * n + q] = s * Mip + c * Miq;
                M[q * n + i] = M[i * n + q];
            }
        }
        
        M[p * n + p] = c * c * app - 2 * s * c * apq + s * s * aqq;
        M[q * n + q] = s * s * app + 2 * s * c * apq + c * c * aqq;
        M[p * n + q] = 0.0;
        M[q * n + p] = 0.0;
    }
    
    return GR_ERROR_NUMERICAL_INSTABILITY;
}

#endif /* GR_INTERNAL_HESSIAN_H */
