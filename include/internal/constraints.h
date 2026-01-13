/**
 * internal/constraints.h - Constraint surface internals
 */

#ifndef GR_INTERNAL_CONSTRAINTS_H
#define GR_INTERNAL_CONSTRAINTS_H

#include "georisk.h"
#include <math.h>
#include <string.h>

#define GR_MAX_CONSTRAINTS 64

/* ============================================================================
 * Constraint Types
 * ============================================================================ */

typedef enum gr_constraint_type {
    GR_CONSTRAINT_LIQUIDITY,
    GR_CONSTRAINT_POSITION_LIMIT,
    GR_CONSTRAINT_MARGIN,
    GR_CONSTRAINT_REGULATORY,
    GR_CONSTRAINT_CUSTOM
} gr_constraint_type_t;

typedef enum gr_constraint_hardness {
    GR_CONSTRAINT_HARD,
    GR_CONSTRAINT_SOFT,
    GR_CONSTRAINT_DYNAMIC
} gr_constraint_hardness_t;

typedef enum gr_constraint_direction {
    GR_CONSTRAINT_UPPER,
    GR_CONSTRAINT_LOWER,
    GR_CONSTRAINT_EQUALITY
} gr_constraint_direction_t;

/* Custom constraint evaluation function */
typedef double (*gr_constraint_eval_fn)(const double* coords, int num_dims, void* user_data);

/* ============================================================================
 * Constraint Structure
 * ============================================================================ */

typedef struct gr_constraint {
    gr_constraint_type_t      type;
    char                      name[64];
    int                       active;
    
    /* Simple threshold constraint */
    int                       dimension;
    gr_constraint_direction_t direction;
    double                    threshold;
    gr_constraint_hardness_t  hardness;
    double                    penalty_rate;
    
    /* Custom constraint */
    gr_constraint_eval_fn     eval_fn;
    void*                     user_data;
} gr_constraint_t;

/* ============================================================================
 * Constraint Surface Structure
 * ============================================================================ */

struct gr_constraint_surface_s {
    gr_context_t*    ctx;
    gr_constraint_t  constraints[GR_MAX_CONSTRAINTS];
    int              num_constraints;
};

/* ============================================================================
 * Constraint Initialization
 * ============================================================================ */

static inline void gr_constraint_init_threshold(
    gr_constraint_t*          c,
    gr_constraint_type_t      type,
    const char*               name,
    int                       dimension,
    gr_constraint_direction_t direction,
    double                    threshold,
    gr_constraint_hardness_t  hardness)
{
    c->type = type;
    if (name) {
        strncpy(c->name, name, sizeof(c->name) - 1);
        c->name[sizeof(c->name) - 1] = '\0';
    }
    c->active = 1;
    c->dimension = dimension;
    c->direction = direction;
    c->threshold = threshold;
    c->hardness = hardness;
    c->penalty_rate = 10.0;
    c->eval_fn = NULL;
    c->user_data = NULL;
}

static inline void gr_constraint_init_custom(
    gr_constraint_t*          c,
    gr_constraint_type_t      type,
    const char*               name,
    gr_constraint_eval_fn     eval_fn,
    void*                     user_data,
    gr_constraint_direction_t direction,
    double                    threshold,
    gr_constraint_hardness_t  hardness)
{
    gr_constraint_init_threshold(c, type, name, -1, direction, threshold, hardness);
    c->eval_fn = eval_fn;
    c->user_data = user_data;
}

/* ============================================================================
 * Constraint Evaluation
 * ============================================================================ */

static inline double gr_constraint_evaluate(
    const gr_constraint_t* c,
    const double*          coords,
    int                    num_dims)
{
    if (c->eval_fn) {
        return c->eval_fn(coords, num_dims, c->user_data);
    }
    
    if (c->dimension >= 0 && c->dimension < num_dims) {
        return coords[c->dimension];
    }
    
    return 0.0;
}

static inline int gr_constraint_is_violated(
    const gr_constraint_t* c,
    const double*          coords,
    int                    num_dims)
{
    if (!c->active) return 0;
    
    double val = gr_constraint_evaluate(c, coords, num_dims);
    
    switch (c->direction) {
        case GR_CONSTRAINT_UPPER:
            return val > c->threshold;
        case GR_CONSTRAINT_LOWER:
            return val < c->threshold;
        case GR_CONSTRAINT_EQUALITY:
            return fabs(val - c->threshold) > 1e-10;
    }
    
    return 0;
}

static inline double gr_constraint_signed_distance(
    const gr_constraint_t* c,
    const double*          coords,
    int                    num_dims)
{
    double val = gr_constraint_evaluate(c, coords, num_dims);
    
    switch (c->direction) {
        case GR_CONSTRAINT_UPPER:
            return c->threshold - val;
        case GR_CONSTRAINT_LOWER:
            return val - c->threshold;
        case GR_CONSTRAINT_EQUALITY:
            return -fabs(val - c->threshold);
    }
    
    return 0.0;
}

/* ============================================================================
 * Surface Queries
 * ============================================================================ */

static inline double gr_constraints_min_distance(
    const gr_constraint_surface_t* surface,
    const double*                  coords,
    int                            num_dims)
{
    double min_dist = 1e300;
    
    for (int i = 0; i < surface->num_constraints; i++) {
        if (!surface->constraints[i].active) continue;
        
        double dist = gr_constraint_signed_distance(&surface->constraints[i], coords, num_dims);
        if (dist < min_dist) {
            min_dist = dist;
        }
    }
    
    return min_dist;
}

static inline int gr_constraints_nearest_index(
    const gr_constraint_surface_t* surface,
    const double*                  coords,
    int                            num_dims)
{
    int nearest = -1;
    double min_dist = 1e300;
    
    for (int i = 0; i < surface->num_constraints; i++) {
        if (!surface->constraints[i].active) continue;
        
        double dist = gr_constraint_signed_distance(&surface->constraints[i], coords, num_dims);
        if (dist < min_dist) {
            min_dist = dist;
            nearest = i;
        }
    }
    
    return nearest;
}

#endif /* GR_INTERNAL_CONSTRAINTS_H */
