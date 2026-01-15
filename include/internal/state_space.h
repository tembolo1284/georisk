/**
 * internal/state_space.h - State space internals
 */

#ifndef GR_INTERNAL_STATE_SPACE_H
#define GR_INTERNAL_STATE_SPACE_H

#include "georisk.h"
#include "allocator.h"
#include <string.h>
#include <math.h>

#ifndef GR_MAX_DIMENSIONS
#define GR_MAX_DIMENSIONS 16
#endif

/* ============================================================================
 * Internal Dimension Structure
 * ============================================================================ */

typedef struct gr_dimension_internal {
    gr_dimension_type_t type;
    char*               name;
    double              min_value;
    double              max_value;
    double              current;
    int                 num_points;
    double*             grid;
} gr_dimension_internal_t;

/* ============================================================================
 * State Space Structure
 * ============================================================================ */

struct gr_state_space_s {
    gr_context_t*           ctx;
    int                     num_dims;
    gr_dimension_internal_t dims[GR_MAX_DIMENSIONS];
    size_t                  total_points;
    size_t                  strides[GR_MAX_DIMENSIONS];
    double*                 prices;
    int                     prices_valid;
};

/* ============================================================================
 * Dimension Helpers
 * ============================================================================ */

static inline gr_error_t gr_dimension_build_grid(
    gr_dimension_internal_t* dim, 
    gr_context_t* ctx)
{
    dim->grid = (double*)gr_ctx_calloc(ctx, (size_t)dim->num_points, sizeof(double));
    if (!dim->grid) return GR_ERROR_OUT_OF_MEMORY;
    
    double step = (dim->max_value - dim->min_value) / (double)(dim->num_points - 1);
    for (int i = 0; i < dim->num_points; i++) {
        dim->grid[i] = dim->min_value + (double)i * step;
    }
    
    return GR_SUCCESS;
}

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

/* ============================================================================
 * Grid Index Helpers
 * ============================================================================ */

static inline void gr_state_space_compute_strides(gr_state_space_t* space)
{
    if (space->num_dims == 0) {
        space->total_points = 0;
        return;
    }
    
    space->strides[space->num_dims - 1] = 1;
    for (int i = space->num_dims - 2; i >= 0; i--) {
        space->strides[i] = space->strides[i + 1] * (size_t)space->dims[i + 1].num_points;
    }
    
    space->total_points = space->strides[0] * (size_t)space->dims[0].num_points;
}

static inline size_t gr_state_space_flat_index(
    const gr_state_space_t* space, 
    const int* indices)
{
    size_t flat = 0;
    for (int i = 0; i < space->num_dims; i++) {
        flat += (size_t)indices[i] * space->strides[i];
    }
    return flat;
}

static inline void gr_state_space_multi_index(
    const gr_state_space_t* space,
    size_t                  flat,
    int*                    indices)
{
    for (int i = 0; i < space->num_dims; i++) {
        indices[i] = (int)(flat / space->strides[i]);
        flat = flat % space->strides[i];
    }
}

static inline void gr_state_space_get_coordinates(
    const gr_state_space_t* space,
    size_t                  flat,
    double*                 coords)
{
    int indices[GR_MAX_DIMENSIONS];
    gr_state_space_multi_index(space, flat, indices);
    
    for (int i = 0; i < space->num_dims; i++) {
        coords[i] = space->dims[i].grid[indices[i]];
    }
}

static inline size_t gr_state_space_nearest_index(
    const gr_state_space_t* space,
    const double*           coords)
{
    int indices[GR_MAX_DIMENSIONS];
    
    for (int d = 0; d < space->num_dims; d++) {
        const gr_dimension_internal_t* dim = &space->dims[d];
        double val = coords[d];
        
        int best = 0;
        double best_dist = 1e300;
        
        for (int i = 0; i < dim->num_points; i++) {
            double dist = fabs(dim->grid[i] - val);
            if (dist < best_dist) {
                best_dist = dist;
                best = i;
            }
        }
        
        indices[d] = best;
    }
    
    return gr_state_space_flat_index(space, indices);
}

/* gr_state_space_interpolate_price is implemented in state_space.c */
double gr_state_space_interpolate_price(const gr_state_space_t* space, const double* coords);

#endif /* GR_INTERNAL_STATE_SPACE_H */
