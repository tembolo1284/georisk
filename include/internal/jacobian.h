/**
 * internal/jacobian.h - Jacobian (first-order sensitivity) internals
 * 
 * The Jacobian captures the gradient of the pricing function with respect
 * to each dimension of the state space. It reveals:
 * 
 *   - Direction of steepest price change
 *   - Magnitude of sensitivity to each risk factor
 *   - Where small input errors amplify into large output errors
 * 
 * In the geometric risk framework, the Jacobian is the first-order
 * approximation of how the pricing manifold tilts at a given point.
 */

#ifndef GR_INTERNAL_JACOBIAN_H
#define GR_INTERNAL_JACOBIAN_H

#include "georisk.h"
#include "core.h"
#include <math.h>

/* ============================================================================
 * Jacobian Structure
 * ============================================================================ */

struct gr_jacobian_s {
    gr_context_t* ctx;          /* Parent context (not owned) */
    int           num_dims;     /* Number of dimensions */
    double*       partials;     /* [num_dims] partial derivatives ∂f/∂x_i */
    double*       point;        /* [num_dims] coordinates where computed */
    double        value;        /* f(point) - the price at this point */
    int           valid;        /* Has compute() been called successfully? */
};

/* ============================================================================
 * Numerical Differentiation Methods
 * ============================================================================ */

typedef enum gr_diff_method {
    GR_DIFF_FORWARD,    /* f(x+h) - f(x) / h           : O(h)   */
    GR_DIFF_BACKWARD,   /* f(x) - f(x-h) / h           : O(h)   */
    GR_DIFF_CENTRAL,    /* f(x+h) - f(x-h) / 2h        : O(h²)  */
    GR_DIFF_FIVE_POINT  /* Higher order central diff   : O(h⁴)  */
} gr_diff_method_t;

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * Compute partial derivative using central difference.
 * 
 * ∂f/∂x_i ≈ [f(x + h*e_i) - f(x - h*e_i)] / (2h)
 * 
 * where e_i is the unit vector in dimension i.
 */
static inline double gr_partial_central(
    gr_pricing_fn fn,
    void*         user_data,
    double*       point,        /* Will be modified temporarily */
    int           num_dims,
    int           dim,          /* Which dimension to differentiate */
    double        h)            /* Bump size */
{
    double original = point[dim];
    
    /* f(x + h) */
    point[dim] = original + h;
    double f_plus = fn(point, num_dims, user_data);
    
    /* f(x - h) */
    point[dim] = original - h;
    double f_minus = fn(point, num_dims, user_data);
    
    /* Restore */
    point[dim] = original;
    
    return (f_plus - f_minus) / (2.0 * h);
}

/**
 * Compute partial derivative using forward difference.
 * Less accurate but requires fewer function evaluations.
 * 
 * ∂f/∂x_i ≈ [f(x + h*e_i) - f(x)] / h
 */
static inline double gr_partial_forward(
    gr_pricing_fn fn,
    void*         user_data,
    double*       point,
    int           num_dims,
    int           dim,
    double        h,
    double        f_center)     /* f(x) - pass in to avoid recomputation */
{
    double original = point[dim];
    
    point[dim] = original + h;
    double f_plus = fn(point, num_dims, user_data);
    
    point[dim] = original;
    
    return (f_plus - f_center) / h;
}

/**
 * Compute partial derivative using five-point stencil.
 * Higher accuracy: O(h⁴) error.
 * 
 * ∂f/∂x_i ≈ [-f(x+2h) + 8f(x+h) - 8f(x-h) + f(x-2h)] / (12h)
 */
static inline double gr_partial_five_point(
    gr_pricing_fn fn,
    void*         user_data,
    double*       point,
    int           num_dims,
    int           dim,
    double        h)
{
    double original = point[dim];
    
    point[dim] = original + 2.0 * h;
    double f_p2 = fn(point, num_dims, user_data);
    
    point[dim] = original + h;
    double f_p1 = fn(point, num_dims, user_data);
    
    point[dim] = original - h;
    double f_m1 = fn(point, num_dims, user_data);
    
    point[dim] = original - 2.0 * h;
    double f_m2 = fn(point, num_dims, user_data);
    
    point[dim] = original;
    
    return (-f_p2 + 8.0 * f_p1 - 8.0 * f_m1 + f_m2) / (12.0 * h);
}

/**
 * Compute L2 norm (Euclidean length) of gradient.
 * This measures the overall sensitivity magnitude.
 */
static inline double gr_jacobian_compute_norm(const gr_jacobian_t* jac)
{
    double sum_sq = 0.0;
    for (int i = 0; i < jac->num_dims; i++) {
        sum_sq += jac->partials[i] * jac->partials[i];
    }
    return sqrt(sum_sq);
}

/**
 * Compute L-infinity norm (max absolute partial).
 * Identifies the single most sensitive dimension.
 */
static inline double gr_jacobian_linf_norm(const gr_jacobian_t* jac)
{
    double max_abs = 0.0;
    for (int i = 0; i < jac->num_dims; i++) {
        double abs_val = fabs(jac->partials[i]);
        if (abs_val > max_abs) {
            max_abs = abs_val;
        }
    }
    return max_abs;
}

/**
 * Find dimension with largest absolute partial derivative.
 * Returns dimension index, or -1 if jacobian is invalid.
 */
static inline int gr_jacobian_most_sensitive_dim(const gr_jacobian_t* jac)
{
    if (!jac->valid || jac->num_dims == 0) return -1;
    
    int max_dim = 0;
    double max_abs = fabs(jac->partials[0]);
    
    for (int i = 1; i < jac->num_dims; i++) {
        double abs_val = fabs(jac->partials[i]);
        if (abs_val > max_abs) {
            max_abs = abs_val;
            max_dim = i;
        }
    }
    return max_dim;
}

/**
 * Compute directional derivative along a given direction.
 * direction should be a unit vector (or will be treated as-is).
 * 
 * D_v f = ∇f · v = Σ (∂f/∂x_i) * v_i
 */
static inline double gr_jacobian_directional_derivative(
    const gr_jacobian_t* jac,
    const double*        direction)
{
    double result = 0.0;
    for (int i = 0; i < jac->num_dims; i++) {
        result += jac->partials[i] * direction[i];
    }
    return result;
}

#endif /* GR_INTERNAL_JACOBIAN_H */
