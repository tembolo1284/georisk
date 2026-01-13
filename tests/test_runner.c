/**
 * test_runner.c - Main test runner for georisk
 */

#include "unity.h"
#include "georisk.h"
#include <stdio.h>
#include <math.h>

/* ============================================================================
 * Test Setup/Teardown
 * ============================================================================ */

static gr_context_t* g_ctx = NULL;

void setUp(void)
{
    g_ctx = gr_context_new();
}

void tearDown(void)
{
    if (g_ctx) {
        gr_context_free(g_ctx);
        g_ctx = NULL;
    }
}

/* ============================================================================
 * Version Tests
 * ============================================================================ */

void test_version_get(void)
{
    unsigned int version = gr_get_version();
    TEST_ASSERT_GREATER_THAN(0, version);
}

void test_version_compatible(void)
{
    int compatible = gr_is_compatible_dll();
    TEST_ASSERT_EQUAL_INT(1, compatible);
}

void test_version_string(void)
{
    const char* str = gr_version_string();
    TEST_ASSERT_NOT_NULL(str);
    TEST_ASSERT_TRUE(str[0] != '\0');
}

/* ============================================================================
 * Error String Tests
 * ============================================================================ */

void test_error_string_success(void)
{
    const char* str = gr_error_string(GR_SUCCESS);
    TEST_ASSERT_EQUAL_STRING("Success", str);
}

void test_error_string_null(void)
{
    const char* str = gr_error_string(GR_ERROR_NULL_POINTER);
    TEST_ASSERT_EQUAL_STRING("Null pointer", str);
}

void test_error_string_invalid(void)
{
    const char* str = gr_error_string((gr_error_t)9999);
    TEST_ASSERT_EQUAL_STRING("Unknown error", str);
}

/* ============================================================================
 * Allocator Tests
 * ============================================================================ */

void test_malloc_basic(void)
{
    void* ptr = gr_malloc(100);
    TEST_ASSERT_NOT_NULL(ptr);
    gr_free(ptr);
}

void test_calloc_zeroed(void)
{
    int* arr = (int*)gr_calloc(10, sizeof(int));
    TEST_ASSERT_NOT_NULL(arr);
    
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_INT(0, arr[i]);
    }
    
    gr_free(arr);
}

void test_strdup(void)
{
    const char* original = "Hello, georisk!";
    char* copy = gr_strdup(original);
    
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_STRING(original, copy);
    TEST_ASSERT_TRUE(copy != original);
    
    gr_free(copy);
}

void test_strdup_null(void)
{
    char* copy = gr_strdup(NULL);
    TEST_ASSERT_NULL(copy);
}

/* ============================================================================
 * Context Tests
 * ============================================================================ */

void test_context_new(void)
{
    TEST_ASSERT_NOT_NULL(g_ctx);
}

void test_context_error_initial(void)
{
    gr_error_t err = gr_context_get_last_error(g_ctx);
    TEST_ASSERT_EQUAL_INT(GR_SUCCESS, err);
}

void test_context_bump_size(void)
{
    gr_context_set_bump_size(g_ctx, 0.001);
    /* No direct getter, but should not error */
    TEST_ASSERT_EQUAL_INT(GR_SUCCESS, gr_context_get_last_error(g_ctx));
}

void test_context_bump_size_invalid(void)
{
    gr_context_set_bump_size(g_ctx, -1.0);
    TEST_ASSERT_EQUAL_INT(GR_ERROR_INVALID_ARGUMENT, gr_context_get_last_error(g_ctx));
}

void test_context_num_threads(void)
{
    gr_context_set_num_threads(g_ctx, 4);
    TEST_ASSERT_EQUAL_INT(GR_SUCCESS, gr_context_get_last_error(g_ctx));
}

void test_context_num_threads_invalid(void)
{
    gr_context_set_num_threads(g_ctx, 0);
    TEST_ASSERT_EQUAL_INT(GR_ERROR_INVALID_ARGUMENT, gr_context_get_last_error(g_ctx));
}

/* ============================================================================
 * State Space Tests
 * ============================================================================ */

void test_state_space_new(void)
{
    gr_state_space_t* space = gr_state_space_new(g_ctx);
    TEST_ASSERT_NOT_NULL(space);
    TEST_ASSERT_EQUAL_INT(0, gr_state_space_get_num_dimensions(space));
    gr_state_space_free(space);
}

void test_state_space_add_dimension(void)
{
    gr_state_space_t* space = gr_state_space_new(g_ctx);
    
    gr_dimension_t dim = {
        .type = GR_DIM_SPOT,
        .name = "spot",
        .min_value = 80.0,
        .max_value = 120.0,
        .current = 100.0,
        .num_points = 21
    };
    
    gr_error_t err = gr_state_space_add_dimension(space, &dim);
    TEST_ASSERT_EQUAL_INT(GR_SUCCESS, err);
    TEST_ASSERT_EQUAL_INT(1, gr_state_space_get_num_dimensions(space));
    TEST_ASSERT_EQUAL_INT(21, gr_state_space_get_total_points(space));
    
    gr_state_space_free(space);
}

