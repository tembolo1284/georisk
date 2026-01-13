/**
 * internal/transport.h - Transport metric internals
 * 
 * "Liquidity, funding, concentration, endogeneity, transport rigidity
 *  are not independent factors but can be viewed as coordinates on a surface."
 * 
 * The transport metric defines the "cost" of moving between states in
 * the state space. In Euclidean space, all directions are equally easy.
 * In financial markets, they are not:
 * 
 *   - Selling into illiquidity costs more than buying
 *   - Unwinding concentrated positions costs more than small trades
 *   - Moving against funding constraints costs more than with them
 * 
 * Mathematically, this is a Riemannian metric tensor g_ij that defines
 * the local inner product on the tangent space. The "distance" between
 * two points is then the geodesic distance—the shortest path cost.
 * 
 * ds² = Σ_ij g_ij dx_i dx_j
 * 
 * When g_ij = δ_ij (identity), we recover Euclidean distance.
 * When g_ij varies across the manifold, some paths become "longer" 
 * (more expensive) even if they cover less Euclidean distance.
 */

#ifndef GR_INTERNAL_TRANSPORT_H
#define GR_INTERNAL_TRANSPORT_H

#include "georisk.h"
#include "core.h"
#include <math.h>

/* ============================================================================
 * Transport Metric Structure
 * ============================================================================ */

#define GR_TRANSPORT_MAX_SAMPLES 1024

/**
 * A sampled metric tensor at a specific point in state space.
 */
typedef struct gr_metric_sample {
    double* coordinates;    /* [num_dims] location */
    double* tensor;         /* [num_dims x num_dims] symmetric positive definite */
    int     num_dims;
} gr_metric_sample_t;

struct gr_transport_metric_s {
    gr_context_t*       ctx;
    int                 num_dims;
    
    /* Sampled metric tensors across state space */
    gr_metric_sample_t  samples[GR_TRANSPORT_MAX_SAMPLES];
    int                 num_samples;
    
    /* Default metric (used where no sample exists) */
    double*             default_tensor;     /* [num_dims x num_dims] */
    int                 use_identity;       /* If true, default is identity */
    
    /* Interpolation settings */
    double              interpolation_radius;   /* For local averaging */
};

/* ============================================================================
 * Metric Tensor Operations
 * ============================================================================ */

/**
 * Get element g[i][j] from a metric tensor.
 */
static inline double gr_metric_get(
    const double* tensor,
    int           num_dims,
    int           i,
    int           j)
{
    return tensor[i * num_dims + j];
}

/**
 * Set element g[i][j] in a metric tensor (also sets g[j][i] for symmetry).
 */
static inline void gr_metric_set(
    double* tensor,
    int     num_dims,
    int     i,
    int     j,
    double  value)
{
    tensor[i * num_dims + j] = value;
    tensor[j * num_dims + i] = value;
}

/**
 * Initialize metric tensor to identity (Euclidean metric).
 */
static inline void gr_metric_set_identity(double* tensor, int num_dims)
{
    for (int i = 0; i < num_dims; i++) {
        for (int j = 0; j < num_dims; j++) {
            tensor[i * num_dims + j] = (i == j) ? 1.0 : 0.0;
        }
    }
}

/**
 * Initialize metric tensor to diagonal with given values.
 * Useful for axis-aligned anisotropy (different costs per dimension).
 */
static inline void gr_metric_set_diagonal(
    double*       tensor,
    int           num_dims,
    const double* diagonal)
{
    for (int i = 0; i < num_dims; i++) {
        for (int j = 0; j < num_dims; j++) {
            tensor[i * num_dims + j] = (i == j) ? diagonal[i] : 0.0;
        }
    }
}

/**
 * Scale entire metric tensor by a factor.
 * Useful for global cost multipliers.
 */
static inline void gr_metric_scale(double* tensor, int num_dims, double factor)
{
    int n2 = num_dims * num_dims;
    for (int i = 0; i < n2; i++) {
        tensor[i] *= factor;
    }
}

/**
 * Compute the quadratic form v^T G v for a vector v.
 * This gives the squared "length" of v under the metric G.
 */
static inline double gr_metric_quadratic_form(
    const double* tensor,
    const double* v,
    int           num_dims)
{
    double result = 0.0;
    for (int i = 0; i < num_dims; i++) {
        for (int j = 0; j < num_dims; j++) {
            result += v[i] * gr_metric_get(tensor, num_dims, i, j) * v[j];
        }
    }
    return result;
}

/**
 * Compute the local "length" of an infinitesimal displacement dv.
 * ds = sqrt(dv^T G dv)
 */
