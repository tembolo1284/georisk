/**
 * constraints.c - Constraint surface implementation
 * 
 * "In mathematics, every geometry is defined by axioms:
 *  1) which states are admissible
 *  2) which movements are possible
 *  3) which transitions carry a cost
 *  4) which constraints make certain paths unlikely or impossible"
 * 
 * This module implements constraint surfaces—the boundaries of
 * the admissible state space. Constraints represent real-world
 * limitations that pricing models ignore:
 * 
 *   - Liquidity: Can't trade infinite size at mid
 *   - Position limits: Regulatory and risk limits
 *   - Margin: Collateral requirements
 *   - Funding: Cost of carry constraints
 * 
 * Near constraint boundaries, the geometry of risk changes dramatically.
 * A position that looks safe by VaR may be one tick away from forced
 * liquidation.
 */

#include "georisk.h"
#include "internal/core.h"
#include "internal/allocator.h"
#include "internal/constraints.h"
#include <string.h>
#include <math.h>

/* ============================================================================
 * Constraint Surface Creation and Destruction
 * ============================================================================ */

GR_API gr_constraint_surface_t* gr_constraint_surface_new(gr_context_t* ctx)
{
    if (!ctx) return NULL;
    
    gr_constraint_surface_t* surface = GR_CTX_ALLOC(ctx, gr_constraint_surface_t);
    if (!surface) {
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                     "Failed to allocate constraint surface");
        return NULL;
    }
    
    surface->ctx = ctx;
    surface->num_constraints = 0;
    
    /* Initialize all constraints to inactive */
    for (int i = 0; i < GR_MAX_CONSTRAINTS; i++) {
        surface->constraints[i].active = 0;
    }
    
    return surface;
}

GR_API void gr_constraint_surface_free(gr_constraint_surface_t* surface)
{
    if (!surface) return;
    
    gr_context_t* ctx = surface->ctx;
    
    /* No dynamic allocations in constraints currently */
    /* If we add custom constraint user_data ownership, free here */
    
    gr_ctx_free(ctx, surface);
}

/* ============================================================================
 * Constraint Management
 * ============================================================================ */

GR_API gr_error_t gr_constraint_add(
    gr_constraint_surface_t* surface,
    gr_constraint_type_t     type,
    const char*              name,
    double                   threshold)
{
    if (!surface) return GR_ERROR_NULL_POINTER;
    
    gr_context_t* ctx = surface->ctx;
    
    if (surface->num_constraints >= GR_MAX_CONSTRAINTS) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT,
                     "Maximum constraints exceeded");
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    gr_constraint_t* c = &surface->constraints[surface->num_constraints];
    
    /* Initialize based on type with sensible defaults */
    switch (type) {
        case GR_CONSTRAINT_LIQUIDITY:
            gr_constraint_init_threshold(
                c, type, name ? name : "liquidity",
                -1,  /* No specific dimension - set by user */
                GR_CONSTRAINT_UPPER,
                threshold,
                GR_CONSTRAINT_SOFT
            );
            c->penalty_rate = 100.0;  /* High penalty for illiquidity */
            break;
            
        case GR_CONSTRAINT_POSITION_LIMIT:
            gr_constraint_init_threshold(
                c, type, name ? name : "position_limit",
                -1,
                GR_CONSTRAINT_UPPER,
                threshold,
                GR_CONSTRAINT_HARD
            );
            break;
            
        case GR_CONSTRAINT_MARGIN:
            gr_constraint_init_threshold(
                c, type, name ? name : "margin",
                -1,
                GR_CONSTRAINT_LOWER,  /* Must be >= threshold */
                threshold,
                GR_CONSTRAINT_SOFT
            );
            c->penalty_rate = 50.0;
            break;
            
        case GR_CONSTRAINT_REGULATORY:
            gr_constraint_init_threshold(
                c, type, name ? name : "regulatory",
                -1,
                GR_CONSTRAINT_UPPER,
                threshold,
                GR_CONSTRAINT_HARD
            );
            break;
            
        case GR_CONSTRAINT_CUSTOM:
        default:
            gr_constraint_init_threshold(
                c, type, name ? name : "custom",
                -1,
                GR_CONSTRAINT_UPPER,
                threshold,
                GR_CONSTRAINT_SOFT
            );
            break;
    }
    
    surface->num_constraints++;
    
    return GR_SUCCESS;
}

/* ============================================================================
 * Advanced Constraint Configuration
 * ============================================================================ */

/**
 * Add a constraint with full configuration.
 */
gr_error_t gr_constraint_add_full(
    gr_constraint_surface_t*  surface,
    gr_constraint_type_t      type,
    const char*               name,
    int                       dimension,
    gr_constraint_direction_t direction,
    double                    threshold,
    gr_constraint_hardness_t  hardness,
    double                    penalty_rate)
{
    if (!surface) return GR_ERROR_NULL_POINTER;
    
    gr_context_t* ctx = surface->ctx;
    
    if (surface->num_constraints >= GR_MAX_CONSTRAINTS) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT,
                     "Maximum constraints exceeded");
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    gr_constraint_t* c = &surface->constraints[surface->num_constraints];
    
    gr_constraint_init_threshold(
        c, type, name,
        dimension, direction, threshold, hardness
    );
    c->penalty_rate = penalty_rate;
    
    surface->num_constraints++;
    
    return GR_SUCCESS;
}

