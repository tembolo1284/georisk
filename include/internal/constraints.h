/**
 * internal/constraints.h - Constraint surface internals
 * 
 * "In mathematics, every geometry is defined by axioms:
 *  1) which states are admissible
 *  2) which movements are possible
 *  3) which transitions carry a cost
 *  4) which constraints make certain paths unlikely or impossible"
 * 
 * Constraints define the boundaries of the admissible state space.
 * They are the walls, cliffs, and barriers that shape what is actually
 * possible regardless of what the pricing model says.
 * 
 * Types of constraints:
 *   - Hard constraints: Cannot be crossed (position limits, regulatory)
 *   - Soft constraints: Can be crossed at a cost (liquidity, margin)
 *   - Dynamic constraints: Move based on market conditions
 */

#ifndef GR_INTERNAL_CONSTRAINTS_H
#define GR_INTERNAL_CONSTRAINTS_H

#include "georisk.h"
#include "core.h"
#include <math.h>

/* ============================================================================
 * Constraint Definition
 * ============================================================================ */

#define GR_MAX_CONSTRAINTS 64
#define GR_MAX_CONSTRAINT_NAME 64

typedef enum gr_constraint_hardness {
    GR_CONSTRAINT_HARD,     /* Cannot be violated under any circumstances */
    GR_CONSTRAINT_SOFT,     /* Can be violated at a cost */
    GR_CONSTRAINT_DYNAMIC   /* Threshold changes based on other factors */
} gr_constraint_hardness_t;

typedef enum gr_constraint_direction {
    GR_CONSTRAINT_UPPER,    /* Value must be <= threshold */
    GR_CONSTRAINT_LOWER,    /* Value must be >= threshold */
    GR_CONSTRAINT_EQUALITY  /* Value must be == threshold (with tolerance) */
} gr_constraint_direction_t;

/**
 * Constraint evaluation function type.
 * 
 * For simple threshold constraints, this is just coordinates[dim] vs threshold.
 * For complex constraints, this computes an arbitrary function of coordinates.
 * 
 * Returns the "constraint value" - the quantity being constrained.
 */
typedef double (*gr_constraint_eval_fn)(
    const double* coordinates,
    int           num_dims,
    void*         user_data
);