void test_state_space_multiple_dimensions(void)
{
    gr_state_space_t* space = gr_state_space_new(g_ctx);
    
    gr_dimension_t dim_spot = {
        .type = GR_DIM_SPOT,
        .name = "spot",
        .min_value = 90.0,
        .max_value = 110.0,
        .num_points = 11
    };
    
    gr_dimension_t dim_vol = {
        .type = GR_DIM_VOLATILITY,
        .name = "volatility",
        .min_value = 0.1,
        .max_value = 0.4,
        .num_points = 7
    };
    
    gr_state_space_add_dimension(space, &dim_spot);
    gr_state_space_add_dimension(space, &dim_vol);
    
    TEST_ASSERT_EQUAL_INT(2, gr_state_space_get_num_dimensions(space));
    TEST_ASSERT_EQUAL_INT(11 * 7, gr_state_space_get_total_points(space));
    
    gr_state_space_free(space);
}

void test_state_space_invalid_dimension(void)
{
    gr_state_space_t* space = gr_state_space_new(g_ctx);
    
    gr_dimension_t dim = {
        .type = GR_DIM_SPOT,
        .name = "bad",
        .min_value = 100.0,
        .max_value = 80.0,  /* Invalid: min > max */
        .num_points = 10
    };
    
    gr_error_t err = gr_state_space_add_dimension(space, &dim);
    TEST_ASSERT_EQUAL_INT(GR_ERROR_INVALID_ARGUMENT, err);
    
    gr_state_space_free(space);
}

/* ============================================================================
 * Jacobian Tests
 * ============================================================================ */

void test_jacobian_new(void)
{
    gr_jacobian_t* jac = gr_jacobian_new(g_ctx, 3);
    TEST_ASSERT_NOT_NULL(jac);
    gr_jacobian_free(jac);
}

void test_jacobian_invalid_dims(void)
{
    gr_jacobian_t* jac = gr_jacobian_new(g_ctx, 0);
    TEST_ASSERT_NULL(jac);
    
    jac = gr_jacobian_new(g_ctx, 100);  /* Too many */
    TEST_ASSERT_NULL(jac);
}

void test_jacobian_norm_uncomputed(void)
{
    gr_jacobian_t* jac = gr_jacobian_new(g_ctx, 2);
    double norm = gr_jacobian_norm(jac);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 0.0, norm);
    gr_jacobian_free(jac);
}

/* ============================================================================
 * Hessian Tests
 * ============================================================================ */

void test_hessian_new(void)
{
    gr_hessian_t* hess = gr_hessian_new(g_ctx, 3);
    TEST_ASSERT_NOT_NULL(hess);
    gr_hessian_free(hess);
}

void test_hessian_invalid_dims(void)
{
    gr_hessian_t* hess = gr_hessian_new(g_ctx, 0);
    TEST_ASSERT_NULL(hess);
}

void test_hessian_trace_uncomputed(void)
{
    gr_hessian_t* hess = gr_hessian_new(g_ctx, 2);
    double trace = gr_hessian_trace(hess);
    TEST_ASSERT_DOUBLE_WITHIN(0.001, 0.0, trace);
    gr_hessian_free(hess);
}

/* ============================================================================
 * Constraint Tests
 * ============================================================================ */

void test_constraint_surface_new(void)
{
    gr_constraint_surface_t* surface = gr_constraint_surface_new(g_ctx);
    TEST_ASSERT_NOT_NULL(surface);
    gr_constraint_surface_free(surface);
}

void test_constraint_add(void)
{
    gr_constraint_surface_t* surface = gr_constraint_surface_new(g_ctx);
    
    gr_error_t err = gr_constraint_add(surface, GR_CONSTRAINT_LIQUIDITY, "spread", 0.05);
    TEST_ASSERT_EQUAL_INT(GR_SUCCESS, err);
    
    err = gr_constraint_add(surface, GR_CONSTRAINT_POSITION_LIMIT, "max_pos", 1000000.0);
    TEST_ASSERT_EQUAL_INT(GR_SUCCESS, err);
    
    gr_constraint_surface_free(surface);
}

void test_constraint_check_no_constraints(void)
{
    gr_constraint_surface_t* surface = gr_constraint_surface_new(g_ctx);
    
    double coords[] = {100.0, 0.2, 0.05};
    int violated = gr_constraint_check(surface, coords, 3);
    
    TEST_ASSERT_EQUAL_INT(0, violated);
    
    gr_constraint_surface_free(surface);
}

