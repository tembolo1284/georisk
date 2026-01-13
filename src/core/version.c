/**
 * version.c - Version information and compatibility checking
 */

#include "georisk.h"
#include <stdio.h>

#define GR_STRINGIFY_HELPER(x) #x
#define GR_STRINGIFY(x) GR_STRINGIFY_HELPER(x)

static const char gr_version_str[] = 
    "georisk "
    GR_STRINGIFY(GR_VERSION_MAJOR) "."
    GR_STRINGIFY(GR_VERSION_MINOR) "."
    GR_STRINGIFY(GR_VERSION_PATCH);

GR_API unsigned int gr_get_version(void)
{
    return GR_VERSION;
}

GR_API int gr_is_compatible_dll(void)
{
    unsigned int dll_version = gr_get_version();
    unsigned int dll_major = (dll_version >> 16) & 0xFF;
    unsigned int dll_minor = (dll_version >> 8) & 0xFF;
    
    unsigned int header_major = GR_VERSION_MAJOR;
    unsigned int header_minor = GR_VERSION_MINOR;
    
    if (dll_major != header_major) {
        return 0;
    }
    
    if (dll_minor < header_minor) {
        return 0;
    }
    
    return 1;
}

GR_API const char* gr_version_string(void)
{
    return gr_version_str;
}

GR_API const char* gr_error_string(gr_error_t err)
{
    switch (err) {
        case GR_SUCCESS:                     return "Success";
        case GR_ERROR_NULL_POINTER:          return "Null pointer";
        case GR_ERROR_INVALID_ARGUMENT:      return "Invalid argument";
        case GR_ERROR_OUT_OF_MEMORY:         return "Out of memory";
        case GR_ERROR_DIMENSION_MISMATCH:    return "Dimension mismatch";
        case GR_ERROR_SINGULAR_MATRIX:       return "Singular matrix";
        case GR_ERROR_NUMERICAL_INSTABILITY: return "Numerical instability";
        case GR_ERROR_PRICING_ENGINE_FAILED: return "Pricing engine failed";
        case GR_ERROR_CONSTRAINT_VIOLATION:  return "Constraint violation";
        case GR_ERROR_NOT_INITIALIZED:       return "Not initialized";
        default:                             return "Unknown error";
    }
}
