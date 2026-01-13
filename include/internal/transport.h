/**
 * internal/transport.h - Transport metric internals
 */

#ifndef GR_INTERNAL_TRANSPORT_H
#define GR_INTERNAL_TRANSPORT_H

#include "georisk.h"
#include "allocator.h"
#include <math.h>

#define GR_TRANSPORT_MAX_SAMPLES 1024
#define GR_GEODESIC_STEPS 100

/* ============================================================================
 * Metric Sample
 * ============================================================================ */

typedef struct gr_metric_sample {
    double* coordinates;
    double* tensor;
    int     num_dims;
} gr_metric_sample_t;

static inline gr_error_t gr_metric_sample_init(
    gr_metric_sample_t* sample,
    gr_context_t*       ctx,
    int                 num_dims,
    const double*       coords,
    const double*       tensor)
{
    sample->num_dims = num_dims;
    
    sample->coordinates = (double*)gr_ctx_malloc(ctx, (size_t)num_dims * sizeof(double));
    if (!sample->coordinates) return GR_ERROR_OUT_OF_MEMORY;
    
    size_t tensor_size = (size_t)num_dims * (size_t)num_dims;
    sample->tensor = (double*)gr_ctx_malloc(ctx, tensor_size * sizeof(double));
    if (!sample->tensor) {
        gr_ctx_free(ctx, sample->coordinates);
        return GR_ERROR_OUT_OF_MEMORY;
    }
    
    for (int i = 0; i < num_dims; i++) {
        sample->coordinates[i] = coords[i];
    }
    
    for (size_t i = 0; i < tensor_size; i++) {
        sample->tensor[i] = tensor[i];
    }
    
    return GR_SUCCESS;
}

static inline void gr_metric_sample_free(gr_metric_sample_t* sample, gr_context_t* ctx)
{
    if (sample->coordinates) {
        gr_ctx_free(ctx, sample->coordinates);
        sample->coordinates = NULL;
    }
    if (sample->tensor) {
        gr_ctx_free(ctx, sample->tensor);
        sample->tensor = NULL;
    }
}

/* ============================================================================
 * Transport Metric Structure
 * ============================================================================ */

struct gr_transport_metric_s {
    gr_context_t*      ctx;
    int                num_dims;
    gr_metric_sample_t samples[GR_TRANSPORT_MAX_SAMPLES];
    int                num_samples;
    double*            default_tensor;
    int                use_identity;
    double             interpolation_radius;
};

/* ============================================================================
 * Metric Tensor Operations
 * ============================================================================ */

static inline void gr_metric_set_identity(double* tensor, int n)
{
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            tensor[i * n + j] = (i == j) ? 1.0 : 0.0;
        }
    }
}

static inline void gr_metric_set_diagonal(double* tensor, int n, const double* diag)
{
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            tensor[i * n + j] = (i == j) ? diag[i] : 0.0;
        }
    }
}

static inline double gr_metric_quadratic_form(
    const double* tensor,
    const double* v,
    int           n)
{
    double result = 0.0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            result += v[i] * tensor[i * n + j] * v[j];
        }
    }
    return result;
}

static inline double gr_metric_infinitesimal_distance(
    const double* tensor,
    const double* dv,
    int           n)
{
    double ds2 = gr_metric_quadratic_form(tensor, dv, n);
    return (ds2 > 0.0) ? sqrt(ds2) : 0.0;
}

/* ============================================================================
 * Metric Interpolation
 * ============================================================================ */

static inline double gr_euclidean_distance(const double* a, const double* b, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        double d = a[i] - b[i];
        sum += d * d;
    }
    return sqrt(sum);
}

static inline void gr_metric_interpolate(
    const gr_transport_metric_t* metric,
    const double*                coords,
    double*                      out_tensor)
{
    int n = metric->num_dims;
    size_t tensor_size = (size_t)n * (size_t)n;
    
    /* If no samples, use default */
    if (metric->num_samples == 0) {
        if (metric->default_tensor) {
            for (size_t i = 0; i < tensor_size; i++) {
                out_tensor[i] = metric->default_tensor[i];
            }
        } else {
            gr_metric_set_identity(out_tensor, n);
        }
        return;
    }
    
    /* Inverse distance weighted interpolation */
    double total_weight = 0.0;
    for (size_t i = 0; i < tensor_size; i++) {
        out_tensor[i] = 0.0;
    }
    
    for (int s = 0; s < metric->num_samples; s++) {
        const gr_metric_sample_t* sample = &metric->samples[s];
        double dist = gr_euclidean_distance(coords, sample->coordinates, n);
        
        /* Skip if outside radius */
        if (metric->interpolation_radius > 0.0 && dist > metric->interpolation_radius) {
            continue;
        }
        
        double weight = (dist < 1e-10) ? 1e10 : 1.0 / dist;
        total_weight += weight;
        
        for (size_t i = 0; i < tensor_size; i++) {
            out_tensor[i] += weight * sample->tensor[i];
        }
    }
    
    if (total_weight > 0.0) {
        for (size_t i = 0; i < tensor_size; i++) {
            out_tensor[i] /= total_weight;
        }
    } else {
        /* Fallback to default */
        if (metric->default_tensor) {
            for (size_t i = 0; i < tensor_size; i++) {
                out_tensor[i] = metric->default_tensor[i];
            }
        } else {
            gr_metric_set_identity(out_tensor, n);
        }
    }
}

/* ============================================================================
 * Geodesic Distance
 * ============================================================================ */

static inline double gr_geodesic_distance_approx(
    const gr_transport_metric_t* metric,
    const double*                from,
    const double*                to)
{
    int n = metric->num_dims;
    double total = 0.0;
    
    double step[16];
    double pos[16];
    double tensor[256];
    
    /* Compute direction */
    for (int i = 0; i < n; i++) {
        step[i] = (to[i] - from[i]) / (double)GR_GEODESIC_STEPS;
        pos[i] = from[i];
    }
    
    /* Integrate along path */
    for (int s = 0; s < GR_GEODESIC_STEPS; s++) {
        /* Midpoint of this segment */
        double mid[16];
        for (int i = 0; i < n; i++) {
            mid[i] = pos[i] + 0.5 * step[i];
        }
        
        /* Get metric at midpoint */
        gr_metric_interpolate(metric, mid, tensor);
        
        /* Compute ds */
        double ds = gr_metric_infinitesimal_distance(tensor, step, n);
        total += ds;
        
        /* Advance position */
        for (int i = 0; i < n; i++) {
            pos[i] += step[i];
        }
    }
    
    return total;
}

/* ============================================================================
 * Common Metric Factories
 * ============================================================================ */

static inline void gr_metric_from_liquidity(
    double*       tensor,
    int           n,
    const double* liquidity)
{
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (i == j) {
                double liq = liquidity[i];
                tensor[i * n + j] = (liq > 1e-10) ? 1.0 / liq : 1e10;
            } else {
                tensor[i * n + j] = 0.0;
            }
        }
    }
}

static inline void gr_metric_from_impact(
    double*       tensor,
    int           n,
    const double* positions,
    const double* impact_coeffs)
{
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (i == j) {
                tensor[i * n + j] = 1.0 + impact_coeffs[i] * fabs(positions[i]);
            } else {
                tensor[i * n + j] = 0.0;
            }
        }
    }
}

#endif /* GR_INTERNAL_TRANSPORT_H */