/* ============================================================================
 * Transport Metric Tests
 * ============================================================================ */

void test_transport_metric_new(void)
{
    gr_transport_metric_t* metric = gr_transport_metric_new(g_ctx);
    TEST_ASSERT_NOT_NULL(metric);
    gr_transport_metric_free(metric);
}

void test_transport_distance_no_samples(void)
{
    gr_transport_metric_t* metric = gr_transport_metric_new(g_ctx);
    
    double from[] = {0.0, 0.0};
    double to[] = {3.0, 4.0};
    
    /* Should default to Euclidean when not initialized */
    double dist = gr_transport_distance(metric, from, to, 2);
    
    /* Euclidean distance = 5.0 */
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 5.0, dist);
    
    gr_transport_metric_free(metric);
}

/* ============================================================================
 * Fragility Map Tests
 * ============================================================================ */

void test_fragility_map_new(void)
{
    gr_state_space_t* space = gr_state_space_new(g_ctx);
    
    gr_dimension_t dim = {
        .type = GR_DIM_SPOT,
        .min_value = 90.0,
        .max_value = 110.0,
        .num_points = 11
    };
    gr_state_space_add_dimension(space, &dim);
    
    gr_fragility_map_t* map = gr_fragility_map_new(g_ctx, space);
    TEST_ASSERT_NOT_NULL(map);
    
    gr_fragility_map_free(map);
    gr_state_space_free(space);
}

void test_fragility_num_regions_empty(void)
{
    gr_state_space_t* space = gr_state_space_new(g_ctx);
    
    gr_dimension_t dim = {
        .type = GR_DIM_SPOT,
        .min_value = 90.0,
        .max_value = 110.0,
        .num_points = 5
    };
    gr_state_space_add_dimension(space, &dim);
    
    gr_fragility_map_t* map = gr_fragility_map_new(g_ctx, space);
    
    size_t n = gr_fragility_map_get_num_fragile_regions(map);
    TEST_ASSERT_EQUAL_INT(0, n);  /* No computation yet */
    
    gr_fragility_map_free(map);
    gr_state_space_free(space);
}

/* ============================================================================
 * Integration Test: Simple Pricing Function
 * ============================================================================ */

/* A simple quadratic "pricing" function for testing */
static double simple_quadratic(const double* coords, int num_dims, void* user_data)
{
    (void)user_data;
    double sum = 0.0;
    for (int i = 0; i < num_dims; i++) {
        sum += coords[i] * coords[i];
    }
    return sum;
}

void test_integration_jacobian_on_quadratic(void)
{
    gr_state_space_t* space = gr_state_space_new(g_ctx);
    
    gr_dimension_t dim_x = {
        .type = GR_DIM_CUSTOM,
        .name = "x",
        .min_value = -5.0,
        .max_value = 5.0,
        .num_points = 21
    };
    
    gr_dimension_t dim_y = {
        .type = GR_DIM_CUSTOM,
        .name = "y",
        .min_value = -5.0,
        .max_value = 5.0,
        .num_points = 21
    };
    
    gr_state_space_add_dimension(space, &dim_x);
    gr_state_space_add_dimension(space, &dim_y);
    
    /* Map prices */
    gr_error_t err = gr_state_space_map_prices(space, simple_quadratic, NULL);
    TEST_ASSERT_EQUAL_INT(GR_SUCCESS, err);
    
    /* Compute Jacobian at (2, 3) */
    /* For f(x,y) = x² + y², gradient is (2x, 2y) = (4, 6) */
    gr_jacobian_t* jac = gr_jacobian_new(g_ctx, 2);
    double point[] = {2.0, 3.0};
    
    err = gr_jacobian_compute(jac, space, point);
    TEST_ASSERT_EQUAL_INT(GR_SUCCESS, err);
    
    double dx = gr_jacobian_get(jac, 0);
    double dy = gr_jacobian_get(jac, 1);
    
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 4.0, dx);
    TEST_ASSERT_DOUBLE_WITHIN(0.1, 6.0, dy);
    
    /* Gradient norm should be sqrt(4² + 6²) = sqrt(52) ≈ 7.21 */
    double norm = gr_jacobian_norm(jac);
    TEST_ASSERT_DOUBLE_WITHIN(0.2, 7.21, norm);
    
    gr_jacobian_free(jac);
    gr_state_space_free(space);
}

