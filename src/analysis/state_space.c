/**
 * state_space.c - State space implementation
 * 
 * "Geometric risk arises before data. It does not start from prices,
 *  but from the space in which the system moves."
 * 
 * The state space defines the manifold on which we analyze risk.
 * Each dimension represents a risk factor (spot, vol, rate, etc.)
 * and the discretization defines the resolution of our analysis.
 */

#include "georisk.h"
#include "internal/core.h"
#include "internal/allocator.h"
#include "internal/state_space.h"
#include <string.h>

/* ============================================================================
 * State Space Creation and Destruction
 * ============================================================================ */

GR_API gr_state_space_t* gr_state_space_new(gr_context_t* ctx)
{
    if (!ctx) return NULL;
    
    gr_state_space_t* space = GR_CTX_ALLOC(ctx, gr_state_space_t);
    if (!space) {
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY, "Failed to allocate state space");
        return NULL;
    }
    
    space->ctx = ctx;
    space->num_dims = 0;
    space->total_points = 0;
    space->prices = NULL;
    space->prices_valid = 0;
    
    /* Initialize strides to zero */
    for (int i = 0; i < GR_MAX_DIMENSIONS; i++) {
        space->strides[i] = 0;
        space->dims[i].name = NULL;
        space->dims[i].grid = NULL;
    }
    
    return space;
}

GR_API void gr_state_space_free(gr_state_space_t* space)
{
    if (!space) return;
    
    gr_context_t* ctx = space->ctx;
    
    /* Free dimension resources */
    for (int i = 0; i < space->num_dims; i++) {
        gr_dimension_free(&space->dims[i], ctx);
    }
    
    /* Free cached prices */
    if (space->prices) {
        gr_ctx_free(ctx, space->prices);
        space->prices = NULL;
    }
    
    /* Free the space itself */
    gr_ctx_free(ctx, space);
}

/* ============================================================================
 * Dimension Management
 * ============================================================================ */

GR_API gr_error_t gr_state_space_add_dimension(
    gr_state_space_t*     space,
    const gr_dimension_t* dim)
{
    if (!space) return GR_ERROR_NULL_POINTER;
    if (!dim) return GR_ERROR_NULL_POINTER;
    
    gr_context_t* ctx = space->ctx;
    
    /* Check capacity */
    if (space->num_dims >= GR_MAX_DIMENSIONS) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT,
                     "Maximum dimensions exceeded");
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    /* Validate dimension parameters */
    if (dim->num_points < 2) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT,
                     "Dimension must have at least 2 points");
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    if (dim->min_value >= dim->max_value) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT,
                     "Dimension min must be less than max");
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    /* Copy dimension into internal storage */
    gr_dimension_internal_t* internal = &space->dims[space->num_dims];
    
    internal->type = dim->type;
    internal->min_value = dim->min_value;
    internal->max_value = dim->max_value;
    internal->current = dim->current;
    internal->num_points = dim->num_points;
    
    /* Copy name */
    if (dim->name) {
        internal->name = gr_ctx_strdup(ctx, dim->name);
        if (!internal->name) {
            gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                         "Failed to allocate dimension name");
            return GR_ERROR_OUT_OF_MEMORY;
        }
    } else {
        internal->name = NULL;
    }
    
    /* Build the grid */
    gr_error_t err = gr_dimension_build_grid(internal, ctx);
    if (err != GR_SUCCESS) {
        if (internal->name) {
            gr_ctx_free(ctx, internal->name);
            internal->name = NULL;
        }
        gr_set_error(ctx, err, "Failed to build dimension grid");
        return err;
    }
    
    space->num_dims++;
    
    /* Recompute strides */
    gr_state_space_compute_strides(space);
    
    /* Invalidate cached prices */
    space->prices_valid = 0;
    
    return GR_SUCCESS;
}

GR_API int gr_state_space_get_num_dimensions(const gr_state_space_t* space)
{
    if (!space) return 0;
    return space->num_dims;
}

GR_API size_t gr_state_space_get_total_points(const gr_state_space_t* space)
{
    if (!space) return 0;
    return space->total_points;
}

/* ============================================================================
 * Price Mapping
 * ============================================================================ */

