/**
 * metric.c - Transport metric implementation
 * 
 * "Liquidity, funding, concentration, endogeneity, transport rigidity
 *  are not independent factors but can be viewed as coordinates on a surface."
 * 
 * The transport metric defines the "cost" of moving between states.
 * In standard risk measures, all movements are treated equally—a 1%
 * move in spot is a 1% move regardless of position size or liquidity.
 * 
 * In reality:
 *   - Selling into illiquidity costs more than buying
 *   - Large positions have market impact
 *   - Funding constraints make some paths more expensive
 *   - Concentrated positions are harder to unwind
 * 
 * The transport metric encodes these costs as a Riemannian metric tensor.
 * Geodesic distance then gives the true "cost" of moving between states,
 * accounting for all these frictions.
 * 
 * ds² = Σ_ij g_ij(x) dx_i dx_j
 */

#include "georisk.h"
#include "internal/core.h"
#include "internal/allocator.h"
#include "internal/transport.h"
#include <string.h>
#include <math.h>

/* ============================================================================
 * Transport Metric Creation and Destruction
 * ============================================================================ */

GR_API gr_transport_metric_t* gr_transport_metric_new(gr_context_t* ctx)
{
    if (!ctx) return NULL;
    
    gr_transport_metric_t* metric = GR_CTX_ALLOC(ctx, gr_transport_metric_t);
    if (!metric) {
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                     "Failed to allocate transport metric");
        return NULL;
    }
    
    metric->ctx = ctx;
    metric->num_dims = 0;
    metric->num_samples = 0;
    metric->default_tensor = NULL;
    metric->use_identity = 1;  /* Default to Euclidean metric */
    metric->interpolation_radius = 0.0;  /* 0 = global interpolation */
    
    return metric;
}

GR_API void gr_transport_metric_free(gr_transport_metric_t* metric)
{
    if (!metric) return;
    
    gr_context_t* ctx = metric->ctx;
    
    /* Free samples */
    for (int i = 0; i < metric->num_samples; i++) {
        gr_metric_sample_free(&metric->samples[i], ctx);
    }
    
    /* Free default tensor */
    if (metric->default_tensor) {
        gr_ctx_free(ctx, metric->default_tensor);
    }
    
    gr_ctx_free(ctx, metric);
}

/* ============================================================================
 * Metric Configuration
 * ============================================================================ */

/**
 * Set the number of dimensions for the metric.
 * Must be called before adding samples.
 */
gr_error_t gr_transport_metric_set_dims(
    gr_transport_metric_t* metric,
    int                    num_dims)
{
    if (!metric) return GR_ERROR_NULL_POINTER;
    
    gr_context_t* ctx = metric->ctx;
    
    if (num_dims < 1 || num_dims > GR_MAX_DIMENSIONS) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT,
                     "Invalid number of dimensions");
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    if (metric->num_samples > 0) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT,
                     "Cannot change dimensions after adding samples");
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    metric->num_dims = num_dims;
    
    /* Allocate default tensor */
    if (metric->default_tensor) {
        gr_ctx_free(ctx, metric->default_tensor);
    }
    
    size_t tensor_size = (size_t)num_dims * (size_t)num_dims;
    metric->default_tensor = (double*)gr_ctx_calloc(ctx, tensor_size, sizeof(double));
    if (!metric->default_tensor) {
        gr_set_error(ctx, GR_ERROR_OUT_OF_MEMORY,
                     "Failed to allocate default tensor");
        return GR_ERROR_OUT_OF_MEMORY;
    }
    
    /* Initialize to identity */
    gr_metric_set_identity(metric->default_tensor, num_dims);
    
    return GR_SUCCESS;
}

/**
 * Set the default metric tensor (used where no samples exist).
 */
