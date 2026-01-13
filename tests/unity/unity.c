/**
 * unity.c - Unity Test Framework implementation
 */

#include "unity.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ============================================================================
 * Global State
 * ============================================================================ */

Unity_t Unity;

/* ============================================================================
 * Output Helpers
 * ============================================================================ */

static void UnityPrint(const char* str)
{
    if (str) {
        while (*str) {
            UNITY_OUTPUT_CHAR(*str++);
        }
    }
}

static void UnityPrintNumber(long number)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%ld", number);
    UnityPrint(buffer);
}

static void UnityPrintFloat(double number)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.6f", number);
    UnityPrint(buffer);
}

static void UnityPrintOK(void)
{
    UnityPrint("\033[32mOK\033[0m");
}

static void UnityPrintFAIL(void)
{
    UnityPrint("\033[31mFAIL\033[0m");
}

static void UnityPrintIGNORE(void)
{
    UnityPrint("\033[33mIGNORE\033[0m");
}

/* ============================================================================
 * Test Execution
 * ============================================================================ */

void UnityBegin(const char* filename)
{
    Unity.TestFile = filename;
    Unity.CurrentTestName = NULL;
    Unity.CurrentTestLineNumber = 0;
    Unity.NumberOfTests = 0;
    Unity.TestFailures = 0;
    Unity.TestIgnores = 0;
    Unity.CurrentTestFailed = 0;
    Unity.CurrentTestIgnored = 0;
    
    UnityPrint("\n");
    UnityPrint("============================================\n");
    UnityPrint("  georisk test suite\n");
    UnityPrint("============================================\n\n");
}

int UnityEnd(void)
{
    UnityPrint("\n--------------------------------------------\n");
    UnityPrintNumber(Unity.NumberOfTests);
    UnityPrint(" Tests ");
    UnityPrintNumber(Unity.TestFailures);
    UnityPrint(" Failures ");
    UnityPrintNumber(Unity.TestIgnores);
    UnityPrint(" Ignored\n");
    
    if (Unity.TestFailures == 0) {
        UnityPrint("\033[32mALL TESTS PASSED\033[0m\n");
    } else {
        UnityPrint("\033[31mTESTS FAILED\033[0m\n");
    }
    
    UnityPrint("--------------------------------------------\n\n");
    UNITY_OUTPUT_FLUSH();
    
    return Unity.TestFailures;
}

void UnityDefaultTestRun(UnityTestFunction func, const char* name, int line)
{
    Unity.CurrentTestName = name;
    Unity.CurrentTestLineNumber = line;
    Unity.CurrentTestFailed = 0;
    Unity.CurrentTestIgnored = 0;
    Unity.NumberOfTests++;
    
    UnityPrint("  ");
    UnityPrint(name);
    UnityPrint(" ... ");
    UNITY_OUTPUT_FLUSH();
    
    func();
    
    if (Unity.CurrentTestIgnored) {
        UnityPrintIGNORE();
    } else if (Unity.CurrentTestFailed) {
        UnityPrintFAIL();
    } else {
        UnityPrintOK();
    }
    
    UnityPrint("\n");
    UNITY_OUTPUT_FLUSH();
}

/* ============================================================================
 * Assertions
 * ============================================================================ */

void UnityAssertEqualNumber(long expected, long actual,
                            const char* msg, int line, int style)
{
    (void)style;
    
    if (expected != actual) {
        Unity.CurrentTestFailed = 1;
        Unity.TestFailures++;
        
        UnityPrint("\n    FAILED at line ");
        UnityPrintNumber(line);
        UnityPrint(": Expected ");
        UnityPrintNumber(expected);
        UnityPrint(" but was ");
        UnityPrintNumber(actual);
        
        if (msg) {
            UnityPrint(" (");
            UnityPrint(msg);
            UnityPrint(")");
        }
        UnityPrint("\n");
    }
}

