/**
 * fragility.c - Fragility detection and mapping
 * 
 * "Identifying regions where small perturbations generate large effects"
 * 
 * Fragility is where geometric risk becomes actionable. This module
 * combines gradient magnitude, curvature analysis, conditioning, and
 * constraint proximity into a unified fragility score.
 * 
 * A fragile region is one where:
 *   - Small input changes cause large output changes (high gradient)
 *   - Linear approximations fail (high curvature)
 *   - Numerical methods become unstable (high condition number)
 *   - The system is close to forced state changes (near constraints)
 * 
 * "Without geometry, risk turns into a narrative."
 * With fragility mapping, we make the geometry explicit and actionable.
 */

#include "georisk.h"
#include "internal/core.h"
#include "internal/allocator.h"
#include "internal/fragility.h"
#include "internal/state_space.h"
#include "internal/jacobian.h"
#include "internal/hessian.h"
#include "internal/constraints.h"
#include <string.h>
#include <math.h>

/* ============================================================================
 * Fragility Map Creation and Destruction
 * ============================================================================ */

GR_API gr_fragility_map_t* gr_fragility_map_new(
    gr_context_t*     ctx,
    gr_state_space_t* space)
{
    if (!ctx) return NULL;
    if (!space) {
        gr_set_error(ctx, GR_ERROR_NULL_POINTER, "State space is NULL");
        return NULL;
    }
    
    gr_fragility_map_t* map = GR_CTX_ALLOC(ctx, gr_fragility_map_t);
    if (!map) {
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                     "Failed to allocate fragility map");
        return NULL;
    }
    
    map->ctx = ctx;
    map->space = space;
    map->config = GR_FRAGILITY_CONFIG_DEFAULT;
    
    map->points = NULL;
    map->num_points = 0;
    map->capacity = 0;
    
    map->grid_scores = NULL;
    map->grid_computed = 0;
    
    map->max_fragility = 0.0;
    map->mean_fragility = 0.0;
    map->fragile_fraction = 0.0;
    
    return map;
}

GR_API void gr_fragility_map_free(gr_fragility_map_t* map)
{
    if (!map) return;
    
    gr_context_t* ctx = map->ctx;
    
    /* Free fragile points */
    for (size_t i = 0; i < map->num_points; i++) {
        gr_fragility_point_free(&map->points[i], ctx);
    }
    
    if (map->points) {
        gr_ctx_free(ctx, map->points);
    }
    
    if (map->grid_scores) {
        gr_ctx_free(ctx, map->grid_scores);
    }
    
    gr_ctx_free(ctx, map);
}

/* ============================================================================
 * Fragility Computation at a Single Point
 * ============================================================================ */

/**
 * Compute fragility score at a single point in state space.
 * 
 * This is the core computation that combines all fragility components.
 */
static gr_error_t compute_point_fragility(
    gr_fragility_map_t*            map,
    const double*                  coordinates,
    const gr_constraint_surface_t* constraints,  /* May be NULL */
    double*                        out_fragility,
    double*                        out_curvature,
    double*                        out_gradient_norm,
    int*                           out_near_constraint)
{
    gr_context_t* ctx = map->ctx;
    gr_state_space_t* space = map->space;
    int n = space->num_dims;
    
    /* Create temporary Jacobian and Hessian */
    gr_jacobian_t* jac = gr_jacobian_new(ctx, n);
    gr_hessian_t* hess = gr_hessian_new(ctx, n);
    
    if (!jac || !hess) {
        if (jac) gr_jacobian_free(jac);
        if (hess) gr_hessian_free(hess);
        return GR_ERROR_OUT_OF_MEMORY;
    }
    
    /* Compute Jacobian */
    gr_error_t err = gr_jacobian_compute(jac, space, coordinates);
    if (err != GR_SUCCESS) {
        gr_jacobian_free(jac);
        gr_hessian_free(hess);
        return err;
    }
    
    /* Compute Hessian */
    err = gr_hessian_compute(hess, space, coordinates);
    if (err != GR_SUCCESS) {
        gr_jacobian_free(jac);
        gr_hessian_free(hess);
        return err;
    }
    
    /* Extract metrics */
    double gradient_norm = gr_jacobian_norm(jac);
    double frobenius = gr_hessian_frobenius_norm(hess);
    double condition = gr_hessian_condition_number(hess);
    
    /* Constraint proximity */
    double constraint_dist = INFINITY;
    int near_constraint = 0;
    
    if (constraints && constraints->num_constraints > 0) {
        constraint_dist = gr_constraints_min_distance(constraints, coordinates, n);
        near_constraint = (constraint_dist < map->config.constraint_threshold);
    }
    
    /* Compute fragility components */
    double grad_component = gr_fragility_from_gradient(
        gradient_norm, 
        map->config.gradient_scale
    );
    
    double curv_component = gr_fragility_from_curvature(
        frobenius,
        map->config.curvature_scale
    );
    
    double cond_component = gr_fragility_from_conditioning(
        condition,
        map->config.condition_threshold
    );
    
    double cons_component = gr_fragility_from_constraint(
        constraint_dist,
        map->config.constraint_threshold
    );
    
    /* Combine into overall fragility score */
    double fragility = gr_fragility_combine(
        grad_component,
        curv_component,
        cond_component,
        cons_component,
        &map->config
    );
    
    /* Output results */
    *out_fragility = fragility;
    *out_curvature = frobenius;
    *out_gradient_norm = gradient_norm;
    *out_near_constraint = near_constraint;
    
    /* Cleanup */
    gr_jacobian_free(jac);
    gr_hessian_free(hess);
    
    return GR_SUCCESS;
}