static inline double gr_metric_infinitesimal_distance(
    const double* tensor,
    const double* dv,
    int           num_dims)
{
    double q = gr_metric_quadratic_form(tensor, dv, num_dims);
    return (q > 0.0) ? sqrt(q) : 0.0;
}

/* ============================================================================
 * Metric Interpolation
 * ============================================================================ */

/**
 * Compute Euclidean distance between two points (for interpolation weights).
 */
static inline double gr_euclidean_distance(
    const double* a,
    const double* b,
    int           num_dims)
{
    double sum_sq = 0.0;
    for (int i = 0; i < num_dims; i++) {
        double diff = a[i] - b[i];
        sum_sq += diff * diff;
    }
    return sqrt(sum_sq);
}

/**
 * Interpolate metric tensor at a point using inverse distance weighting.
 * 
 * G(x) = Σ_k w_k G_k / Σ_k w_k
 * 
 * where w_k = 1 / (d(x, x_k) + ε)^p
 */
static inline void gr_metric_interpolate(
    const gr_transport_metric_t* metric,
    const double*                coordinates,
    double*                      out_tensor)  /* [num_dims x num_dims] */
{
    int n = metric->num_dims;
    int n2 = n * n;
    
    /* Initialize to zero */
    for (int i = 0; i < n2; i++) {
        out_tensor[i] = 0.0;
    }
    
    /* If no samples, return default */
    if (metric->num_samples == 0) {
        if (metric->use_identity) {
            gr_metric_set_identity(out_tensor, n);
        } else if (metric->default_tensor) {
            for (int i = 0; i < n2; i++) {
                out_tensor[i] = metric->default_tensor[i];
            }
        } else {
            gr_metric_set_identity(out_tensor, n);
        }
        return;
    }
    
    /* Inverse distance weighting */
    double total_weight = 0.0;
    double epsilon = 1e-10;
    double p = 2.0;  /* Power parameter */
    
    for (int k = 0; k < metric->num_samples; k++) {
        const gr_metric_sample_t* sample = &metric->samples[k];
        
        double dist = gr_euclidean_distance(
            coordinates,
            sample->coordinates,
            n
        );
        
        /* Skip samples outside interpolation radius */
        if (metric->interpolation_radius > 0.0 && 
            dist > metric->interpolation_radius) {
            continue;
        }
        
        double w = 1.0 / pow(dist + epsilon, p);
        total_weight += w;
        
        for (int i = 0; i < n2; i++) {
            out_tensor[i] += w * sample->tensor[i];
        }
    }
    
    /* Normalize */
    if (total_weight > epsilon) {
        for (int i = 0; i < n2; i++) {
            out_tensor[i] /= total_weight;
        }
    } else {
        /* No nearby samples, use default */
        if (metric->use_identity) {
            gr_metric_set_identity(out_tensor, n);
        } else if (metric->default_tensor) {
            for (int i = 0; i < n2; i++) {
                out_tensor[i] = metric->default_tensor[i];
            }
        } else {
            gr_metric_set_identity(out_tensor, n);
        }
    }
}

/* ============================================================================
 * Geodesic Distance Computation
 * 
 * The geodesic distance is the length of the shortest path between two
 * points under the metric. For a general Riemannian metric, this requires
 * solving a boundary value problem (geodesic equation).
 * 
 * We use a simple approximation: discretize the straight line path and
 * integrate the metric along it. This is exact for constant metrics and
 * a good approximation for slowly varying metrics.
 * ============================================================================ */

#define GR_GEODESIC_STEPS 100

/**
 * Approximate geodesic distance by integrating metric along straight line.
 * 
 * d(a, b) ≈ ∫₀¹ sqrt((b-a)^T G(a + t(b-a)) (b-a)) dt
 * 
 * Discretized with midpoint rule.
 */
static inline double gr_geodesic_distance_approx(
    const gr_transport_metric_t* metric,
    const double*                from,
    const double*                to)
{
    int n = metric->num_dims;
    
    /* Direction vector */
    double* direction = (double*)alloca((size_t)n * sizeof(double));
    for (int i = 0; i < n; i++) {
        direction[i] = to[i] - from[i];
    }
    
    /* Integration */
    double total_length = 0.0;
    double dt = 1.0 / GR_GEODESIC_STEPS;
    
    double* point = (double*)alloca((size_t)n * sizeof(double));
    double* tensor = (double*)alloca((size_t)(n * n) * sizeof(double));
    
    for (int step = 0; step < GR_GEODESIC_STEPS; step++) {
        /* Midpoint of this segment */
        double t = (step + 0.5) * dt;
        for (int i = 0; i < n; i++) {
            point[i] = from[i] + t * direction[i];
        }
        
        /* Get metric at this point */
        gr_metric_interpolate(metric, point, tensor);
        
        /* Infinitesimal length for this segment */
        /* ds = sqrt(dv^T G dv) where dv = direction * dt */
        double q = gr_metric_quadratic_form(tensor, direction, n);
        double ds = (q > 0.0) ? sqrt(q) * dt : 0.0;
        
        total_length += ds;
    }
    
    return total_length;
}

