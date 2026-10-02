#ifndef PSX_REVERB_H
#define PSX_REVERB_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The console's presets, numbered as its library numbers them.
enum
{
    PSX_REVERB_OFF,
    PSX_REVERB_ROOM,
    PSX_REVERB_STUDIO_A,
    PSX_REVERB_STUDIO_B,
    PSX_REVERB_STUDIO_C,
    PSX_REVERB_HALL,
    PSX_REVERB_SPACE,
    PSX_REVERB_ECHO,
    PSX_REVERB_DELAY,
    PSX_REVERB_PIPE,
    PSX_REVERB_PRESETS
};

typedef struct psx_reverb psx_reverb;

// A reverb for the given sample rate, at studio C and depth 40. NULL for a rate it cannot be resampled to, or if
// memory runs out. Allocates, so not for the audio thread.
psx_reverb *psx_reverb_create(uint32_t sample_rate);
void psx_reverb_destroy(psx_reverb *reverb);

// Settings take effect from the next frame processed; none is safe to call during processing on another thread.
// A preset other than the current one clears the reverb, cutting its tail; the current one again changes nothing.
// Out of range values are held to the nearest end.
void psx_reverb_set_preset(psx_reverb *reverb, int preset);

// The reverb's output volume, 0-127 as the console's tools give it.
void psx_reverb_set_depth(psx_reverb *reverb, int depth);

// Kept whatever the preset, and applied while it is echo or delay: delay time 1-127 to both, feedback 0-127 to echo
// alone, delay being echo without it. Both start at 127, the console's own echo.
void psx_reverb_set_delay(psx_reverb *reverb, int delay);
void psx_reverb_set_feedback(psx_reverb *reverb, int feedback);

// Interleaved stereo, a full-scale sample being 1.0; writes the reverb alone. Input and output may be one buffer.
void psx_reverb_process(psx_reverb *reverb, const float *input, float *output, size_t frames);

// major << 16 | minor << 8 | patch, the layout of a Unity effect definition's plugin version.
uint32_t psx_reverb_version(void);

#ifdef __cplusplus
}
#endif

#endif
