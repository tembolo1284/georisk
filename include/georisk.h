/**
 * georisk.h - Geometric Risk Analysis Library
 * 
 * Maps the topology of pricing manifolds to identify structural fragility,
 * constraint surfaces, and regions where small perturbations generate
 * large effects—before they appear in statistical risk measures.
 * 
 * "Statistical risk measures what appears. Geometric risk describes what is possible."
 */

#ifndef GEORISK_H_INCLUDED
#define GEORISK_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * Export Markers (Ronacher's pattern)
 * ============================================================================ */

#ifndef GR_API
#  ifdef _WIN32
#     if defined(GR_BUILD_SHARED)
#         define GR_API __declspec(dllexport)
#     elif !defined(GR_BUILD_STATIC)
#         define GR_API __declspec(dllimport)
#     else
#         define GR_API
#     endif
#  else
#     if __GNUC__ >= 4
#         define GR_API __attribute__((visibility("default")))
#     else
#         define GR_API
#     endif
#  endif
#endif

/* ============================================================================
 * Version
 * ============================================================================ */

#define GR_VERSION_MAJOR 0
#define GR_VERSION_MINOR 1
#define GR_VERSION_PATCH 0
#define GR_VERSION ((GR_VERSION_MAJOR << 16) | (GR_VERSION_MINOR << 8) | GR_VERSION_PATCH)

GR_API unsigned int gr_get_version(void);
GR_API int gr_is_compatible_dll(void);
GR_API const char* gr_version_string(void);

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum gr_error {
    GR_SUCCESS = 0,
    GR_ERROR_NULL_POINTER,
    GR_ERROR_INVALID_ARGUMENT,
    GR_ERROR_OUT_OF_MEMORY,
    GR_ERROR_DIMENSION_MISMATCH,
    GR_ERROR_SINGULAR_MATRIX,
    GR_ERROR_NUMERICAL_INSTABILITY,
    GR_ERROR_PRICING_ENGINE_FAILED,
    GR_ERROR_CONSTRAINT_VIOLATION,
    GR_ERROR_NOT_INITIALIZED
} gr_error_t;

GR_API const char* gr_error_string(gr_error_t err);

/* ============================================================================
 * Opaque Types (no structs in public header per Ronacher)
 * ============================================================================ */

struct gr_context_s;
typedef struct gr_context_s gr_context_t;

struct gr_state_space_s;
typedef struct gr_state_space_s gr_state_space_t;

struct gr_jacobian_s;
typedef struct gr_jacobian_s gr_jacobian_t;

struct gr_hessian_s;
typedef struct gr_hessian_s gr_hessian_t;

struct gr_fragility_map_s;
typedef struct gr_fragility_map_s gr_fragility_map_t;

struct gr_constraint_surface_s;
typedef struct gr_constraint_surface_s gr_constraint_surface_t;

struct gr_transport_metric_s;
typedef struct gr_transport_metric_s gr_transport_metric_t;

/* ============================================================================
 * Custom Allocators
 * ============================================================================ */

GR_API void gr_set_allocators(
    void* (*f_malloc)(size_t),
    void* (*f_realloc)(void*, size_t),
    void  (*f_free)(void*)
);

GR_API void* gr_malloc(size_t size);
GR_API void* gr_realloc(void* ptr, size_t size);
GR_API void* gr_calloc(size_t count, size_t size);
GR_API void  gr_free(void* ptr);
GR_API char* gr_strdup(const char* str);

/* ============================================================================
 * Context Management
 * ============================================================================ */

GR_API gr_context_t* gr_context_new(void);
GR_API void gr_context_free(gr_context_t* ctx);

/* Link external pricing engines */
GR_API gr_error_t gr_context_set_mco_library(gr_context_t* ctx, const char* path);
GR_API gr_error_t gr_context_set_fdp_library(gr_context_t* ctx, const char* path);

/* Configuration */
GR_API void gr_context_set_bump_size(gr_context_t* ctx, double bump);
GR_API void gr_context_set_num_threads(gr_context_t* ctx, int threads);

/* Error handling */
GR_API gr_error_t gr_context_get_last_error(const gr_context_t* ctx);
GR_API const char* gr_context_get_error_message(const gr_context_t* ctx);

