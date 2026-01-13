/**
 * internal/fragility.h - Fragility detection internals
 * 
 * "Identifying regions where small perturbations generate large effects"
 * 
 * Fragility is where geometric risk becomes actionable. A fragile region
 * is characterized by:
 * 
 *   1. High gradient norm (steep sensitivity)
 *   2. High curvature (nonlinearity, Hessian eigenvalues)
 *   3. Proximity to constraint surfaces (liquidity cliffs, margin limits)
 *   4. Ill-conditioning (condition number of local Hessian)
 *   5. Discontinuities (barriers, knockouts, digital payoffs)
 * 
 * The fragility score combines these factors into a single metric that
 * answers: "How much could things go wrong here?"
 */

#ifndef GR_INTERNAL_FRAGILITY_H
#define GR_INTERNAL_FRAGILITY_H

#include "georisk.h"
#include "core.h"
#include "state_space.h"
#include "jacobian.h"
#include "hessian.h"
#include <math.h>

/* ============================================================================
 * Fragility Configuration
 * ============================================================================ */

typedef struct gr_fragility_config {
    /* Weights for combining fragility components (should sum to 1.0) */
    double weight_gradient;       /* Contribution from gradient magnitude */
    double weight_curvature;      /* Contribution from Hessian analysis */
    double weight_conditioning;   /* Contribution from condition number */
    double weight_constraint;     /* Contribution from constraint proximity */
    
    /* Thresholds for normalization */
    double gradient_scale;        /* Gradient norm considered "high" */
    double curvature_scale;       /* Curvature considered "high" */
    double condition_threshold;   /* Condition number considered problematic */
    double constraint_threshold;  /* Distance to constraint considered "close" */
    
    /* Detection thresholds */
    double fragility_threshold;   /* Score above which region is flagged */
    int    store_all_points;      /* Store all points or only fragile ones */
} gr_fragility_config_t;

/* Default configuration */
static const gr_fragility_config_t GR_FRAGILITY_CONFIG_DEFAULT = {
    .weight_gradient     = 0.25,
    .weight_curvature    = 0.30,
    .weight_conditioning = 0.25,
    .weight_constraint   = 0.20,
    
    .gradient_scale      = 1.0,
    .curvature_scale     = 10.0,
    .condition_threshold = 100.0,
    .constraint_threshold = 0.05,
    
    .fragility_threshold = 0.5,
    .store_all_points    = 0
};

/* ============================================================================
 * Fragility Map Structure
 * ============================================================================ */

struct gr_fragility_map_s {
    gr_context_t*         ctx;              /* Parent context */
    gr_state_space_t*     space;            /* State space being analyzed */
    gr_fragility_config_t config;           /* Configuration */
    
    /* Storage for fragile regions */
    gr_fragility_point_t* points;           /* Array of fragile points */
    size_t                num_points;       /* Number of fragile points found */
    size_t                capacity;         /* Allocated capacity */
    
    /* Full grid fragility scores (optional) */
    double*               grid_scores;      /* [total_points] or NULL */
    int                   grid_computed;    /* Has full grid been computed? */
    
    /* Statistics */
    double                max_fragility;    /* Maximum fragility found */
    double                mean_fragility;   /* Mean fragility across grid */
    double                fragile_fraction; /* Fraction of points above threshold */
};

/* ============================================================================
 * Fragility Component Computations
 * ============================================================================ */

/**
 * Compute gradient-based fragility component.
 * High gradient = high sensitivity = more fragile.
 * 
 * Returns value in [0, 1] where 1 = maximally fragile.
 */
static inline double gr_fragility_from_gradient(
    double gradient_norm,
    double scale)
{
    if (scale <= 0.0) return 0.0;
    
    /* Sigmoid-like scaling: 2 / (1 + exp(-x/scale)) - 1 */
    double x = gradient_norm / scale;
    return 2.0 / (1.0 + exp(-x)) - 1.0;
}

/**
 * Compute curvature-based fragility component.
 * High curvature = nonlinear = linear hedges fail.
 * 
 * Uses the Frobenius norm of the Hessian as curvature measure.
 */
static inline double gr_fragility_from_curvature(
    double frobenius_norm,
    double scale)
{
    if (scale <= 0.0) return 0.0;
    
    double x = frobenius_norm / scale;
    return 2.0 / (1.0 + exp(-x)) - 1.0;
}

/**
 * Compute conditioning-based fragility component.
 * High condition number = numerically unstable = fragile.
 */
