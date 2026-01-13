# georisk

**Geometric risk engine that maps pricing manifold topology to identify fragility before it appears in returns.**

> "Statistical risk measures what appears. Geometric risk describes what is possible."  
> — Quantis

## Overview

Traditional risk measures (VaR, Beta, stress scenarios) are derived from observed data and assume local linearity. They fail precisely at points of discontinuity—regime changes, liquidity crises, margin calls—when you need them most.

**georisk** takes a different approach. Instead of fitting curves to historical data, it analyzes the *geometry* of the pricing manifold itself:

- **State Space Mapping**: Define the dimensions of risk (spot, vol, rates, liquidity, funding)
- **Jacobian Analysis**: First-order sensitivities (generalized Greeks)
- **Hessian Analysis**: Second-order curvature (where linear hedges fail)
- **Fragility Detection**: Identify regions where small perturbations generate large effects
- **Constraint Surfaces**: Model the boundaries of admissible states (position limits, margin, liquidity)
- **Transport Metrics**: Measure the true cost of moving between states (not all paths are equal)

## Building
```bash
# Build shared library
make

# Build with debug symbols
make DEBUG=1

# Run tests
make test

# Install (default: /usr/local)
make install

# Install to custom location
make install PREFIX=/opt/georisk
```

## Quick Start
```c
#include <georisk.h>

int main(void)
{
    // Create context
    gr_context_t* ctx = gr_context_new();
    
    // Load pricing engines (optional)
    gr_context_set_mco_library(ctx, "./libmcoptions.so");
    gr_context_set_fdp_library(ctx, "./libfdpricing.so");
    
    // Define state space
    gr_state_space_t* space = gr_state_space_new(ctx);
    
    gr_dimension_t dim_spot = {
        .type = GR_DIM_SPOT,
        .name = "spot",
        .min_value = 80.0,
        .max_value = 120.0,
        .num_points = 41
    };
    gr_state_space_add_dimension(space, &dim_spot);
    
    gr_dimension_t dim_vol = {
        .type = GR_DIM_VOLATILITY,
        .name = "vol",
        .min_value = 0.1,
        .max_value = 0.5,
        .num_points = 21
    };
    gr_state_space_add_dimension(space, &dim_vol);
    
    // Map prices across state space
    // (using your pricing function or the bridge to mcoptions/fdpricing)
    gr_state_space_map_prices(space, my_pricing_function, NULL);
    
    // Analyze at a point
    double point[] = {100.0, 0.25};
    
    gr_jacobian_t* jac = gr_jacobian_new(ctx, 2);
    gr_jacobian_compute(jac, space, point);
    printf("Gradient norm: %.4f\n", gr_jacobian_norm(jac));
    
    gr_hessian_t* hess = gr_hessian_new(ctx, 2);
    gr_hessian_compute(hess, space, point);
    printf("Curvature: %.4f\n", gr_hessian_frobenius_norm(hess));
    
    // Detect fragile regions
    gr_fragility_map_t* fragility = gr_fragility_map_new(ctx, space);
    gr_fragility_map_compute(fragility);
    printf("Fragile regions: %zu\n", gr_fragility_map_get_num_fragile_regions(fragility));
    
    // Cleanup
    gr_fragility_map_free(fragility);
    gr_hessian_free(hess);
    gr_jacobian_free(jac);
    gr_state_space_free(space);
    gr_context_free(ctx);
    
    return 0;
}
```

## Integration with Pricing Libraries

georisk is designed to work with your existing pricing infrastructure:
```c
// Load Monte Carlo library
gr_context_set_mco_library(ctx, "~/libraries/libmcoptions.so");

// Load Finite Difference library
gr_context_set_fdp_library(ctx, "~/libraries/libfdpricing.so");

// Use unified pricing interface
double price = gr_bridge_price_vanilla(
    ctx,
    GR_ENGINE_AUTO,      // Use best available
    GR_STYLE_EUROPEAN,
    GR_TYPE_CALL,
    100.0,   // spot
    100.0,   // strike
    0.05,    // rate
    0.2,     // vol
    1.0      // maturity
);
```

## Key Concepts

### Geometric Risk vs Statistical Risk

| Statistical Risk | Geometric Risk |
|-----------------|----------------|
| Derived from data | Arises before data |
| Assumes linearity | Maps curvature |
| Measures dispersion | Maps possibilities |
| Backwards-looking | Structure-based |
| Fails at discontinuities | Identifies them |

### The Four Axioms

Every geometry is defined by:
1. **Which states are admissible** → Constraint surfaces
2. **Which movements are possible** → State space topology
3. **Which transitions carry cost** → Transport metric
4. **Which constraints make paths impossible** → Hard constraints

### Fragility Score

The fragility score combines:
- **Gradient magnitude** (sensitivity)
- **Hessian curvature** (nonlinearity)
- **Condition number** (numerical stability)
- **Constraint proximity** (forced state changes)

## Project Structure
```
georisk/
├── include/
│   ├── georisk.h              # Public API (single header)
│   └── internal/              # Private headers
├── src/
│   ├── core/                  # Context, allocators, version
│   ├── analysis/              # State space, Jacobian, Hessian, fragility
│   ├── transport/             # Constraints, metric
│   └── bridge/                # Pricing library bridges
├── tests/
│   ├── unity/                 # Unity test framework
│   └── test_runner.c          # Test suite
├── Makefile
└── README.md
```

