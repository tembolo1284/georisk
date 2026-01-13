/**
 * unity.h - Unity Test Framework (minimal subset)
 * 
 * Full version: https://github.com/ThrowTheSwitch/Unity
 * This is a minimal implementation for georisk tests.
 */

#ifndef UNITY_H
#define UNITY_H

#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * Configuration
 * ============================================================================ */

#ifndef UNITY_OUTPUT_CHAR
#include <stdio.h>
#define UNITY_OUTPUT_CHAR(c) putchar(c)
#endif

#ifndef UNITY_OUTPUT_FLUSH
#define UNITY_OUTPUT_FLUSH() fflush(stdout)
#endif

/* ============================================================================
 * Types
 * ============================================================================ */

typedef void (*UnityTestFunction)(void);

typedef struct {
    const char* TestFile;
    const char* CurrentTestName;
    int CurrentTestLineNumber;
    int NumberOfTests;
    int TestFailures;
    int TestIgnores;
    int CurrentTestFailed;
    int CurrentTestIgnored;
} Unity_t;

extern Unity_t Unity;

/* ============================================================================
 * Test Execution
 * ============================================================================ */

void UnityBegin(const char* filename);
int UnityEnd(void);
void UnityDefaultTestRun(UnityTestFunction func, const char* name, int line);

/* ============================================================================
 * Assertions
 * ============================================================================ */

void UnityAssertEqualNumber(long expected, long actual, 
                            const char* msg, int line, int style);
void UnityAssertFloatsWithin(float delta, float expected, float actual,
                             const char* msg, int line);
void UnityAssertDoublesWithin(double delta, double expected, double actual,
                              const char* msg, int line);
void UnityAssertEqualString(const char* expected, const char* actual,
                            const char* msg, int line);
void UnityAssertNull(const void* pointer, const char* msg, int line);
void UnityAssertNotNull(const void* pointer, const char* msg, int line);
void UnityFail(const char* msg, int line);
void UnityIgnore(const char* msg, int line);

/* ============================================================================
 * Test Macros
 * ============================================================================ */

#define TEST_PROTECT() setjmp(Unity.AbortFrame)

#define RUN_TEST(func) UnityDefaultTestRun(func, #func, __LINE__)

#define TEST_ASSERT(condition) \
    do { if (!(condition)) UnityFail(#condition, __LINE__); } while(0)

#define TEST_ASSERT_TRUE(condition) TEST_ASSERT(condition)
#define TEST_ASSERT_FALSE(condition) TEST_ASSERT(!(condition))

#define TEST_ASSERT_NULL(pointer) UnityAssertNull(pointer, NULL, __LINE__)
#define TEST_ASSERT_NOT_NULL(pointer) UnityAssertNotNull(pointer, NULL, __LINE__)

#define TEST_ASSERT_EQUAL(expected, actual) \
    UnityAssertEqualNumber((long)(expected), (long)(actual), NULL, __LINE__, 0)

#define TEST_ASSERT_EQUAL_INT(expected, actual) \
    UnityAssertEqualNumber((long)(expected), (long)(actual), NULL, __LINE__, 0)

#define TEST_ASSERT_EQUAL_PTR(expected, actual) \
    UnityAssertEqualNumber((long)(expected), (long)(actual), NULL, __LINE__, 0)

#define TEST_ASSERT_EQUAL_STRING(expected, actual) \
    UnityAssertEqualString(expected, actual, NULL, __LINE__)

#define TEST_ASSERT_FLOAT_WITHIN(delta, expected, actual) \
    UnityAssertFloatsWithin(delta, expected, actual, NULL, __LINE__)

#define TEST_ASSERT_DOUBLE_WITHIN(delta, expected, actual) \
    UnityAssertDoublesWithin(delta, expected, actual, NULL, __LINE__)

#define TEST_FAIL() UnityFail(NULL, __LINE__)
#define TEST_FAIL_MESSAGE(msg) UnityFail(msg, __LINE__)

#define TEST_IGNORE() UnityIgnore(NULL, __LINE__)
#define TEST_IGNORE_MESSAGE(msg) UnityIgnore(msg, __LINE__)

/* Greater/Less than */
#define TEST_ASSERT_GREATER_THAN(threshold, actual) \
    do { if (!((actual) > (threshold))) \
        UnityFail("Expected greater than " #threshold, __LINE__); } while(0)

#define TEST_ASSERT_LESS_THAN(threshold, actual) \
    do { if (!((actual) < (threshold))) \
        UnityFail("Expected less than " #threshold, __LINE__); } while(0)

#define TEST_ASSERT_GREATER_OR_EQUAL(threshold, actual) \
    do { if (!((actual) >= (threshold))) \
        UnityFail("Expected >= " #threshold, __LINE__); } while(0)

#endif /* UNITY_H */