static inline double gr_fragility_from_conditioning(
    double condition_number,
    double threshold)
{
    if (threshold <= 1.0) return 0.0;
    if (condition_number <= 1.0) return 0.0;
    
    /* Log scale: condition numbers can be huge */
    double log_cond = log10(condition_number);
    double log_thresh = log10(threshold);
    
    if (log_cond <= 0.0) return 0.0;
    if (log_cond >= 2.0 * log_thresh) return 1.0;
    
    return log_cond / (2.0 * log_thresh);
}

/**
 * Compute constraint-based fragility component.
 * Close to constraint = close to forced liquidation = fragile.
 */
static inline double gr_fragility_from_constraint(
    double distance,
    double threshold)
{
    if (threshold <= 0.0) return 0.0;
    if (distance <= 0.0) return 1.0;  /* At or beyond constraint */
    if (distance >= threshold) return 0.0;
    
    /* Linear interpolation: closer = more fragile */
    return 1.0 - (distance / threshold);
}

/**
 * Combine fragility components into overall score.
 */
static inline double gr_fragility_combine(
    double                       gradient_component,
    double                       curvature_component,
    double                       conditioning_component,
    double                       constraint_component,
    const gr_fragility_config_t* config)
{
    return config->weight_gradient     * gradient_component +
           config->weight_curvature    * curvature_component +
           config->weight_conditioning * conditioning_component +
           config->weight_constraint   * constraint_component;
}

/* ============================================================================
 * Fragility Point Management
 * ============================================================================ */

/**
 * Initialize a fragility point with given coordinates.
 */
static inline gr_error_t gr_fragility_point_init(
    gr_fragility_point_t* point,
    gr_context_t*         ctx,
    int                   num_dims,
    const double*         coordinates)
{
    point->coordinates = (double*)gr_ctx_calloc(ctx, (size_t)num_dims, sizeof(double));
    if (!point->coordinates) {
        return GR_ERROR_OUT_OF_MEMORY;
    }
    
    for (int i = 0; i < num_dims; i++) {
        point->coordinates[i] = coordinates[i];
    }
    
    point->fragility_score = 0.0;
    point->curvature = 0.0;
    point->gradient_norm = 0.0;
    point->near_constraint = 0;
    
    return GR_SUCCESS;
}

/**
 * Free fragility point resources.
 */
static inline void gr_fragility_point_free(
    gr_fragility_point_t* point,
    gr_context_t*         ctx)
{
    if (point->coordinates) {
        gr_ctx_free(ctx, point->coordinates);
        point->coordinates = NULL;
    }
}

/**
 * Ensure capacity for storing fragile points.
 */
static inline gr_error_t gr_fragility_map_ensure_capacity(
    gr_fragility_map_t* map,
    size_t              needed)
{
    if (map->capacity >= needed) return GR_SUCCESS;
    
    size_t new_cap = map->capacity == 0 ? 64 : map->capacity * 2;
    while (new_cap < needed) new_cap *= 2;
    
    gr_fragility_point_t* new_points = (gr_fragility_point_t*)gr_ctx_realloc(
        map->ctx,
        map->points,
        new_cap * sizeof(gr_fragility_point_t)
    );
    
    if (!new_points) {
        return GR_ERROR_OUT_OF_MEMORY;
    }
    
    map->points = new_points;
    map->capacity = new_cap;
    
    return GR_SUCCESS;
}

/**
 * Add a fragile point to the map.
 */
static inline gr_error_t gr_fragility_map_add_point(
    gr_fragility_map_t*   map,
    const double*         coordinates,
    double                fragility_score,
    double                curvature,
    double                gradient_norm,
    int                   near_constraint)
{
    gr_error_t err = gr_fragility_map_ensure_capacity(map, map->num_points + 1);
    if (err != GR_SUCCESS) return err;
    
    gr_fragility_point_t* point = &map->points[map->num_points];
    
    err = gr_fragility_point_init(
        point,
        map->ctx,
        map->space->num_dims,
        coordinates
    );
    if (err != GR_SUCCESS) return err;
    
    point->fragility_score = fragility_score;
    point->curvature = curvature;
    point->gradient_norm = gradient_norm;
    point->near_constraint = near_constraint;
    
    map->num_points++;
    
    /* Update statistics */
    if (fragility_score > map->max_fragility) {
        map->max_fragility = fragility_score;
    }
    
    return GR_SUCCESS;
}

/* ============================================================================
 * Region Classification
 * ============================================================================ */

