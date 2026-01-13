/**
 * internal/core.h - Internal core definitions
 * 
 * Private header for internal use only. Not part of public API.
 */

#ifndef GR_INTERNAL_CORE_H
#define GR_INTERNAL_CORE_H

#include "georisk.h"
#include <dlfcn.h>

/* ============================================================================
 * Internal Macros
 * ============================================================================ */

#define GR_UNUSED(x) ((void)(x))

#define GR_MIN(a, b) ((a) < (b) ? (a) : (b))
#define GR_MAX(a, b) ((a) > (b) ? (a) : (b))
#define GR_CLAMP(x, lo, hi) GR_MIN(GR_MAX(x, lo), hi)

#define GR_ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Default bump size for numerical differentiation (1 basis point) */
#define GR_DEFAULT_BUMP 0.0001

/* Maximum dimensions for state space */
#define GR_MAX_DIMENSIONS 16

/* Maximum error message length */
#define GR_MAX_ERROR_MSG 256

/* ============================================================================
 * Bridge Function Pointers - Monte Carlo Library (mcoptions)
 * ============================================================================ */

typedef struct gr_mco_vtable {
    void* handle;  /* dlopen handle */
    
    /* Context management */
    void* (*context_new)(void);
    void  (*context_free)(void* ctx);
    void  (*context_set_seed)(void* ctx, uint64_t seed);
    void  (*context_set_num_simulations)(void* ctx, uint32_t n);
    void  (*context_set_num_steps)(void* ctx, uint32_t n);
    void  (*context_set_antithetic)(void* ctx, int enabled);
    void  (*context_set_num_threads)(void* ctx, int n);
    
    /* Pricing functions */
    double (*european_call)(void* ctx, double S, double K, double r, double sigma, double T);
    double (*european_put)(void* ctx, double S, double K, double r, double sigma, double T);
    double (*asian_call)(void* ctx, double S, double K, double r, double sigma, double T);
    double (*asian_put)(void* ctx, double S, double K, double r, double sigma, double T);
} gr_mco_vtable_t;

/* ============================================================================
 * Bridge Function Pointers - Finite Difference Library (fdpricing)
 * ============================================================================ */

typedef struct gr_fdp_vtable {
    void* handle;  /* dlopen handle */
    
    /* Context management */
    void* (*context_new)(void);
    void  (*context_free)(void* ctx);
    
    /* Pricing functions */
    double (*price_european_call)(void* ctx, double S, double K, double r, double sigma, double T);
    double (*price_european_put)(void* ctx, double S, double K, double r, double sigma, double T);
    double (*price_american_call)(void* ctx, double S, double K, double r, double sigma, double T);
    double (*price_american_put)(void* ctx, double S, double K, double r, double sigma, double T);
} gr_fdp_vtable_t;

/* ============================================================================
 * Context Structure (internal definition)
 * ============================================================================ */

struct gr_context_s {
    /* Custom allocators (per-context) */
    struct {
        void* (*f_malloc)(size_t);
        void* (*f_realloc)(void*, size_t);
        void  (*f_free)(void*);
    } allocators;
    int use_custom_allocators;
    
    /* Pricing engine bridges */
    gr_mco_vtable_t mco;
    gr_fdp_vtable_t fdp;
    int mco_loaded;
    int fdp_loaded;
    
    /* Cached pricing contexts */
    void* mco_ctx;
    void* fdp_ctx;
    
    /* Configuration */
    double bump_size;
    int    num_threads;
    
    /* Error state */
    gr_error_t last_error;
    char       error_msg[GR_MAX_ERROR_MSG];
};

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/* Set error on context */
static inline void gr_set_error(gr_context_t* ctx, gr_error_t err, const char* msg) {
    if (ctx) {
        ctx->last_error = err;
        if (msg) {
            size_t len = 0;
            while (msg[len] && len < GR_MAX_ERROR_MSG - 1) {
                ctx->error_msg[len] = msg[len];
                len++;
            }
            ctx->error_msg[len] = '\0';
        } else {
            ctx->error_msg[0] = '\0';
        }
    }
}

/* Clear error on context */
static inline void gr_clear_error(gr_context_t* ctx) {
    if (ctx) {
        ctx->last_error = GR_SUCCESS;
        ctx->error_msg[0] = '\0';
    }
}

#endif /* GR_INTERNAL_CORE_H */