void test_integration_hessian_on_quadratic(void)
{
    gr_state_space_t* space = gr_state_space_new(g_ctx);
    
    gr_dimension_t dim_x = {
        .type = GR_DIM_CUSTOM,
        .name = "x",
        .min_value = -5.0,
        .max_value = 5.0,
        .num_points = 21
    };
    
    gr_dimension_t dim_y = {
        .type = GR_DIM_CUSTOM,
        .name = "y",
        .min_value = -5.0,
        .max_value = 5.0,
        .num_points = 21
    };
    
    gr_state_space_add_dimension(space, &dim_x);
    gr_state_space_add_dimension(space, &dim_y);
    
    gr_state_space_map_prices(space, simple_quadratic, NULL);
    
    /* Compute Hessian at (2, 3) */
    /* For f(x,y) = x² + y², Hessian is [[2,0],[0,2]] */
    gr_hessian_t* hess = gr_hessian_new(g_ctx, 2);
    double point[] = {2.0, 3.0};
    
    gr_error_t err = gr_hessian_compute(hess, space, point);
    TEST_ASSERT_EQUAL_INT(GR_SUCCESS, err);
    
    double h00 = gr_hessian_get(hess, 0, 0);
    double h01 = gr_hessian_get(hess, 0, 1);
    double h10 = gr_hessian_get(hess, 1, 0);
    double h11 = gr_hessian_get(hess, 1, 1);
    
    TEST_ASSERT_DOUBLE_WITHIN(0.2, 2.0, h00);
    TEST_ASSERT_DOUBLE_WITHIN(0.2, 0.0, h01);
    TEST_ASSERT_DOUBLE_WITHIN(0.2, 0.0, h10);
    TEST_ASSERT_DOUBLE_WITHIN(0.2, 2.0, h11);
    
    /* Trace should be 4 */
    double trace = gr_hessian_trace(hess);
    TEST_ASSERT_DOUBLE_WITHIN(0.4, 4.0, trace);
    
    gr_hessian_free(hess);
    gr_state_space_free(space);
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void)
{
    UnityBegin("georisk");
    
    /* Version tests */
    RUN_TEST(test_version_get);
    RUN_TEST(test_version_compatible);
    RUN_TEST(test_version_string);
    
    /* Error tests */
    RUN_TEST(test_error_string_success);
    RUN_TEST(test_error_string_null);
    RUN_TEST(test_error_string_invalid);
    
    /* Allocator tests */
    RUN_TEST(test_malloc_basic);
    RUN_TEST(test_calloc_zeroed);
    RUN_TEST(test_strdup);
    RUN_TEST(test_strdup_null);
    
    /* Context tests */
    setUp();
    RUN_TEST(test_context_new);
    tearDown();
    
    setUp();
    RUN_TEST(test_context_error_initial);
    tearDown();
    
    setUp();
    RUN_TEST(test_context_bump_size);
    tearDown();
    
    setUp();
    RUN_TEST(test_context_bump_size_invalid);
    tearDown();
    
    setUp();
    RUN_TEST(test_context_num_threads);
    tearDown();
    
    setUp();
    RUN_TEST(test_context_num_threads_invalid);
    tearDown();
    
    /* State space tests */
    setUp();
    RUN_TEST(test_state_space_new);
    tearDown();
    
    setUp();
    RUN_TEST(test_state_space_add_dimension);
    tearDown();
    
    setUp();
    RUN_TEST(test_state_space_multiple_dimensions);
    tearDown();
    
    setUp();
    RUN_TEST(test_state_space_invalid_dimension);
    tearDown();
    
    /* Jacobian tests */
    setUp();
    RUN_TEST(test_jacobian_new);
    tearDown();
    
    setUp();
    RUN_TEST(test_jacobian_invalid_dims);
    tearDown();
    
    setUp();
    RUN_TEST(test_jacobian_norm_uncomputed);
    tearDown();
    
    /* Hessian tests */
    setUp();
    RUN_TEST(test_hessian_new);
    tearDown();
    
    setUp();
    RUN_TEST(test_hessian_invalid_dims);
    tearDown();
    
    setUp();
    RUN_TEST(test_hessian_trace_uncomputed);
    tearDown();
    
    /* Constraint tests */
    setUp();
    RUN_TEST(test_constraint_surface_new);
    tearDown();
    
    setUp();
    RUN_TEST(test_constraint_add);
    tearDown();
    
    setUp();
    RUN_TEST(test_constraint_check_no_constraints);
    tearDown();
    
    /* Transport metric tests */
    setUp();
    RUN_TEST(test_transport_metric_new);
    tearDown();
    
    setUp();
    RUN_TEST(test_transport_distance_no_samples);
    tearDown();
    
    /* Fragility tests */
    setUp();
    RUN_TEST(test_fragility_map_new);
    tearDown();
    
    setUp();
    RUN_TEST(test_fragility_num_regions_empty);
    tearDown();
    
    /* Integration tests */
    setUp();
    RUN_TEST(test_integration_jacobian_on_quadratic);
    tearDown();
    
    setUp();
    RUN_TEST(test_integration_hessian_on_quadratic);
    tearDown();
    
    return UnityEnd();
}