/* ============================================================================
 * Full Grid Fragility Computation
 * ============================================================================ */

GR_API gr_error_t gr_fragility_map_compute(gr_fragility_map_t* map)
{
    if (!map) return GR_ERROR_NULL_POINTER;
    
    gr_context_t* ctx = map->ctx;
    gr_state_space_t* space = map->space;
    
    if (!space->prices_valid) {
        gr_set_error(ctx, GR_ERROR_NOT_INITIALIZED,
                     "State space prices not computed");
        return GR_ERROR_NOT_INITIALIZED;
    }
    
    size_t total = space->total_points;
    int n = space->num_dims;
    
    /* Allocate grid scores if needed */
    if (!map->grid_scores) {
        map->grid_scores = (double*)gr_ctx_calloc(ctx, total, sizeof(double));
        if (!map->grid_scores) {
            gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                         "Failed to allocate fragility grid");
            return GR_ERROR_OUT_OF_MEMORY;
        }
    }
    
    /* Reset statistics */
    map->max_fragility = 0.0;
    double sum_fragility = 0.0;
    size_t num_fragile = 0;
    
    /* Clear existing fragile points */
    for (size_t i = 0; i < map->num_points; i++) {
        gr_fragility_point_free(&map->points[i], ctx);
    }
    map->num_points = 0;
    
    /* Temporary coordinate buffer */
    double coords[GR_MAX_DIMENSIONS];
    
    /* Create reusable Jacobian and Hessian */
    gr_jacobian_t* jac = gr_jacobian_new(ctx, n);
    gr_hessian_t* hess = gr_hessian_new(ctx, n);
    
    if (!jac || !hess) {
        if (jac) gr_jacobian_free(jac);
        if (hess) gr_hessian_free(hess);
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                     "Failed to allocate analysis structures");
        return GR_ERROR_OUT_OF_MEMORY;
    }
    
    /* Iterate over all grid points */
    for (size_t flat = 0; flat < total; flat++) {
        /* Get coordinates */
        gr_state_space_get_coordinates(space, flat, coords);
        
        /* Compute Jacobian */
        gr_error_t err = gr_jacobian_compute(jac, space, coords);
        if (err != GR_SUCCESS) continue;  /* Skip problematic points */
        
        /* Compute Hessian */
        err = gr_hessian_compute(hess, space, coords);
        if (err != GR_SUCCESS) continue;
        
        /* Extract metrics */
        double gradient_norm = gr_jacobian_norm(jac);
        double frobenius = gr_hessian_frobenius_norm(hess);
        double condition = gr_hessian_condition_number(hess);
        
        /* Compute fragility components */
        double grad_component = gr_fragility_from_gradient(
            gradient_norm, 
            map->config.gradient_scale
        );
        
        double curv_component = gr_fragility_from_curvature(
            frobenius,
            map->config.curvature_scale
        );
        
        double cond_component = gr_fragility_from_conditioning(
            condition,
            map->config.condition_threshold
        );
        
        /* No constraints in basic compute - use 0 */
        double cons_component = 0.0;
        
        /* Combine */
        double fragility = gr_fragility_combine(
            grad_component,
            curv_component,
            cond_component,
            cons_component,
            &map->config
        );
        
        /* Store in grid */
        map->grid_scores[flat] = fragility;
        
        /* Update statistics */
        sum_fragility += fragility;
        if (fragility > map->max_fragility) {
            map->max_fragility = fragility;
        }
        
        /* Check if fragile */
        if (fragility >= map->config.fragility_threshold) {
            num_fragile++;
            
            /* Store fragile point */
            err = gr_fragility_map_add_point(
                map,
                coords,
                fragility,
                frobenius,
                gradient_norm,
                0  /* near_constraint - not computed here */
            );
            /* Ignore error - continue even if storage fails */
        }
    }
    
    /* Compute final statistics */
    map->mean_fragility = (total > 0) ? sum_fragility / (double)total : 0.0;
    map->fragile_fraction = (total > 0) ? (double)num_fragile / (double)total : 0.0;
    map->grid_computed = 1;
    
    /* Cleanup */
    gr_jacobian_free(jac);
    gr_hessian_free(hess);
    
    return GR_SUCCESS;
}

