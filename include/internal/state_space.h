/**
 * internal/state_space.h - State space internal structure
 * 
 * "Geometric risk arises before data. It does not start from prices,
 *  but from the space in which the system moves."
 * 
 * The state space defines the geometry of possibilities—which states
 * are admissible, how they connect, and how we discretize the manifold
 * for numerical analysis.
 */

#ifndef GR_INTERNAL_STATE_SPACE_H
#define GR_INTERNAL_STATE_SPACE_H

#include "georisk.h"
#include "core.h"

/* ============================================================================
 * Dimension Storage (internal copy of user-provided dimension)
 * ============================================================================ */

typedef struct gr_dimension_internal {
    gr_dimension_type_t type;
    char*               name;       /* Owned copy */
    double              min_value;
    double              max_value;
    double              current;
    double              step_size;
    int                 num_points;
    double*             grid;       /* Precomputed grid points: [num_points] */
} gr_dimension_internal_t;

/* ============================================================================
 * State Space Structure
 * ============================================================================ */

struct gr_state_space_s {
    gr_context_t*            ctx;           /* Parent context (not owned) */
    
    /* Dimensions */
    gr_dimension_internal_t  dims[GR_MAX_DIMENSIONS];
    int                      num_dims;
    
    /* Total grid */
    size_t                   total_points;  /* Product of all num_points */
    
    /* Cached price values across the grid (optional, computed on demand) */
    double*                  prices;        /* [total_points] or NULL */
    int                      prices_valid;
    
    /* Strides for indexing into flattened grid */
    size_t                   strides[GR_MAX_DIMENSIONS];
};

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * Convert multi-dimensional index to flat index.
 * indices: array of [num_dims] indices, each in [0, dims[i].num_points)
 */
static inline size_t gr_state_space_flat_index(
    const gr_state_space_t* space,
    const int*              indices)
{
    size_t flat = 0;
    for (int d = 0; d < space->num_dims; d++) {
        flat += (size_t)indices[d] * space->strides[d];
    }
    return flat;
}

/**
 * Convert flat index to multi-dimensional indices.
 * out_indices: array of [num_dims] to receive indices
 */
static inline void gr_state_space_multi_index(
    const gr_state_space_t* space,
    size_t                  flat,
    int*                    out_indices)
{
    for (int d = 0; d < space->num_dims; d++) {
        out_indices[d] = (int)(flat / space->strides[d]);
        flat %= space->strides[d];
    }
}

/**
 * Get coordinates (actual values) for a flat index.
 * out_coords: array of [num_dims] doubles
 */
static inline void gr_state_space_get_coordinates(
    const gr_state_space_t* space,
    size_t                  flat,
    double*                 out_coords)
{
    int indices[GR_MAX_DIMENSIONS];
    gr_state_space_multi_index(space, flat, indices);
    
    for (int d = 0; d < space->num_dims; d++) {
        out_coords[d] = space->dims[d].grid[indices[d]];
    }
}

/**
 * Find nearest grid index for a coordinate value in a dimension.
 */
static inline int gr_dimension_nearest_index(
    const gr_dimension_internal_t* dim,
    double                         value)
{
    if (value <= dim->min_value) return 0;
    if (value >= dim->max_value) return dim->num_points - 1;
    
    /* Linear search (could optimize with binary search for large grids) */
    for (int i = 0; i < dim->num_points - 1; i++) {
        if (value >= dim->grid[i] && value <= dim->grid[i + 1]) {
            /* Return closer one */
            double d0 = value - dim->grid[i];
            double d1 = dim->grid[i + 1] - value;
            return (d0 <= d1) ? i : i + 1;
        }
    }
    return dim->num_points - 1;
}

/**
 * Find flat index for nearest grid point to given coordinates.
 */
static inline size_t gr_state_space_nearest_index(
    const gr_state_space_t* space,
    const double*           coords)
{
    int indices[GR_MAX_DIMENSIONS];
    for (int d = 0; d < space->num_dims; d++) {
        indices[d] = gr_dimension_nearest_index(&space->dims[d], coords[d]);
    }
    return gr_state_space_flat_index(space, indices);
}

/**
 * Recompute strides after dimensions change.
 * Strides are in row-major order (last dimension varies fastest).
 */
static inline void gr_state_space_compute_strides(gr_state_space_t* space)
{
    if (space->num_dims == 0) {
        space->total_points = 0;
        return;
    }
    
    /* Last dimension has stride 1 */
    space->strides[space->num_dims - 1] = 1;
    
    /* Work backwards */
    for (int d = space->num_dims - 2; d >= 0; d--) {
        space->strides[d] = space->strides[d + 1] * (size_t)space->dims[d + 1].num_points;
    }
    
    /* Total points */
    space->total_points = space->strides[0] * (size_t)space->dims[0].num_points;
}

/**
 * Build uniform grid for a dimension.
 */
static inline gr_error_t gr_dimension_build_grid(
    gr_dimension_internal_t* dim,
    gr_context_t*            ctx)
{
    if (dim->num_points < 2) {
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    dim->grid = (double*)gr_ctx_calloc(ctx, (size_t)dim->num_points, sizeof(double));
    if (!dim->grid) {
        return GR_ERROR_OUT_OF_MEMORY;
    }
    
    double range = dim->max_value - dim->min_value;
    double step = range / (double)(dim->num_points - 1);
    dim->step_size = step;
    
    for (int i = 0; i < dim->num_points; i++) {
        dim->grid[i] = dim->min_value + (double)i * step;
    }
    
    /* Ensure last point is exactly max (avoid floating point drift) */
    dim->grid[dim->num_points - 1] = dim->max_value;
    
    return GR_SUCCESS;
}

/**
 * Free dimension resources.
 */
static inline void gr_dimension_free(gr_dimension_internal_t* dim, gr_context_t* ctx)
{
    if (dim->name) {
        gr_ctx_free(ctx, dim->name);
        dim->name = NULL;
    }
    if (dim->grid) {
        gr_ctx_free(ctx, dim->grid);
        dim->grid = NULL;
    }
}

#endif /* GR_INTERNAL_STATE_SPACE_H */