typedef struct gr_constraint {
    gr_constraint_type_t      type;
    gr_constraint_hardness_t  hardness;
    gr_constraint_direction_t direction;
    char                      name[GR_MAX_CONSTRAINT_NAME];
    
    /* Simple threshold constraint (if eval_fn is NULL) */
    int                       dimension;    /* Which dimension this constrains */
    double                    threshold;    /* The limit value */
    double                    tolerance;    /* For equality constraints */
    
    /* Complex constraint (if eval_fn is not NULL) */
    gr_constraint_eval_fn     eval_fn;
    void*                     user_data;
    
    /* Soft constraint parameters */
    double                    penalty_rate; /* Cost per unit of violation */
    
    /* State */
    int                       active;       /* Is this constraint enabled? */
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
 * Constraint Evaluation Helpers
 * ============================================================================ */

/**
 * Get the constrained value for a simple threshold constraint.
 */
static inline double gr_constraint_get_value(
    const gr_constraint_t* constraint,
    const double*          coordinates,
    int                    num_dims)
{
    if (constraint->eval_fn) {
        return constraint->eval_fn(coordinates, num_dims, constraint->user_data);
    }
    
    if (constraint->dimension >= 0 && constraint->dimension < num_dims) {
        return coordinates[constraint->dimension];
    }
    
    return 0.0;
}

/**
 * Check if a single constraint is violated.
 * Returns 1 if violated, 0 if satisfied.
 */
static inline int gr_constraint_is_violated(
    const gr_constraint_t* constraint,
    const double*          coordinates,
    int                    num_dims)
{
    if (!constraint->active) return 0;
    
    double value = gr_constraint_get_value(constraint, coordinates, num_dims);
    
    switch (constraint->direction) {
        case GR_CONSTRAINT_UPPER:
            return value > constraint->threshold;
            
        case GR_CONSTRAINT_LOWER:
            return value < constraint->threshold;
            
        case GR_CONSTRAINT_EQUALITY:
            return fabs(value - constraint->threshold) > constraint->tolerance;
    }
    
    return 0;
}

/**
 * Compute signed distance to constraint boundary.
 * 
 * Positive = inside (satisfied)
 * Negative = outside (violated)
 * Zero = exactly on boundary
 * 
 * For soft constraints, this can be used to compute penalty.
 */
static inline double gr_constraint_signed_distance(
    const gr_constraint_t* constraint,
    const double*          coordinates,
    int                    num_dims)
{
    if (!constraint->active) return INFINITY;
    
    double value = gr_constraint_get_value(constraint, coordinates, num_dims);
    
    switch (constraint->direction) {
        case GR_CONSTRAINT_UPPER:
            /* Positive when value < threshold */
            return constraint->threshold - value;
            
        case GR_CONSTRAINT_LOWER:
            /* Positive when value > threshold */
            return value - constraint->threshold;
            
        case GR_CONSTRAINT_EQUALITY:
            /* Positive when within tolerance */
            return constraint->tolerance - fabs(value - constraint->threshold);
    }
    
    return 0.0;
}

/**
 * Compute penalty for constraint violation (soft constraints only).
 */
static inline double gr_constraint_penalty(
    const gr_constraint_t* constraint,
    const double*          coordinates,
    int                    num_dims)
{
    if (!constraint->active) return 0.0;
    if (constraint->hardness == GR_CONSTRAINT_HARD) return 0.0;
    
    double dist = gr_constraint_signed_distance(constraint, coordinates, num_dims);
    
    if (dist >= 0.0) return 0.0;  /* Not violated */
    
    /* Penalty proportional to violation */
    return constraint->penalty_rate * (-dist);
}

/**
 * Find minimum distance to any constraint surface.
 */
static inline double gr_constraints_min_distance(
    const gr_constraint_surface_t* surface,
    const double*                  coordinates,
    int                            num_dims)
{
    double min_dist = INFINITY;
    
    for (int i = 0; i < surface->num_constraints; i++) {
        double dist = gr_constraint_signed_distance(
            &surface->constraints[i],
            coordinates,
            num_dims
        );
        if (dist < min_dist) {
            min_dist = dist;
        }
    }
    
    return min_dist;
}

/**
 * Find index of the nearest (most binding) constraint.
 * Returns -1 if no constraints exist.
 */
static inline int gr_constraints_nearest_index(
    const gr_constraint_surface_t* surface,
    const double*                  coordinates,
    int                            num_dims)
{
    double min_dist = INFINITY;
    int nearest = -1;
    
    for (int i = 0; i < surface->num_constraints; i++) {
        double dist = gr_constraint_signed_distance(
            &surface->constraints[i],
            coordinates,
            num_dims
        );
        if (dist < min_dist) {
            min_dist = dist;
            nearest = i;
        }
    }
    
    return nearest;
}

/**
 * Compute total penalty from all soft constraints.
 */
static inline double gr_constraints_total_penalty(
    const gr_constraint_surface_t* surface,
    const double*                  coordinates,
    int                            num_dims)
{
    double total = 0.0;
    
    for (int i = 0; i < surface->num_constraints; i++) {
        total += gr_constraint_penalty(
            &surface->constraints[i],
            coordinates,
            num_dims
        );
    }
    
    return total;
}

/**
 * Check if any hard constraint is violated.
 */
static inline int gr_constraints_any_hard_violation(
    const gr_constraint_surface_t* surface,
    const double*                  coordinates,
    int                            num_dims)
{
    for (int i = 0; i < surface->num_constraints; i++) {
        const gr_constraint_t* c = &surface->constraints[i];
        if (c->hardness == GR_CONSTRAINT_HARD) {
            if (gr_constraint_is_violated(c, coordinates, num_dims)) {
                return 1;
            }
        }
    }
    return 0;
}

/* ============================================================================
 * Constraint Initialization Helpers
 * ============================================================================ */

/**
 * Initialize a simple threshold constraint.
 */
static inline void gr_constraint_init_threshold(
    gr_constraint_t*          constraint,
    gr_constraint_type_t      type,
    const char*               name,
    int                       dimension,
    gr_constraint_direction_t direction,
    double                    threshold,
    gr_constraint_hardness_t  hardness)
{
    constraint->type = type;
    constraint->hardness = hardness;
    constraint->direction = direction;
    constraint->dimension = dimension;
    constraint->threshold = threshold;
    constraint->tolerance = 1e-9;
    constraint->eval_fn = NULL;
    constraint->user_data = NULL;
    constraint->penalty_rate = 1.0;
    constraint->active = 1;
    
    /* Copy name safely */
    size_t len = 0;
    if (name) {
        while (name[len] && len < GR_MAX_CONSTRAINT_NAME - 1) {
            constraint->name[len] = name[len];
            len++;
        }
    }
    constraint->name[len] = '\0';
}

/**
 * Initialize a custom function constraint.
 */
static inline void gr_constraint_init_custom(
    gr_constraint_t*          constraint,
    gr_constraint_type_t      type,
    const char*               name,
    gr_constraint_eval_fn     eval_fn,
    void*                     user_data,
    gr_constraint_direction_t direction,
    double                    threshold,
    gr_constraint_hardness_t  hardness)
{
    constraint->type = type;
    constraint->hardness = hardness;
    constraint->direction = direction;
    constraint->dimension = -1;  /* Not used for custom */
    constraint->threshold = threshold;
    constraint->tolerance = 1e-9;
    constraint->eval_fn = eval_fn;
    constraint->user_data = user_data;
    constraint->penalty_rate = 1.0;
    constraint->active = 1;
    
    size_t len = 0;
    if (name) {
        while (name[len] && len < GR_MAX_CONSTRAINT_NAME - 1) {
            constraint->name[len] = name[len];
            len++;
        }
    }
    constraint->name[len] = '\0';
}

/* ============================================================================
 * Common Constraint Factories
 * ============================================================================ */

/**
 * Create a liquidity constraint: bid-ask spread must be <= threshold.
 */
static inline void gr_constraint_liquidity(
    gr_constraint_t* constraint,
    int              spread_dimension,
    double           max_spread)
{
    gr_constraint_init_threshold(
        constraint,
        GR_CONSTRAINT_LIQUIDITY,
        "max_bid_ask_spread",
        spread_dimension,
        GR_CONSTRAINT_UPPER,
        max_spread,
        GR_CONSTRAINT_SOFT
    );
    constraint->penalty_rate = 100.0;  /* High penalty for illiquidity */
}

/**
 * Create a position limit constraint.
 */
static inline void gr_constraint_position_limit(
    gr_constraint_t* constraint,
    int              position_dimension,
    double           max_position)
{
    gr_constraint_init_threshold(
        constraint,
        GR_CONSTRAINT_POSITION_LIMIT,
        "max_position",
        position_dimension,
        GR_CONSTRAINT_UPPER,
        max_position,
        GR_CONSTRAINT_HARD
    );
}

/**
 * Create a margin constraint: margin ratio must be >= threshold.
 */
static inline void gr_constraint_margin(
    gr_constraint_t* constraint,
    int              margin_dimension,
    double           min_margin_ratio)
{
    gr_constraint_init_threshold(
        constraint,
        GR_CONSTRAINT_MARGIN,
        "min_margin_ratio",
        margin_dimension,
        GR_CONSTRAINT_LOWER,
        min_margin_ratio,
        GR_CONSTRAINT_SOFT
    );
    constraint->penalty_rate = 50.0;
}

#endif /* GR_INTERNAL_CONSTRAINTS_H */