/* ============================================================================
 * Sample Management
 * ============================================================================ */

/**
 * Initialize a metric sample.
 */
static inline gr_error_t gr_metric_sample_init(
    gr_metric_sample_t* sample,
    gr_context_t*       ctx,
    int                 num_dims,
    const double*       coordinates,
    const double*       tensor)
{
    sample->num_dims = num_dims;
    
    sample->coordinates = (double*)gr_ctx_calloc(ctx, (size_t)num_dims, sizeof(double));
    if (!sample->coordinates) {
        return GR_ERROR_OUT_OF_MEMORY;
    }
    
    sample->tensor = (double*)gr_ctx_calloc(ctx, (size_t)(num_dims * num_dims), sizeof(double));
    if (!sample->tensor) {
        gr_ctx_free(ctx, sample->coordinates);
        sample->coordinates = NULL;
        return GR_ERROR_OUT_OF_MEMORY;
    }
    
    for (int i = 0; i < num_dims; i++) {
        sample->coordinates[i] = coordinates[i];
    }
    
    for (int i = 0; i < num_dims * num_dims; i++) {
        sample->tensor[i] = tensor[i];
    }
    
    return GR_SUCCESS;
}

/**
 * Free a metric sample.
 */
static inline void gr_metric_sample_free(
    gr_metric_sample_t* sample,
    gr_context_t*       ctx)
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
 * Common Metric Factories
 * ============================================================================ */

/**
 * Create a liquidity-adjusted metric.
 * 
 * In dimensions with low liquidity, movement is "expensive" (high metric).
 * In dimensions with high liquidity, movement is "cheap" (low metric).
 * 
 * g_ii = 1 / liquidity_i
 */
static inline void gr_metric_from_liquidity(
    double*       tensor,
    int           num_dims,
    const double* liquidity)  /* [num_dims] liquidity scores, higher = more liquid */
{
    for (int i = 0; i < num_dims; i++) {
        for (int j = 0; j < num_dims; j++) {
            if (i == j) {
                double liq = liquidity[i];
                /* Avoid division by zero, cap at minimum liquidity */
                if (liq < 0.01) liq = 0.01;
                tensor[i * num_dims + j] = 1.0 / liq;
            } else {
                tensor[i * num_dims + j] = 0.0;
            }
        }
    }
}

/**
 * Create an impact-adjusted metric.
 * 
 * Movement cost increases with position size (market impact).
 * g_ii = 1 + impact_coefficient * |position_i|
 */
static inline void gr_metric_from_impact(
    double*       tensor,
    int           num_dims,
    const double* positions,         /* [num_dims] current positions */
    const double* impact_coeffs)     /* [num_dims] impact per unit */
{
    for (int i = 0; i < num_dims; i++) {
        for (int j = 0; j < num_dims; j++) {
            if (i == j) {
                double impact = 1.0 + impact_coeffs[i] * fabs(positions[i]);
                tensor[i * num_dims + j] = impact;
            } else {
                tensor[i * num_dims + j] = 0.0;
            }
        }
    }
}

/**
 * Create an asymmetric metric (buying vs selling costs different).
 * 
 * This is a simplification—true asymmetry requires Finsler geometry.
 * We approximate by using the average of buy and sell costs.
 */
static inline void gr_metric_from_asymmetric_costs(
    double*       tensor,
    int           num_dims,
    const double* buy_costs,    /* [num_dims] cost to increase position */
    const double* sell_costs)   /* [num_dims] cost to decrease position */
{
    for (int i = 0; i < num_dims; i++) {
        for (int j = 0; j < num_dims; j++) {
            if (i == j) {
                /* Average of buy and sell costs */
                double avg = 0.5 * (buy_costs[i] + sell_costs[i]);
                tensor[i * num_dims + j] = avg;
            } else {
                tensor[i * num_dims + j] = 0.0;
            }
        }
    }
}

#endif /* GR_INTERNAL_TRANSPORT_H */
