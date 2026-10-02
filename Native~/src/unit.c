#include "unit.h"

#include <string.h>

// The console's 39-tap half-band filter less its zero taps: these 20 sit two samples apart, symmetric about a centre
// tap of 4000h (0.5) that falls between the 10th and 11th.
static const int32_t side_taps[20] = {-0x0001, 0x0002,  -0x000A, 0x0023,  -0x0067, 0x010A,  -0x0268,
                                      0x0534,  -0x0B90, 0x2806,  0x2806,  -0x0B90, 0x0534,  -0x0268,
                                      0x010A,  -0x0067, 0x0023,  -0x000A, 0x0002,  -0x0001};

static int16_t saturate16(int32_t value)
{
    int16_t saturated = (int16_t)(value < INT16_MIN ? INT16_MIN : value > INT16_MAX ? INT16_MAX : value);
    return saturated;
}

// The 39 inputs ending at the newest, filtered: the side taps on every other one, the centre on the 20th back.
static int16_t downsample(const int16_t history[64], int32_t newest)
{
    int32_t sum = 0x4000 * history[(newest + 64 - 19) & 63];
    for (int tap = 0; tap < 20; tap++)
    {
        sum += side_taps[tap] * history[(newest + 64 - 38 + 2 * tap) & 63];
    }
    int16_t sample = saturate16(sum >> 15);
    return sample;
}

// The point halfway between the 10th and 11th most recent outputs. Zero-stuffing halves the signal, so the result is
// doubled: shifted by 14, not 15.
static int16_t upsample_between(const int16_t history[32], int32_t newest)
{
    int32_t sum = 0;
    for (int tap = 0; tap < 20; tap++)
    {
        sum += side_taps[tap] * history[(newest + 32 - 19 + tap) & 31];
    }
    int16_t sample = saturate16(sum >> 14);
    return sample;
}

void reverb_unit_init(reverb_unit *u)
{
    memset(u->down, 0, sizeof(u->down));
    memset(u->up, 0, sizeof(u->up));
    u->depth[0] = 0;
    u->depth[1] = 0;
    u->position = 0;
    reverb_set_mode(&u->reverb, REVERB_MODE_OFF);
}

// Every other sample runs the reverb on both sides: the filter takes one 22,050 Hz sample from the inputs and the
// reverb's answer joins the outputs. The samples between take the output that falls exactly on one of its outputs, the
// centre tap alone. Either way each filter delays by 19 samples, 38 in all, as measured on the console.
void reverb_unit_process(reverb_unit *u, const int16_t input[2], int16_t output[2])
{
    int32_t p = u->position;
    int16_t wet[2];
    u->down[0][p] = input[0];
    u->down[1][p] = input[1];

    if (p & 1)
    {
        const int16_t reduced[2] = {downsample(u->down[0], p), downsample(u->down[1], p)};
        int16_t produced[2];
        reverb_process(&u->reverb, reduced, produced);
        for (int side = 0; side < 2; side++)
        {
            u->up[side][p >> 1] = produced[side];
            wet[side] = upsample_between(u->up[side], p >> 1);
        }
    }
    else
    {
        for (int side = 0; side < 2; side++)
        {
            wet[side] = u->up[side][((p >> 1) + 32 - 10) & 31];
        }
    }

    u->position = (p + 1) & 63;
    for (int side = 0; side < 2; side++)
    {
        output[side] = saturate16((wet[side] * u->depth[side]) >> 15);
    }
}