/* ============================================================================
 * State Space - The Geometry of Possibilities
 * 
 * "Geometric risk arises before data. It does not start from prices,
 *  but from the space in which the system moves."
 * ============================================================================ */

typedef enum gr_dimension_type {
    GR_DIM_SPOT,           /* Underlying price */
    GR_DIM_VOLATILITY,     /* Implied or realized vol */
    GR_DIM_RATE,           /* Interest rate */
    GR_DIM_TIME,           /* Time to maturity */
    GR_DIM_CORRELATION,    /* Correlation parameter */
    GR_DIM_LIQUIDITY,      /* Liquidity score / bid-ask */
    GR_DIM_FUNDING,        /* Funding cost */
    GR_DIM_CUSTOM          /* User-defined dimension */
} gr_dimension_type_t;

typedef struct gr_dimension {
    gr_dimension_type_t type;
    const char*         name;
    double              min_value;
    double              max_value;
    double              current;
    double              step_size;      /* Grid resolution */
    int                 num_points;     /* Discretization */
} gr_dimension_t;

GR_API gr_state_space_t* gr_state_space_new(gr_context_t* ctx);
GR_API void gr_state_space_free(gr_state_space_t* space);

GR_API gr_error_t gr_state_space_add_dimension(
    gr_state_space_t* space,
    const gr_dimension_t* dim
);

GR_API int gr_state_space_get_num_dimensions(const gr_state_space_t* space);
GR_API size_t gr_state_space_get_total_points(const gr_state_space_t* space);

/* Map a pricing function across the state space */
typedef double (*gr_pricing_fn)(
    const double* coordinates,  /* Array of dimension values */
    int           num_dims,
    void*         user_data
);

GR_API gr_error_t gr_state_space_map_prices(
    gr_state_space_t* space,
    gr_pricing_fn     fn,
    void*             user_data
);

/* ============================================================================
 * Jacobian - First-Order Sensitivity Structure
 * 
 * The Jacobian reveals how prices respond to infinitesimal movements
 * along each axis of the state space. Where gradients are steep,
 * small errors in inputs produce large errors in outputs.
 * ============================================================================ */

GR_API gr_jacobian_t* gr_jacobian_new(gr_context_t* ctx, int num_dims);
GR_API void gr_jacobian_free(gr_jacobian_t* jac);

GR_API gr_error_t gr_jacobian_compute(
    gr_jacobian_t*          jac,
    const gr_state_space_t* space,
    const double*           point   /* Where in state space */
);

GR_API double gr_jacobian_get(const gr_jacobian_t* jac, int dim);
GR_API double gr_jacobian_norm(const gr_jacobian_t* jac);  /* Gradient magnitude */

/* ============================================================================
 * Hessian - Second-Order Curvature Structure
 * 
 * The Hessian captures the curvature of the pricing manifold.
 * High curvature = nonlinearity = where linear risk models fail.
 * Eigenvalues reveal principal directions of sensitivity.
 * ============================================================================ */

GR_API gr_hessian_t* gr_hessian_new(gr_context_t* ctx, int num_dims);
GR_API void gr_hessian_free(gr_hessian_t* hess);

GR_API gr_error_t gr_hessian_compute(
    gr_hessian_t*           hess,
    const gr_state_space_t* space,
    const double*           point
);

GR_API double gr_hessian_get(const gr_hessian_t* hess, int row, int col);

/* Curvature analysis */
GR_API gr_error_t gr_hessian_eigenvalues(
    const gr_hessian_t* hess,
    double*             eigenvalues,    /* Output: sorted descending */
    int                 num_eigenvalues
);

GR_API double gr_hessian_trace(const gr_hessian_t* hess);       /* Sum of eigenvalues */
GR_API double gr_hessian_frobenius_norm(const gr_hessian_t* hess);
GR_API double gr_hessian_condition_number(const gr_hessian_t* hess);

/* ============================================================================
 * Fragility Map - Where Small Perturbations Generate Large Effects
 * 
 * "Identifying regions where small perturbations generate large effects"
 * ============================================================================ */

typedef struct gr_fragility_point {
    double* coordinates;        /* Location in state space */
    double  fragility_score;    /* 0 = stable, 1 = maximally fragile */
    double  curvature;          /* Local curvature measure */
    double  gradient_norm;      /* Sensitivity magnitude */
    int     near_constraint;    /* Boolean: close to constraint surface */
} gr_fragility_point_t;

