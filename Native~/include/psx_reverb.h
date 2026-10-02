#ifndef PSX_REVERB_H
#define PSX_REVERB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// major << 16 | minor << 8 | patch, the layout of a Unity effect definition's plugin version.
uint32_t psx_reverb_version(void);

#ifdef __cplusplus
}
#endif

#endif