GR_API gr_error_t gr_state_space_map_prices(
    gr_state_space_t* space,
    gr_pricing_fn     fn,
    void*             user_data)
{
    if (!space) return GR_ERROR_NULL_POINTER;
    if (!fn) return GR_ERROR_NULL_POINTER;
    
    gr_context_t* ctx = space->ctx;
    
    if (space->num_dims == 0) {
        gr_set_error(ctx, GR_ERROR_NOT_INITIALIZED,
                     "State space has no dimensions");
        return GR_ERROR_NOT_INITIALIZED;
    }
    
    /* Allocate price storage if needed */
    if (!space->prices) {
        space->prices = (double*)gr_ctx_calloc(
            ctx, 
            space->total_points, 
            sizeof(double)
        );
        if (!space->prices) {
            gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                         "Failed to allocate price grid");
            return GR_ERROR_OUT_OF_MEMORY;
        }
    }
    
    /* Temporary buffer for coordinates */
    double coords[GR_MAX_DIMENSIONS];
    
    /* Iterate over all grid points */
    for (size_t flat = 0; flat < space->total_points; flat++) {
        /* Get coordinates for this point */
        gr_state_space_get_coordinates(space, flat, coords);
        
        /* Compute price */
        double price = fn(coords, space->num_dims, user_data);
        space->prices[flat] = price;
    }
    
    space->prices_valid = 1;
    
    return GR_SUCCESS;
}

/* ============================================================================
 * Internal Helpers (exposed for other modules)
 * ============================================================================ */

/**
 * Get price at a grid point (by flat index).
 * Returns 0.0 if prices not computed or index out of range.
 */
double gr_state_space_get_price(const gr_state_space_t* space, size_t flat_index)
{
    if (!space || !space->prices_valid || !space->prices) {
        return 0.0;
    }
    
    if (flat_index >= space->total_points) {
        return 0.0;
    }
    
    return space->prices[flat_index];
}

/**
 * Get price at coordinates (with interpolation to nearest grid point).
 * For more accurate interpolation, use gr_state_space_interpolate_price.
 */
double gr_state_space_get_price_at(
    const gr_state_space_t* space,
    const double*           coordinates)
{
    if (!space || !space->prices_valid || !space->prices) {
        return 0.0;
    }
    
    size_t flat = gr_state_space_nearest_index(space, coordinates);
    return space->prices[flat];
}

/**
 * Multilinear interpolation of price at arbitrary coordinates.
 * 
 * For n dimensions, this does 2^n weighted evaluations.
 * More accurate than nearest-neighbor but more expensive.
 */
double gr_state_space_interpolate_price(
    const gr_state_space_t* space,
    const double*           coordinates)
{
    if (!space || !space->prices_valid || !space->prices) {
        return 0.0;
    }
    
    int n = space->num_dims;
    
    /* Find bounding grid indices for each dimension */
    int lo[GR_MAX_DIMENSIONS];
    int hi[GR_MAX_DIMENSIONS];
    double t[GR_MAX_DIMENSIONS];  /* Interpolation parameter [0,1] */
    
    for (int d = 0; d < n; d++) {
        const gr_dimension_internal_t* dim = &space->dims[d];
        double val = coordinates[d];
        
        /* Clamp to grid range */
        if (val <= dim->min_value) {
            lo[d] = 0;
            hi[d] = 0;
            t[d] = 0.0;
        } else if (val >= dim->max_value) {
            lo[d] = dim->num_points - 1;
            hi[d] = dim->num_points - 1;
            t[d] = 0.0;
        } else {
            /* Find bracketing indices */
            for (int i = 0; i < dim->num_points - 1; i++) {
                if (val >= dim->grid[i] && val <= dim->grid[i + 1]) {
                    lo[d] = i;
                    hi[d] = i + 1;
                    double range = dim->grid[i + 1] - dim->grid[i];
                    t[d] = (range > 1e-15) ? (val - dim->grid[i]) / range : 0.0;
                    break;
                }
            }
        }
    }
    
    /* Multilinear interpolation: sum over 2^n corners */
    double result = 0.0;
    int num_corners = 1 << n;  /* 2^n */
    
    for (int corner = 0; corner < num_corners; corner++) {
        /* Build index for this corner */
        int indices[GR_MAX_DIMENSIONS];
        double weight = 1.0;
        
        for (int d = 0; d < n; d++) {
            int use_hi = (corner >> d) & 1;
            indices[d] = use_hi ? hi[d] : lo[d];
            weight *= use_hi ? t[d] : (1.0 - t[d]);
        }
        
        /* Get price at this corner */
        size_t flat = gr_state_space_flat_index(space, indices);
        result += weight * space->prices[flat];
    }
    
    return result;
}

/**
 * Get the grid value for a dimension at a given index.
 */
double gr_state_space_get_grid_value(
    const gr_state_space_t* space,
    int                     dim,
    int                     index)
{
    if (!space || dim < 0 || dim >= space->num_dims) {
        return 0.0;
    }
    
    const gr_dimension_internal_t* d = &space->dims[dim];
    
    if (index < 0 || index >= d->num_points) {
        return 0.0;
    }
    
    return d->grid[index];
}