gr_error_t gr_transport_metric_set_default(
    gr_transport_metric_t* metric,
    const double*          tensor)
{
    if (!metric) return GR_ERROR_NULL_POINTER;
    if (!tensor) return GR_ERROR_NULL_POINTER;
    
    if (metric->num_dims == 0) {
        gr_set_error(metric->ctx, GR_ERROR_NOT_INITIALIZED,
                     "Set dimensions first");
        return GR_ERROR_NOT_INITIALIZED;
    }
    
    size_t tensor_size = (size_t)metric->num_dims * (size_t)metric->num_dims;
    
    for (size_t i = 0; i < tensor_size; i++) {
        metric->default_tensor[i] = tensor[i];
    }
    
    metric->use_identity = 0;
    
    return GR_SUCCESS;
}

/**
 * Set interpolation radius.
 * 0 = global (all samples contribute)
 * > 0 = only samples within radius contribute
 */
void gr_transport_metric_set_radius(
    gr_transport_metric_t* metric,
    double                 radius)
{
    if (!metric) return;
    metric->interpolation_radius = radius > 0.0 ? radius : 0.0;
}

/* ============================================================================
 * Metric Sampling
 * ============================================================================ */

GR_API gr_error_t gr_transport_metric_set(
    gr_transport_metric_t* metric,
    const double*          coordinates,
    int                    num_dims,
    const double*          tensor)
{
    if (!metric) return GR_ERROR_NULL_POINTER;
    if (!coordinates) return GR_ERROR_NULL_POINTER;
    if (!tensor) return GR_ERROR_NULL_POINTER;
    
    gr_context_t* ctx = metric->ctx;
    
    /* Initialize dimensions if not set */
    if (metric->num_dims == 0) {
        gr_error_t err = gr_transport_metric_set_dims(metric, num_dims);
        if (err != GR_SUCCESS) return err;
    }
    
    /* Validate dimensions */
    if (num_dims != metric->num_dims) {
        gr_set_error(ctx, GR_ERROR_DIMENSION_MISMATCH,
                     "Dimension mismatch in metric sample");
        return GR_ERROR_DIMENSION_MISMATCH;
    }
    
    /* Check capacity */
    if (metric->num_samples >= GR_TRANSPORT_MAX_SAMPLES) {
        gr_set_error(ctx, GR_ERROR_INVALID_ARGUMENT,
                     "Maximum metric samples exceeded");
        return GR_ERROR_INVALID_ARGUMENT;
    }
    
    /* Add sample */
    gr_error_t err = gr_metric_sample_init(
        &metric->samples[metric->num_samples],
        ctx,
        num_dims,
        coordinates,
        tensor
    );
    
    if (err != GR_SUCCESS) {
        gr_set_error(ctx, err, "Failed to add metric sample");
        return err;
    }
    
    metric->num_samples++;
    
    return GR_SUCCESS;
}

/* ============================================================================
 * Distance Computation
 * ============================================================================ */

GR_API double gr_transport_distance(
    const gr_transport_metric_t* metric,
    const double*                from,
    const double*                to,
    int                          num_dims)
{
    if (!metric) return 0.0;
    if (!from || !to) return 0.0;
    
    /* Validate dimensions */
    if (metric->num_dims == 0) {
        /* Not initialized - use Euclidean */
        return gr_euclidean_distance(from, to, num_dims);
    }
    
    if (num_dims != metric->num_dims) {
        return 0.0;  /* Dimension mismatch */
    }
    
    /* Use geodesic approximation */
    return gr_geodesic_distance_approx(metric, from, to);
}

/* ============================================================================
 * Metric Queries
 * ============================================================================ */

/**
 * Get the metric tensor at a point (interpolated if needed).
 */
gr_error_t gr_transport_metric_get_tensor(
    const gr_transport_metric_t* metric,
    const double*                coordinates,
    double*                      out_tensor)
{
    if (!metric) return GR_ERROR_NULL_POINTER;
    if (!coordinates) return GR_ERROR_NULL_POINTER;
    if (!out_tensor) return GR_ERROR_NULL_POINTER;
    
    if (metric->num_dims == 0) {
        return GR_ERROR_NOT_INITIALIZED;
    }
    
    gr_metric_interpolate(metric, coordinates, out_tensor);
    
    return GR_SUCCESS;
}