void UnityAssertFloatsWithin(float delta, float expected, float actual,
                             const char* msg, int line)
{
    float diff = expected - actual;
    if (diff < 0) diff = -diff;
    
    if (diff > delta) {
        Unity.CurrentTestFailed = 1;
        Unity.TestFailures++;
        
        UnityPrint("\n    FAILED at line ");
        UnityPrintNumber(line);
        UnityPrint(": Expected ");
        UnityPrintFloat(expected);
        UnityPrint(" +/- ");
        UnityPrintFloat(delta);
        UnityPrint(" but was ");
        UnityPrintFloat(actual);
        
        if (msg) {
            UnityPrint(" (");
            UnityPrint(msg);
            UnityPrint(")");
        }
        UnityPrint("\n");
    }
}

void UnityAssertDoublesWithin(double delta, double expected, double actual,
                              const char* msg, int line)
{
    double diff = expected - actual;
    if (diff < 0) diff = -diff;
    
    if (diff > delta) {
        Unity.CurrentTestFailed = 1;
        Unity.TestFailures++;
        
        UnityPrint("\n    FAILED at line ");
        UnityPrintNumber(line);
        UnityPrint(": Expected ");
        UnityPrintFloat(expected);
        UnityPrint(" +/- ");
        UnityPrintFloat(delta);
        UnityPrint(" but was ");
        UnityPrintFloat(actual);
        
        if (msg) {
            UnityPrint(" (");
            UnityPrint(msg);
            UnityPrint(")");
        }
        UnityPrint("\n");
    }
}

void UnityAssertEqualString(const char* expected, const char* actual,
                            const char* msg, int line)
{
    int match = 0;
    
    if (expected == actual) {
        match = 1;
    } else if (expected && actual) {
        match = (strcmp(expected, actual) == 0);
    }
    
    if (!match) {
        Unity.CurrentTestFailed = 1;
        Unity.TestFailures++;
        
        UnityPrint("\n    FAILED at line ");
        UnityPrintNumber(line);
        UnityPrint(": Expected \"");
        UnityPrint(expected ? expected : "NULL");
        UnityPrint("\" but was \"");
        UnityPrint(actual ? actual : "NULL");
        UnityPrint("\"");
        
        if (msg) {
            UnityPrint(" (");
            UnityPrint(msg);
            UnityPrint(")");
        }
        UnityPrint("\n");
    }
}

void UnityAssertNull(const void* pointer, const char* msg, int line)
{
    if (pointer != NULL) {
        Unity.CurrentTestFailed = 1;
        Unity.TestFailures++;
        
        UnityPrint("\n    FAILED at line ");
        UnityPrintNumber(line);
        UnityPrint(": Expected NULL");
        
        if (msg) {
            UnityPrint(" (");
            UnityPrint(msg);
            UnityPrint(")");
        }
        UnityPrint("\n");
    }
}

void UnityAssertNotNull(const void* pointer, const char* msg, int line)
{
    if (pointer == NULL) {
        Unity.CurrentTestFailed = 1;
        Unity.TestFailures++;
        
        UnityPrint("\n    FAILED at line ");
        UnityPrintNumber(line);
        UnityPrint(": Expected not NULL");
        
        if (msg) {
            UnityPrint(" (");
            UnityPrint(msg);
            UnityPrint(")");
        }
        UnityPrint("\n");
    }
}

void UnityFail(const char* msg, int line)
{
    Unity.CurrentTestFailed = 1;
    Unity.TestFailures++;
    
    UnityPrint("\n    FAILED at line ");
    UnityPrintNumber(line);
    
    if (msg) {
        UnityPrint(": ");
        UnityPrint(msg);
    }
    UnityPrint("\n");
}

void UnityIgnore(const char* msg, int line)
{
    (void)line;
    
    Unity.CurrentTestIgnored = 1;
    Unity.TestIgnores++;
    
    if (msg) {
        UnityPrint(" [");
        UnityPrint(msg);
        UnityPrint("] ");
    }
}
