/**
 * internal/fragility.h - Fragility detection internals
 */

#ifndef GR_INTERNAL_FRAGILITY_H
#define GR_INTERNAL_FRAGILITY_H

#include "georisk.h"
#include "allocator.h"
#include "state_space.h"
#include "jacobian.h"
#include "hessian.h"
#include <math.h>

/* ============================================================================
 * Fragility Configuration
 * ============================================================================ */

typedef struct gr_fragility_config {
    double gradient_weight;
    double curvature_weight;
    double condition_weight;
    double constraint_weight;
    double gradient_scale;
    double curvature_scale;
    double condition_threshold;
    double constraint_threshold;
    double fragility_threshold;
} gr_fragility_config_t;

#define GR_FRAGILITY_CONFIG_DEFAULT { \
    .gradient_weight = 0.25,          \
    .curvature_weight = 0.30,         \
    .condition_weight = 0.25,         \
    .constraint_weight = 0.20,        \
    .gradient_scale = 1.0,            \
    .curvature_scale = 1.0,           \
    .condition_threshold = 100.0,     \
    .constraint_threshold = 0.1,      \
    .fragility_threshold = 0.5        \
}

/* ============================================================================
 * Fragility Point
 * ============================================================================ */

struct gr_fragility_point_s {
    double* coordinates;
    double  fragility_score;
    double  curvature;
    double  gradient_norm;
    int     near_constraint;
};

static inline void gr_fragility_point_free(gr_fragility_point_t* pt, gr_context_t* ctx)
{
    if (pt->coordinates) {
        gr_ctx_free(ctx, pt->coordinates);
        pt->coordinates = NULL;
    }
}

/* ============================================================================
 * Fragility Map Structure
 * ============================================================================ */

struct gr_fragility_map_s {
    gr_context_t*         ctx;
    gr_state_space_t*     space;
    gr_fragility_config_t config;
    
    gr_fragility_point_t* points;
    size_t                num_points;
    size_t                capacity;
    
    double*               grid_scores;
    int                   grid_computed;
    
    double                max_fragility;
    double                mean_fragility;
    double                fragile_fraction;
};

/* ============================================================================
 * Region Classification
 * ============================================================================ */

typedef enum gr_region_type {
    GR_REGION_STABLE,
    GR_REGION_SENSITIVE,
    GR_REGION_FRAGILE,
    GR_REGION_CRITICAL
} gr_region_type_t;

static inline gr_region_type_t gr_classify_fragility(double score)
{
    if (score < 0.25) return GR_REGION_STABLE;
    if (score < 0.50) return GR_REGION_SENSITIVE;
    if (score < 0.75) return GR_REGION_FRAGILE;
    return GR_REGION_CRITICAL;
}

static inline const char* gr_region_type_string(gr_region_type_t type)
{
    switch (type) {
        case GR_REGION_STABLE:    return "STABLE";
        case GR_REGION_SENSITIVE: return "SENSITIVE";
        case GR_REGION_FRAGILE:   return "FRAGILE";
        case GR_REGION_CRITICAL:  return "CRITICAL";
        default:                  return "UNKNOWN";
    }
}

/* ============================================================================
 * Fragility Component Functions
 * ============================================================================ */

static inline double gr_fragility_from_gradient(double norm, double scale)
{
    double x = norm / scale;
    return x / (1.0 + x);  /* Sigmoid-like: approaches 1 as norm -> infinity */
}

static inline double gr_fragility_from_curvature(double frobenius, double scale)
{
    double x = frobenius / scale;
    return x / (1.0 + x);
}

static inline double gr_fragility_from_conditioning(double condition, double threshold)
{
    if (condition < 1.0) return 0.0;
    return log(condition) / log(threshold);  /* 0 at cond=1, 1 at cond=threshold */
}

static inline double gr_fragility_from_constraint(double distance, double threshold)
{
    if (distance <= 0.0) return 1.0;
    if (distance >= threshold) return 0.0;
    return 1.0 - (distance / threshold);
}

static inline double gr_fragility_combine(
    double grad, double curv, double cond, double cons,
    const gr_fragility_config_t* cfg)
{
    double score = cfg->gradient_weight * grad
                 + cfg->curvature_weight * curv
                 + cfg->condition_weight * cond
                 + cfg->constraint_weight * cons;
    
    if (score < 0.0) score = 0.0;
    if (score > 1.0) score = 1.0;
    
    return score;
}

/* ============================================================================
 * Point Storage
 * ============================================================================ */

static inline gr_error_t gr_fragility_map_add_point(
    gr_fragility_map_t* map,
    const double*       coords,
    double              fragility,
    double              curvature,
    double              gradient_norm,
    int                 near_constraint)
{
    gr_context_t* ctx = map->ctx;
    int n = map->space->num_dims;
    
    /* Grow capacity if needed */
    if (map->num_points >= map->capacity) {
        size_t new_cap = map->capacity == 0 ? 64 : map->capacity * 2;
        gr_fragility_point_t* new_pts = (gr_fragility_point_t*)gr_ctx_realloc(
            ctx, map->points, new_cap * sizeof(gr_fragility_point_t));
        if (!new_pts) return GR_ERROR_OUT_OF_MEMORY;
        map->points = new_pts;
        map->capacity = new_cap;
    }
    
    gr_fragility_point_t* pt = &map->points[map->num_points];
    
    pt->coordinates = (double*)gr_ctx_malloc(ctx, (size_t)n * sizeof(double));
    if (!pt->coordinates) return GR_ERROR_OUT_OF_MEMORY;
    
    for (int i = 0; i < n; i++) {
        pt->coordinates[i] = coords[i];
    }
    
    pt->fragility_score = fragility;
    pt->curvature = curvature;
    pt->gradient_norm = gradient_norm;
    pt->near_constraint = near_constraint;
    
    map->num_points++;
    
    return GR_SUCCESS;
}

#endif /* GR_INTERNAL_FRAGILITY_H */