GR_API gr_fragility_map_t* gr_fragility_map_new(
    gr_context_t*     ctx,
    gr_state_space_t* space
);
GR_API void gr_fragility_map_free(gr_fragility_map_t* map);

GR_API gr_error_t gr_fragility_map_compute(gr_fragility_map_t* map);

GR_API size_t gr_fragility_map_get_num_fragile_regions(const gr_fragility_map_t* map);
GR_API gr_error_t gr_fragility_map_get_region(
    const gr_fragility_map_t* map,
    size_t                    index,
    gr_fragility_point_t*     out
);

/* Get fragility at a specific point */
GR_API double gr_fragility_at_point(
    const gr_fragility_map_t* map,
    const double*             coordinates
);

/* ============================================================================
 * Constraint Surfaces - The Boundaries of Admissible States
 * 
 * "In mathematics, every geometry is defined by axioms:
 *  1) which states are admissible
 *  2) which movements are possible
 *  3) which transitions carry a cost
 *  4) which constraints make certain paths unlikely or impossible"
 * ============================================================================ */

typedef enum gr_constraint_type {
    GR_CONSTRAINT_LIQUIDITY,      /* Bid-ask spread threshold */
    GR_CONSTRAINT_POSITION_LIMIT, /* Max position size */
    GR_CONSTRAINT_MARGIN,         /* Margin requirement */
    GR_CONSTRAINT_REGULATORY,     /* Regulatory limit */
    GR_CONSTRAINT_CUSTOM          /* User-defined */
} gr_constraint_type_t;

GR_API gr_constraint_surface_t* gr_constraint_surface_new(gr_context_t* ctx);
GR_API void gr_constraint_surface_free(gr_constraint_surface_t* surface);

GR_API gr_error_t gr_constraint_add(
    gr_constraint_surface_t* surface,
    gr_constraint_type_t     type,
    const char*              name,
    double                   threshold
);

/* Check if a point violates any constraint */
GR_API int gr_constraint_check(
    const gr_constraint_surface_t* surface,
    const double*                  coordinates,
    int                            num_dims
);

/* Distance to nearest constraint surface */
GR_API double gr_constraint_distance(
    const gr_constraint_surface_t* surface,
    const double*                  coordinates,
    int                            num_dims
);

/* ============================================================================
 * Transport Metric - The Cost of Moving Between States
 * 
 * "Liquidity, funding, concentration, endogeneity, transport rigidity
 *  are not independent factors but can be viewed as coordinates on a surface."
 * 
 * Some paths through state space are "shorter" (cheaper/more accessible)
 * than others, even if they cover the same Euclidean distance.
 * ============================================================================ */

GR_API gr_transport_metric_t* gr_transport_metric_new(gr_context_t* ctx);
GR_API void gr_transport_metric_free(gr_transport_metric_t* metric);

/* Set metric tensor at a point (defines local "cost" of movement) */
GR_API gr_error_t gr_transport_metric_set(
    gr_transport_metric_t* metric,
    const double*          coordinates,
    int                    num_dims,
    const double*          tensor      /* num_dims x num_dims symmetric matrix */
);

/* Compute geodesic distance (shortest path cost) between two points */
GR_API double gr_transport_distance(
    const gr_transport_metric_t* metric,
    const double*                from,
    const double*                to,
    int                          num_dims
);

/* ============================================================================
 * High-Level Analysis - Integrated Risk Assessment
 * ============================================================================ */

typedef struct gr_risk_report {
    double overall_fragility;       /* Aggregate fragility score */
    double max_curvature;           /* Worst-case curvature */
    double constraint_proximity;    /* Nearest constraint distance */
    double effective_dimension;     /* How many dims really matter */
    size_t num_fragile_regions;     /* Count of fragile spots */
    const char* warning_message;    /* Human-readable warning if any */
} gr_risk_report_t;

GR_API gr_error_t gr_analyze_portfolio(
    gr_context_t*                  ctx,
    const gr_state_space_t*        space,
    const gr_constraint_surface_t* constraints,
    gr_risk_report_t*              report
);

#ifdef __cplusplus
}
#endif

#endif /* GEORISK_H_INCLUDED */
