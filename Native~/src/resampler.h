#ifndef PSX_REVERB_RESAMPLER_H
#define PSX_REVERB_RESAMPLER_H

#include <stdbool.h>
#include <stdint.h>

enum
{
    // The most frames one input can give, which bounds the lowest rate supported to about 5.5 kHz.
    RESAMPLER_MAX_OUTPUTS = 8
};

// Converts stereo between two rates by an exact ratio, one side of which is the reverb unit's 44.1 kHz, with a
// polyphase windowed-sinc filter. The filter is flat to the reverb's band, 11,025 Hz or the lower rate's limit if
// that is less, and rejects by 100 dB whatever would fold into it.
typedef struct resampler
{
    // out / in = up / down, in lowest terms; up is also the number of phases.
    uint32_t up;
    uint32_t down;
    uint32_t taps;
    // up phases of taps each, phase by phase.
    float *coefficients;
    // taps frames per side, a ring ending at newest.
    float *history;
    uint32_t newest;
    // Where the next output falls: ahead inputs on from the next one pushed, phase up-ths of an input past that.
    uint32_t ahead;
    uint32_t phase;
} resampler;

// Designs the filter; allocates, so not for the audio thread. False for a ratio needing more than 1024 phases or more
// outputs per input than RESAMPLER_MAX_OUTPUTS.
bool resampler_init(resampler *r, uint32_t rate_in, uint32_t rate_out);
void resampler_free(resampler *r);

// Takes one frame and writes the frames it completes, returning how many.
uint32_t resampler_push(resampler *r, const float input[2], float output[RESAMPLER_MAX_OUTPUTS][2]);

// The filter's delay, in input frames.
double resampler_delay(const resampler *r);

#endif
