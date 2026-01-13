/**
 * internal/jacobian.h - Jacobian (gradient) internals
 */

#ifndef GR_INTERNAL_JACOBIAN_H
#define GR_INTERNAL_JACOBIAN_H

#include "georisk.h"
#include <math.h>

/* ============================================================================
 * Jacobian Structure
 * ============================================================================ */

struct gr_jacobian_s {
    gr_context_t* ctx;
    int           num_dims;
    double*       partials;    /* Partial derivatives */
    double*       point;       /* Evaluation point */
    double        value;       /* Function value at point */
    int           valid;
};

/* ============================================================================
 * Analysis Functions
 * ============================================================================ */

static inline double gr_jacobian_compute_norm(const gr_jacobian_t* j)
{
    double sum = 0.0;
    for (int i = 0; i < j->num_dims; i++) {
        sum += j->partials[i] * j->partials[i];
    }
    return sqrt(sum);
}

static inline double gr_jacobian_compute_linf_norm(const gr_jacobian_t* j)
{
    double max = 0.0;
    for (int i = 0; i < j->num_dims; i++) {
        double abs_val = fabs(j->partials[i]);
        if (abs_val > max) max = abs_val;
    }
    return max;
}

static inline int gr_jacobian_most_sensitive(const gr_jacobian_t* j)
{
    int idx = 0;
    double max = 0.0;
    for (int i = 0; i < j->num_dims; i++) {
        double abs_val = fabs(j->partials[i]);
        if (abs_val > max) {
            max = abs_val;
            idx = i;
        }
    }
    return idx;
}

/* ============================================================================
 * Numerical Differentiation
 * ============================================================================ */

static inline double gr_partial_forward(
    gr_pricing_fn fn, void* data, double* x, int n, int dim, double h)
{
    double orig = x[dim];
    double f0 = fn(x, n, data);
    
    x[dim] = orig + h;
    double f1 = fn(x, n, data);
    
    x[dim] = orig;
    return (f1 - f0) / h;
}

static inline double gr_partial_central(
    gr_pricing_fn fn, void* data, double* x, int n, int dim, double h)
{
    double orig = x[dim];
    
    x[dim] = orig + h;
    double f_plus = fn(x, n, data);
    
    x[dim] = orig - h;
    double f_minus = fn(x, n, data);
    
    x[dim] = orig;
    return (f_plus - f_minus) / (2.0 * h);
}

#endif /* GR_INTERNAL_JACOBIAN_H */