/* ============================================================================
 * Fragility Map Accessors
 * ============================================================================ */

GR_API size_t gr_fragility_map_get_num_fragile_regions(const gr_fragility_map_t* map)
{
    if (!map) return 0;
    return map->num_points;
}

GR_API gr_error_t gr_fragility_map_get_region(
    const gr_fragility_map_t* map,
    size_t                    index,
    gr_fragility_point_t*     out)
{
    if (!map) return GR_ERROR_NULL_POINTER;
    if (!out) return GR_ERROR_NULL_POINTER;
    
    if (index >= map->num_points) {
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    const gr_fragility_point_t* src = &map->points[index];
    
    /* Shallow copy - caller should not free coordinates */
    out->coordinates = src->coordinates;
    out->fragility_score = src->fragility_score;
    out->curvature = src->curvature;
    out->gradient_norm = src->gradient_norm;
    out->near_constraint = src->near_constraint;
    
    return GR_SUCCESS;
}

GR_API double gr_fragility_at_point(
    const gr_fragility_map_t* map,
    const double*             coordinates)
{
    if (!map || !coordinates) return 0.0;
    
    if (!map->grid_computed || !map->grid_scores) {
        return 0.0;
    }
    
    /* Find nearest grid point */
    size_t flat = gr_state_space_nearest_index(map->space, coordinates);
    
    if (flat >= map->space->total_points) {
        return 0.0;
    }
    
    return map->grid_scores[flat];
}

/* ============================================================================
 * Fragility Configuration
 * ============================================================================ */

/**
 * Set fragility configuration.
 * Not in public API yet, but useful for tuning.
 */
void gr_fragility_map_set_config(
    gr_fragility_map_t*          map,
    const gr_fragility_config_t* config)
{
    if (!map || !config) return;
    map->config = *config;
    
    /* Invalidate computed results */
    map->grid_computed = 0;
}

/**
 * Get fragility statistics.
 */
void gr_fragility_map_get_statistics(
    const gr_fragility_map_t* map,
    double*                   out_max,
    double*                   out_mean,
    double*                   out_fragile_fraction)
{
    if (!map) return;
    
    if (out_max) *out_max = map->max_fragility;
    if (out_mean) *out_mean = map->mean_fragility;
    if (out_fragile_fraction) *out_fragile_fraction = map->fragile_fraction;
}

/* ============================================================================
 * Fragility Reporting
 * ============================================================================ */

/**
 * Generate a human-readable fragility report.
 * Returns allocated string that caller must free.
 */
char* gr_fragility_map_report(const gr_fragility_map_t* map)
{
    if (!map) return NULL;
    
    gr_context_t* ctx = map->ctx;
    
    /* Estimate buffer size */
    size_t buf_size = 1024 + map->num_points * 256;
    char* buf = (char*)gr_ctx_malloc(ctx, buf_size);
    if (!buf) return NULL;
    
    char* p = buf;
    size_t remaining = buf_size;
    int written;
    
    /* Header */
    written = snprintf(p, remaining,
        "=== Fragility Analysis Report ===\n\n"
        "State Space: %d dimensions, %zu total points\n\n"
        "Statistics:\n"
        "  Max Fragility:      %.4f\n"
        "  Mean Fragility:     %.4f\n"
        "  Fragile Fraction:   %.2f%%\n"
        "  Fragile Regions:    %zu\n\n",
        map->space->num_dims,
        map->space->total_points,
        map->max_fragility,
        map->mean_fragility,
        map->fragile_fraction * 100.0,
        map->num_points
    );
    
    if (written > 0 && (size_t)written < remaining) {
        p += written;
        remaining -= (size_t)written;
    }
    
    /* Top fragile regions */
    if (map->num_points > 0) {
        written = snprintf(p, remaining, "Top Fragile Regions:\n");
        if (written > 0 && (size_t)written < remaining) {
            p += written;
            remaining -= (size_t)written;
        }
        
        size_t num_to_show = map->num_points < 10 ? map->num_points : 10;
        
        for (size_t i = 0; i < num_to_show; i++) {
            const gr_fragility_point_t* pt = &map->points[i];
            gr_region_type_t type = gr_classify_fragility(pt->fragility_score);
            
            written = snprintf(p, remaining,
                "  [%zu] Score: %.4f (%s)\n"
                "       Gradient: %.4f, Curvature: %.4f\n",
                i + 1,
                pt->fragility_score,
                gr_region_type_string(type),
                pt->gradient_norm,
                pt->curvature
            );
            
            if (written > 0 && (size_t)written < remaining) {
                p += written;
                remaining -= (size_t)written;
            }
        }
    }
    
    return buf;
}
