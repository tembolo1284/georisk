/**
 * fragility.c - Fragility detection and mapping
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
#include <stdio.h>
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
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY, "Failed to allocate fragility map");
        return NULL;
    }
    
    map->ctx = ctx;
    map->space = space;
    map->config = (gr_fragility_config_t)GR_FRAGILITY_CONFIG_DEFAULT;
    
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
 * Full Grid Fragility Computation
 * ============================================================================ */

GR_API gr_error_t gr_fragility_map_compute(gr_fragility_map_t* map)
{
    if (!map) return GR_ERROR_NULL_POINTER;
    
    gr_context_t* ctx = map->ctx;
    gr_state_space_t* space = map->space;
    
    if (!space->prices_valid) {
        gr_set_error(ctx, GR_ERROR_NOT_INITIALIZED, "State space prices not computed");
        return GR_ERROR_NOT_INITIALIZED;
    }
    
    size_t total = space->total_points;
    int n = space->num_dims;
    
    if (!map->grid_scores) {
        map->grid_scores = (double*)gr_ctx_calloc(ctx, total, sizeof(double));
        if (!map->grid_scores) {
            gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY, "Failed to allocate fragility grid");
            return GR_ERROR_OUT_OF_MEMORY;
        }
    }
    
    map->max_fragility = 0.0;
    double sum_fragility = 0.0;
    size_t num_fragile = 0;
    
    for (size_t i = 0; i < map->num_points; i++) {
        gr_fragility_point_free(&map->points[i], ctx);
    }
    map->num_points = 0;
    
    double coords[GR_MAX_DIMENSIONS];
    
    gr_jacobian_t* jac = gr_jacobian_new(ctx, n);
    gr_hessian_t* hess = gr_hessian_new(ctx, n);
    
    if (!jac || !hess) {
        if (jac) gr_jacobian_free(jac);
        if (hess) gr_hessian_free(hess);
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY, "Failed to allocate analysis structures");
        return GR_ERROR_OUT_OF_MEMORY;
    }
    
    for (size_t flat = 0; flat < total; flat++) {
        gr_state_space_get_coordinates(space, flat, coords);
        
        gr_error_t err = gr_jacobian_compute(jac, space, coords);
        if (err != GR_SUCCESS) continue;
        
        err = gr_hessian_compute(hess, space, coords);
        if (err != GR_SUCCESS) continue;
        
        double gradient_norm = gr_jacobian_norm(jac);
        double frobenius = gr_hessian_frobenius_norm(hess);
        double condition = gr_hessian_condition_number(hess);
        
        double grad_component = gr_fragility_from_gradient(gradient_norm, map->config.gradient_scale);
        double curv_component = gr_fragility_from_curvature(frobenius, map->config.curvature_scale);
        double cond_component = gr_fragility_from_conditioning(condition, map->config.condition_threshold);
        double cons_component = 0.0;
        
        double fragility = gr_fragility_combine(
            grad_component, curv_component, cond_component, cons_component,
            &map->config);
        
        map->grid_scores[flat] = fragility;
        
        sum_fragility += fragility;
        if (fragility > map->max_fragility) {
            map->max_fragility = fragility;
        }
        
        if (fragility >= map->config.fragility_threshold) {
            num_fragile++;
            gr_fragility_map_add_point(map, coords, fragility, frobenius, gradient_norm, 0);
        }
    }
    
    map->mean_fragility = (total > 0) ? sum_fragility / (double)total : 0.0;
    map->fragile_fraction = (total > 0) ? (double)num_fragile / (double)total : 0.0;
    map->grid_computed = 1;
    
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
    if (index >= map->num_points) return GR_ERROR_INVALID_ARGUMENT;
    
    const gr_fragility_point_t* src = &map->points[index];
    
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
    if (!map->grid_computed || !map->grid_scores) return 0.0;
    
    size_t flat = gr_state_space_nearest_index(map->space, coordinates);
    if (flat >= map->space->total_points) return 0.0;
    
    return map->grid_scores[flat];
}
