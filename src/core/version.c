/**
 * version.c - Version information and compatibility checking
 * 
 * Provides runtime version checking to ensure header/library compatibility.
 * This prevents subtle bugs from mismatched headers and shared libraries.
 */

#include "georisk.h"
#include <stdio.h>

/* ============================================================================
 * Version String (compiled into binary)
 * ============================================================================ */

static const char gr_version_str[] = 
    "georisk " 
    GR_STRINGIFY(GR_VERSION_MAJOR) "."
    GR_STRINGIFY(GR_VERSION_MINOR) "."
    GR_STRINGIFY(GR_VERSION_PATCH);

/* Helper macro for stringification */
#ifndef GR_STRINGIFY
#define GR_STRINGIFY_HELPER(x) #x
#define GR_STRINGIFY(x) GR_STRINGIFY_HELPER(x)
#endif

/* ============================================================================
 * Public API
 * ============================================================================ */

GR_API unsigned int gr_get_version(void)
{
    return GR_VERSION;
}

GR_API int gr_is_compatible_dll(void)
{
    /*
     * Compatibility rules:
     * - Major version must match exactly
     * - Minor version of DLL must be >= header minor version
     * 
     * This allows forward-compatible updates within a major version.
     */
    unsigned int dll_version = gr_get_version();
    unsigned int dll_major = (dll_version >> 16) & 0xFF;
    unsigned int dll_minor = (dll_version >> 8) & 0xFF;
    
    unsigned int header_major = GR_VERSION_MAJOR;
    unsigned int header_minor = GR_VERSION_MINOR;
    
    if (dll_major != header_major) {
        return 0;  /* Major version mismatch - incompatible */
    }
    
    if (dll_minor < header_minor) {
        return 0;  /* DLL is older than header - may be missing features */
    }
    
    return 1;  /* Compatible */
}

GR_API const char* gr_version_string(void)
{
    return gr_version_str;
}

/* ============================================================================
 * Error Code Strings
 * ============================================================================ */

static const char* gr_error_strings[] = {
    [GR_SUCCESS]                    = "Success",
    [GR_ERROR_NULL_POINTER]         = "Null pointer",
    [GR_ERROR_INVALID_ARGUMENT]     = "Invalid argument",
    [GR_ERROR_OUT_OF_MEMORY]        = "Out of memory",
    [GR_ERROR_DIMENSION_MISMATCH]   = "Dimension mismatch",
    [GR_ERROR_SINGULAR_MATRIX]      = "Singular matrix",
    [GR_ERROR_NUMERICAL_INSTABILITY]= "Numerical instability",
    [GR_ERROR_PRICING_ENGINE_FAILED]= "Pricing engine failed",
    [GR_ERROR_CONSTRAINT_VIOLATION] = "Constraint violation",
    [GR_ERROR_NOT_INITIALIZED]      = "Not initialized"
};

#define GR_NUM_ERROR_CODES (sizeof(gr_error_strings) / sizeof(gr_error_strings[0]))

GR_API const char* gr_error_string(gr_error_t err)
{
    if (err < 0 || (size_t)err >= GR_NUM_ERROR_CODES) {
        return "Unknown error";
    }
    
    const char* str = gr_error_strings[err];
    return str ? str : "Unknown error";
}
