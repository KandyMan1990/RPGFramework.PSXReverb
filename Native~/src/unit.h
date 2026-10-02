#ifndef PSX_REVERB_UNIT_H
#define PSX_REVERB_UNIT_H

#include "reverb.h"

enum
{
    UNIT_RATE = 44100
};

// The console's whole reverb unit at 44.1 kHz: its resampling filter down to the reverb at 22,050 Hz, the reverb, the
// same filter back up, and the output volume. Its mode, delay and feedback are set on the reverb inside it.
typedef struct reverb_unit
{
    reverb reverb;
    // The output volume, the console's depth: signed, 8000h being -1.0.
    int16_t depth[2];
    // The last 64 inputs at 44.1 kHz and the last 32 outputs at 22,050 Hz, by side, each a ring.
    int16_t down[2][64];
    int16_t up[2][32];
    int32_t position;
} reverb_unit;

// Mode off, depth 0, every buffer clear.
void reverb_unit_init(reverb_unit *u);

// One sample at 44.1 kHz for both sides.
void reverb_unit_process(reverb_unit *u, const int16_t input[2], int16_t output[2]);

#endif