/**
 * Add a custom function constraint.
 */
gr_error_t gr_constraint_add_custom(
    gr_constraint_surface_t*  surface,
    const char*               name,
    gr_constraint_eval_fn     eval_fn,
    void*                     user_data,
    gr_constraint_direction_t direction,
    double                    threshold,
    gr_constraint_hardness_t  hardness)
{
    if (!surface) return GR_ERROR_NULL_POINTER;
    if (!eval_fn) return GR_ERROR_NULL_POINTER;
    
    gr_context_t* ctx = surface->ctx;
    
    if (surface->num_constraints >= GR_MAX_CONSTRAINTS) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT,
                     "Maximum constraints exceeded");
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    gr_constraint_t* c = &surface->constraints[surface->num_constraints];
    
    gr_constraint_init_custom(
        c, GR_CONSTRAINT_CUSTOM, name,
        eval_fn, user_data,
        direction, threshold, hardness
    );
    
    surface->num_constraints++;
    
    return GR_SUCCESS;
}

/* ============================================================================
 * Constraint Checking
 * ============================================================================ */

GR_API int gr_constraint_check(
    const gr_constraint_surface_t* surface,
    const double*                  coordinates,
    int                            num_dims)
{
    if (!surface || !coordinates) return 0;
    
    /* Return 1 if ANY constraint is violated */
    for (int i = 0; i < surface->num_constraints; i++) {
        if (gr_constraint_is_violated(&surface->constraints[i], coordinates, num_dims)) {
            return 1;
        }
    }
    
    return 0;
}

GR_API double gr_constraint_distance(
    const gr_constraint_surface_t* surface,
    const double*                  coordinates,
    int                            num_dims)
{
    if (!surface || !coordinates) return INFINITY;
    
    return gr_constraints_min_distance(surface, coordinates, num_dims);
}

/* ============================================================================
 * Constraint Queries
 * ============================================================================ */

/**
 * Get the number of constraints.
 */
int gr_constraint_surface_count(const gr_constraint_surface_t* surface)
{
    if (!surface) return 0;
    return surface->num_constraints;
}

/**
 * Get constraint name by index.
 */
const char* gr_constraint_get_name(
    const gr_constraint_surface_t* surface,
    int                            index)
{
    if (!surface) return NULL;
    if (index < 0 || index >= surface->num_constraints) return NULL;
    
    return surface->constraints[index].name;
}

/**
 * Enable or disable a constraint by index.
 */
void gr_constraint_set_active(
    gr_constraint_surface_t* surface,
    int                      index,
    int                      active)
{
    if (!surface) return;
    if (index < 0 || index >= surface->num_constraints) return;
    
    surface->constraints[index].active = active ? 1 : 0;
}

/**
 * Find which constraint is most binding (closest to violation).
 * Returns constraint index, or -1 if no constraints.
 */
int gr_constraint_most_binding(
    const gr_constraint_surface_t* surface,
    const double*                  coordinates,
    int                            num_dims,
    double*                        out_distance)
{
    if (!surface || !coordinates) return -1;
    
    int nearest = gr_constraints_nearest_index(surface, coordinates, num_dims);
    
    if (nearest >= 0 && out_distance) {
        *out_distance = gr_constraint_signed_distance(
            &surface->constraints[nearest],
            coordinates,
            num_dims
        );
    }
    
    return nearest;
}

/* ============================================================================
 * Constraint Visualization Helpers
 * ============================================================================ */

/**
 * Generate points along a constraint boundary for visualization.
 * Useful for 2D slices of the state space.
 * 
 * Returns number of points generated.
 */
int gr_constraint_trace_boundary(
    const gr_constraint_surface_t* surface,
    int                            constraint_index,
    int                            fixed_dims,      /* Bitmask of fixed dimensions */
    const double*                  fixed_values,    /* Values for fixed dims */
    int                            trace_dim,       /* Dimension to trace along */
    double                         trace_min,
    double                         trace_max,
    int                            num_samples,
    double*                        out_points,      /* [num_samples * num_dims] */
    int                            num_dims)
{
    if (!surface || !out_points) return 0;
    if (constraint_index < 0 || constraint_index >= surface->num_constraints) return 0;
    if (num_samples < 2) return 0;
    
    const gr_constraint_t* c = &surface->constraints[constraint_index];
    
    /* For simple threshold constraints, the boundary is straightforward */
    if (c->eval_fn == NULL && c->dimension == trace_dim) {
        /* Constraint is directly on the trace dimension */
        /* Boundary is a hyperplane at threshold */
        double step = (trace_max - trace_min) / (double)(num_samples - 1);
        
        for (int i = 0; i < num_samples; i++) {
            double* pt = &out_points[i * num_dims];
            
            for (int d = 0; d < num_dims; d++) {
                if (d == trace_dim) {
                    pt[d] = c->threshold;  /* On the boundary */
                } else if (fixed_dims & (1 << d)) {
                    pt[d] = fixed_values[d];
                } else {
                    pt[d] = trace_min + (double)i * step;
                }
            }
        }
        
        return num_samples;
    }
    
    /* For more complex constraints, would need numerical root finding */
    /* Not implemented in this version */
    
    return 0;
}
