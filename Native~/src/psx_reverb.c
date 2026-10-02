#include "psx_reverb.h"

#include <limits.h>

// The reverb's integer maths relies on both. C leaves each to the compiler; every compiler targeted does this.
_Static_assert((-1 >> 1) == -1, "right-shifting a negative value must be an arithmetic shift");
_Static_assert(INT_MAX >= 2147483647, "int must be at least 32 bits");

uint32_t psx_reverb_version(void)
{
    uint32_t version = ((uint32_t)PSX_REVERB_VERSION_MAJOR << 16) | ((uint32_t)PSX_REVERB_VERSION_MINOR << 8) |
                       (uint32_t)PSX_REVERB_VERSION_PATCH;
    return version;
}