/**
 * Compute the "local cost" of an infinitesimal move at a point.
 * This is sqrt(dv^T G dv) for displacement dv.
 */
double gr_transport_local_cost(
    const gr_transport_metric_t* metric,
    const double*                coordinates,
    const double*                displacement,
    int                          num_dims)
{
    if (!metric || !coordinates || !displacement) return 0.0;
    if (metric->num_dims == 0 || num_dims != metric->num_dims) return 0.0;
    
    /* Get metric at this point */
    double tensor[GR_MAX_DIMENSIONS * GR_MAX_DIMENSIONS];
    gr_metric_interpolate(metric, coordinates, tensor);
    
    /* Compute infinitesimal distance */
    return gr_metric_infinitesimal_distance(tensor, displacement, num_dims);
}

/* ============================================================================
 * Convenience: Build Metric from Market Data
 * ============================================================================ */

/**
 * Build a diagonal metric from liquidity scores.
 * Higher liquidity = lower cost = smaller metric values.
 */
gr_error_t gr_transport_metric_from_liquidity(
    gr_transport_metric_t* metric,
    const double*          coordinates,
    const double*          liquidity,
    int                    num_dims)
{
    if (!metric || !coordinates || !liquidity) return GR_ERROR_NULL_POINTER;
    
    /* Build tensor */
    double tensor[GR_MAX_DIMENSIONS * GR_MAX_DIMENSIONS];
    gr_metric_from_liquidity(tensor, num_dims, liquidity);
    
    /* Add as sample */
    return gr_transport_metric_set(metric, coordinates, num_dims, tensor);
}

/**
 * Build a diagonal metric from market impact coefficients.
 */
gr_error_t gr_transport_metric_from_impact(
    gr_transport_metric_t* metric,
    const double*          coordinates,
    const double*          positions,
    const double*          impact_coeffs,
    int                    num_dims)
{
    if (!metric || !coordinates || !positions || !impact_coeffs) {
        return GR_ERROR_NULL_POINTER;
    }
    
    /* Build tensor */
    double tensor[GR_MAX_DIMENSIONS * GR_MAX_DIMENSIONS];
    gr_metric_from_impact(tensor, num_dims, positions, impact_coeffs);
    
    /* Add as sample */
    return gr_transport_metric_set(metric, coordinates, num_dims, tensor);
}

/* ============================================================================
 * Path Analysis
 * ============================================================================ */

/**
 * Compute total cost along a discrete path.
 * path: [num_waypoints * num_dims] array of coordinates
 */
double gr_transport_path_cost(
    const gr_transport_metric_t* metric,
    const double*                path,
    int                          num_waypoints,
    int                          num_dims)
{
    if (!metric || !path) return 0.0;
    if (num_waypoints < 2) return 0.0;
    if (metric->num_dims == 0 || num_dims != metric->num_dims) return 0.0;
    
    double total_cost = 0.0;
    
    for (int i = 0; i < num_waypoints - 1; i++) {
        const double* from = &path[i * num_dims];
        const double* to = &path[(i + 1) * num_dims];
        
        total_cost += gr_geodesic_distance_approx(metric, from, to);
    }
    
    return total_cost;
}

/**
 * Check if a direct path is "cheap" compared to Euclidean distance.
 * Returns ratio: transport_distance / euclidean_distance.
 * Ratio > 1 means path is expensive (high friction).
 * Ratio ≈ 1 means metric is close to Euclidean.
 */
double gr_transport_friction_ratio(
    const gr_transport_metric_t* metric,
    const double*                from,
    const double*                to,
    int                          num_dims)
{
    if (!metric || !from || !to) return 1.0;
    
    double euclidean = gr_euclidean_distance(from, to, num_dims);
    if (euclidean < 1e-15) return 1.0;
    
    double transport = gr_transport_distance(metric, from, to, num_dims);
    
    return transport / euclidean;
}
