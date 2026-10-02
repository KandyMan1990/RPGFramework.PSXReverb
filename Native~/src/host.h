#ifndef PSX_REVERB_HOST_H
#define PSX_REVERB_HOST_H

#include "resampler.h"
#include "unit.h"

#include <stddef.h>

enum
{
    HOST_QUEUE = 32
};

// The reverb unit at whatever rate the host runs, in float, a full-scale sample being 1.0. At 44.1 kHz the unit runs
// directly; at any other rate a resampler takes the input to 44.1 kHz and another brings the reverb back.
typedef struct host_reverb
{
    reverb_unit unit;
    uint32_t rate;
    bool direct;
    resampler to_unit;
    resampler from_unit;
    // Output at the host's rate waiting to go out, primed with silence so it never runs dry.
    float queue[HOST_QUEUE][2];
    uint32_t queue_first;
    uint32_t queue_count;
    uint32_t primed;
} host_reverb;

// Allocates, so not for the audio thread. False for a rate the resamplers cannot reach.
bool host_reverb_init(host_reverb *h, uint32_t rate);
void host_reverb_free(host_reverb *h);

// Interleaved stereo, any number of frames; input and output may be the same buffer.
void host_reverb_process(host_reverb *h, const float *input, float *output, size_t frames);

// How many frames later than on the console the reverb arrives, from the resamplers alone.
double host_reverb_added_latency(const host_reverb *h);

#endif