typedef enum gr_region_type {
    GR_REGION_STABLE,           /* Low fragility, safe to operate */
    GR_REGION_SENSITIVE,        /* Moderate fragility, monitor closely */
    GR_REGION_FRAGILE,          /* High fragility, reduce exposure */
    GR_REGION_CRITICAL          /* Very high fragility, immediate action needed */
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

#endif /* GR_INTERNAL_FRAGILITY_H *//**
 * internal/fragility.h - Fragility detection internals
 * 
 * "Identifying regions where small perturbations generate large effects"
 * 
 * Fragility is where geometric risk becomes actionable. A fragile region
 * is characterized by:
 * 
 *   1. High gradient norm (steep sensitivity)
 *   2. High curvature (nonlinearity, Hessian eigenvalues)
 *   3. Proximity to constraint surfaces (liquidity cliffs, margin limits)
 *   4. Ill-conditioning (condition number of local Hessian)
 *   5. Discontinuities (barriers, knockouts, digital payoffs)
 * 
 * The fragility score combines these factors into a single metric that
 * answers: "How much could things go wrong here?"
 */

#ifndef GR_INTERNAL_FRAGILITY_H
#define GR_INTERNAL_FRAGILITY_H

#include "georisk.h"
#include "core.h"
#include "state_space.h"
#include "jacobian.h"
#include "hessian.h"
#include <math.h>

/* ============================================================================
 * Fragility Configuration
 * ============================================================================ */

typedef struct gr_fragility_config {
    /* Weights for combining fragility components (should sum to 1.0) */
    double weight_gradient;       /* Contribution from gradient magnitude */
    double weight_curvature;      /* Contribution from Hessian analysis */
    double weight_conditioning;   /* Contribution from condition number */
    double weight_constraint;     /* Contribution from constraint proximity */
    
    /* Thresholds for normalization */
    double gradient_scale;        /* Gradient norm considered "high" */
    double curvature_scale;       /* Curvature considered "high" */
    double condition_threshold;   /* Condition number considered problematic */
    double constraint_threshold;  /* Distance to constraint considered "close" */
    
    /* Detection thresholds */
    double fragility_threshold;   /* Score above which region is flagged */
    int    store_all_points;      /* Store all points or only fragile ones */
} gr_fragility_config_t;

/* Default configuration */
static const gr_fragility_config_t GR_FRAGILITY_CONFIG_DEFAULT = {
    .weight_gradient     = 0.25,
    .weight_curvature    = 0.30,
    .weight_conditioning = 0.25,
    .weight_constraint   = 0.20,
    
    .gradient_scale      = 1.0,
    .curvature_scale     = 10.0,
    .condition_threshold = 100.0,
    .constraint_threshold = 0.05,
    
    .fragility_threshold = 0.5,
    .store_all_points    = 0
};

/* ============================================================================
 * Fragility Map Structure
 * ============================================================================ */

struct gr_fragility_map_s {
    gr_context_t*         ctx;              /* Parent context */
    gr_state_space_t*     space;            /* State space being analyzed */
    gr_fragility_config_t config;           /* Configuration */
    
    /* Storage for fragile regions */
    gr_fragility_point_t* points;           /* Array of fragile points */
    size_t                num_points;       /* Number of fragile points found */
    size_t                capacity;         /* Allocated capacity */
    
    /* Full grid fragility scores (optional) */
    double*               grid_scores;      /* [total_points] or NULL */
    int                   grid_computed;    /* Has full grid been computed? */
    
    /* Statistics */
    double                max_fragility;    /* Maximum fragility found */
    double                mean_fragility;   /* Mean fragility across grid */
    double                fragile_fraction; /* Fraction of points above threshold */
};

/* ============================================================================
 * Fragility Component Computations
 * ============================================================================ */

/**
 * Compute gradient-based fragility component.
 * High gradient = high sensitivity = more fragile.
 * 
 * Returns value in [0, 1] where 1 = maximally fragile.
 */
static inline double gr_fragility_from_gradient(
    double gradient_norm,
    double scale)
{
    if (scale <= 0.0) return 0.0;
    
    /* Sigmoid-like scaling: 2 / (1 + exp(-x/scale)) - 1 */
    double x = gradient_norm / scale;
    return 2.0 / (1.0 + exp(-x)) - 1.0;
}

/**
 * Compute curvature-based fragility component.
 * High curvature = nonlinear = linear hedges fail.
 * 
 * Uses the Frobenius norm of the Hessian as curvature measure.
 */
static inline double gr_fragility_from_curvature(
    double frobenius_norm,
    double scale)
{
    if (scale <= 0.0) return 0.0;
    
    double x = frobenius_norm / scale;
    return 2.0 / (1.0 + exp(-x)) - 1.0;
}

/**
 * Compute conditioning-based fragility component.
 * High condition number = numerically unstable = fragile.
 */
static inline double gr_fragility_from_conditioning(
    double condition_number,
    double threshold)
{
    if (threshold <= 1.0) return 0.0;
    if (condition_number <= 1.0) return 0.0;
    
    /* Log scale: condition numbers can be huge */
    double log_cond = log10(condition_number);
    double log_thresh = log10(threshold);
    
    if (log_cond <= 0.0) return 0.0;
    if (log_cond >= 2.0 * log_thresh) return 1.0;
    
    return log_cond / (2.0 * log_thresh);
}

/**
 * Compute constraint-based fragility component.
 * Close to constraint = close to forced liquidation = fragile.
 */
static inline double gr_fragility_from_constraint(
    double distance,
    double threshold)
{
    if (threshold <= 0.0) return 0.0;
    if (distance <= 0.0) return 1.0;  /* At or beyond constraint */
    if (distance >= threshold) return 0.0;
    
    /* Linear interpolation: closer = more fragile */
    return 1.0 - (distance / threshold);
}

/**
 * Combine fragility components into overall score.
 */
static inline double gr_fragility_combine(
    double                       gradient_component,
    double                       curvature_component,
    double                       conditioning_component,
    double                       constraint_component,
    const gr_fragility_config_t* config)
{
    return config->weight_gradient     * gradient_component +
           config->weight_curvature    * curvature_component +
           config->weight_conditioning * conditioning_component +
           config->weight_constraint   * constraint_component;
}

/* ============================================================================
 * Fragility Point Management
 * ============================================================================ */

/**
 * Initialize a fragility point with given coordinates.
 */
static inline gr_error_t gr_fragility_point_init(
    gr_fragility_point_t* point,
    gr_context_t*         ctx,
    int                   num_dims,
    const double*         coordinates)
{
    point->coordinates = (double*)gr_ctx_calloc(ctx, (size_t)num_dims, sizeof(double));
    if (!point->coordinates) {
        return GR_ERROR_OUT_OF_MEMORY;
    }
    
    for (int i = 0; i < num_dims; i++) {
        point->coordinates[i] = coordinates[i];
    }
    
    point->fragility_score = 0.0;
    point->curvature = 0.0;
    point->gradient_norm = 0.0;
    point->near_constraint = 0;
    
    return GR_SUCCESS;
}

/**
 * Free fragility point resources.
 */
static inline void gr_fragility_point_free(
    gr_fragility_point_t* point,
    gr_context_t*         ctx)
{
    if (point->coordinates) {
        gr_ctx_free(ctx, point->coordinates);
        point->coordinates = NULL;
    }
}

/**
 * Ensure capacity for storing fragile points.
 */
static inline gr_error_t gr_fragility_map_ensure_capacity(
    gr_fragility_map_t* map,
    size_t              needed)
{
    if (map->capacity >= needed) return GR_SUCCESS;
    
    size_t new_cap = map->capacity == 0 ? 64 : map->capacity * 2;
    while (new_cap < needed) new_cap *= 2;
    
    gr_fragility_point_t* new_points = (gr_fragility_point_t*)gr_ctx_realloc(
        map->ctx,
        map->points,
        new_cap * sizeof(gr_fragility_point_t)
    );
    
    if (!new_points) {
        return GR_ERROR_OUT_OF_MEMORY;
    }
    
    map->points = new_points;
    map->capacity = new_cap;
    
    return GR_SUCCESS;
}

/**
 * Add a fragile point to the map.
 */
static inline gr_error_t gr_fragility_map_add_point(
    gr_fragility_map_t*   map,
    const double*         coordinates,
    double                fragility_score,
    double                curvature,
    double                gradient_norm,
    int                   near_constraint)
{
    gr_error_t err = gr_fragility_map_ensure_capacity(map, map->num_points + 1);
    if (err != GR_SUCCESS) return err;
    
    gr_fragility_point_t* point = &map->points[map->num_points];
    
    err = gr_fragility_point_init(
        point,
        map->ctx,
        map->space->num_dims,
        coordinates
    );
    if (err != GR_SUCCESS) return err;
    
    point->fragility_score = fragility_score;
    point->curvature = curvature;
    point->gradient_norm = gradient_norm;
    point->near_constraint = near_constraint;
    
    map->num_points++;
    
    /* Update statistics */
    if (fragility_score > map->max_fragility) {
        map->max_fragility = fragility_score;
    }
    
    return GR_SUCCESS;
}

/* ============================================================================
 * Region Classification
 * ============================================================================ */

typedef enum gr_region_type {
    GR_REGION_STABLE,           /* Low fragility, safe to operate */
    GR_REGION_SENSITIVE,        /* Moderate fragility, monitor closely */
    GR_REGION_FRAGILE,          /* High fragility, reduce exposure */
    GR_REGION_CRITICAL          /* Very high fragility, immediate action needed */
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

#endif /* GR_INTERNAL_FRAGILITY_H */
