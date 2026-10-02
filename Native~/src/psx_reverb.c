#include "psx_reverb.h"

#include "host.h"

#include <limits.h>
#include <stdlib.h>

// The reverb's integer maths relies on both. C leaves each to the compiler; every compiler targeted does this.
_Static_assert((-1 >> 1) == -1, "right-shifting a negative value must be an arithmetic shift");
_Static_assert(INT_MAX >= 2147483647, "int must be at least 32 bits");

_Static_assert(PSX_REVERB_OFF == REVERB_MODE_OFF && PSX_REVERB_ROOM == REVERB_MODE_ROOM &&
                   PSX_REVERB_STUDIO_A == REVERB_MODE_STUDIO_A && PSX_REVERB_STUDIO_B == REVERB_MODE_STUDIO_B &&
                   PSX_REVERB_STUDIO_C == REVERB_MODE_STUDIO_C && PSX_REVERB_HALL == REVERB_MODE_HALL &&
                   PSX_REVERB_SPACE == REVERB_MODE_SPACE && PSX_REVERB_ECHO == REVERB_MODE_ECHO &&
                   PSX_REVERB_DELAY == REVERB_MODE_DELAY && PSX_REVERB_PIPE == REVERB_MODE_PIPE &&
                   PSX_REVERB_PRESETS == REVERB_MODE_COUNT,
               "the public presets are the reverb's own modes");

struct psx_reverb
{
    host_reverb host;
    int preset;
    int delay;
    int feedback;
};

static int clamp(int value, int low, int high)
{
    int clamped = value < low ? low : value > high ? high : value;
    return clamped;
}

// The reverb ignores both outside echo and delay. Delay keeps the feedback of 0 its mode starts with.
static void apply_echo_settings(psx_reverb *r)
{
    reverb *core = &r->host.unit.reverb;
    reverb_set_delay(core, r->delay);
    if (core->mode == REVERB_MODE_ECHO)
    {
        reverb_set_feedback(core, r->feedback);
    }
}

psx_reverb *psx_reverb_create(uint32_t sample_rate)
{
    psx_reverb *r = malloc(sizeof(*r));
    if (!r)
    {
        return NULL;
    }
    if (!host_reverb_init(&r->host, sample_rate))
    {
        free(r);
        return NULL;
    }
    r->preset = -1;
    r->delay = 127;
    r->feedback = 127;
    psx_reverb_set_preset(r, PSX_REVERB_STUDIO_C);
    psx_reverb_set_depth(r, 40);
    return r;
}

void psx_reverb_destroy(psx_reverb *r)
{
    if (!r)
    {
        return;
    }
    host_reverb_free(&r->host);
    free(r);
}

void psx_reverb_set_preset(psx_reverb *r, int preset)
{
    int held = clamp(preset, 0, PSX_REVERB_PRESETS - 1);
    if (held == r->preset)
    {
        return;
    }
    r->preset = held;
    reverb_set_mode(&r->host.unit.reverb, held);
    apply_echo_settings(r);
}

void psx_reverb_set_depth(psx_reverb *r, int depth)
{
    int16_t volume = (int16_t)(clamp(depth, 0, 127) << 8);
    r->host.unit.depth[0] = volume;
    r->host.unit.depth[1] = volume;
}

void psx_reverb_set_delay(psx_reverb *r, int delay)
{
    r->delay = clamp(delay, 1, 127);
    apply_echo_settings(r);
}

void psx_reverb_set_feedback(psx_reverb *r, int feedback)
{
    r->feedback = clamp(feedback, 0, 127);
    apply_echo_settings(r);
}

void psx_reverb_process(psx_reverb *r, const float *input, float *output, size_t frames)
{
    host_reverb_process(&r->host, input, output, frames);
}

uint32_t psx_reverb_version(void)
{
    uint32_t version = ((uint32_t)PSX_REVERB_VERSION_MAJOR << 16) | ((uint32_t)PSX_REVERB_VERSION_MINOR << 8) |
                       (uint32_t)PSX_REVERB_VERSION_PATCH;
    return version;
}
