#include "check.h"
#include "suites.h"
#include "unit.h"

// Delay's echo comes back 16,368 samples at 22,050 Hz after it went in; each filter adds 19 samples at 44.1 kHz.
enum
{
    REVERB_ECHO = 16368,
    FILTER_DELAY = 19
};

static reverb_unit u;

static int16_t step(int16_t left, int16_t *right)
{
    const int16_t input[2] = {left, 0};
    int16_t output[2];
    reverb_unit_process(&u, input, output);
    *right = output[1];
    return output[0];
}

static void start(int mode, int16_t depth)
{
    reverb_unit_init(&u);
    reverb_set_mode(&u.reverb, mode);
    u.depth[0] = depth;
    u.depth[1] = depth;
}

// An impulse on an even sample meets only the filter's centre tap, so the reverb takes one sample, 2000h, nine
// samples in. Delay sends it back as -8192 and then -1 for good, as its reflection's rounding leaks. Out of the filter
// the echo itself lands on sample 2 * 16368 + 38, the two filters' 19 each, between -5123 and -5124: the 2806h taps
// either side weight -8192 equally, and the -1 after it tips the second.
static void the_filters_delay_by_38_samples(void)
{
    start(REVERB_MODE_DELAY, 0x7FFF);
    const int echo = 2 * REVERB_ECHO + 2 * FILTER_DELAY;
    int16_t right;
    int heard = step(0x4000, &right);
    int heard_right = right;
    for (int tick = 1; tick < echo - FILTER_DELAY; tick++)
    {
        heard |= step(0, &right);
        heard_right |= right;
    }
    CHECK_EQ(0, heard);
    for (int tick = echo - FILTER_DELAY; tick < echo - 1; tick++)
    {
        step(0, &right);
        heard_right |= right;
    }
    CHECK_EQ(-5123, step(0, &right));
    heard_right |= right;
    CHECK_EQ(-8192, step(0, &right));
    heard_right |= right;
    CHECK_EQ(-5124, step(0, &right));
    CHECK_EQ(0, heard_right | right);
}

// An impulse on an odd sample meets only the side taps, so the reverb takes the filter's own coefficients, halved:
// -1, 1, -5, 17 ... 5123, 5123 ... -5, 1, -1. Delay inverts them, its reflection's leak nudges a few by one, and a
// depth of -1.0 inverts them back. The samples that fall on the reverb's own outputs show them unfiltered.
static void the_filter_is_the_consoles(void)
{
    static const int16_t expected[20] = {0,    1,   -3,   17,  -50, 133, -306, 666, -1478, 5123,
                                         5124, -1478, 666, -306, 133, -50, 17,   -3,  1,     0};
    start(REVERB_MODE_DELAY, INT16_MIN);
    int16_t right;
    step(0, &right);
    step(0x4000, &right);
    // The reverb's sample m comes out on 2m + 1 + 19.
    for (int tick = 2; tick < 2 * REVERB_ECHO + FILTER_DELAY + 1; tick++)
    {
        step(0, &right);
    }
    for (int m = 0; m < 20; m++)
    {
        CHECK_EQ(expected[m], step(0, &right));
        step(0, &right);
    }
}

static void depth_zero_is_silent(void)
{
    start(REVERB_MODE_HALL, 0);
    int heard = 0;
    for (int tick = 0; tick < UNIT_RATE; tick++)
    {
        int16_t right;
        heard |= step((int16_t)(tick % 2 ? 20000 : -20000), &right) | right;
    }
    CHECK_EQ(0, heard);
}

// A preset change clears the reverb's work area, which does not hold the filters' history: what is already in the
// output filter still plays out, for no more than the 38 samples its window spans at 44.1 kHz.
static void a_preset_change_leaves_only_the_filter_to_play_out(void)
{
    start(REVERB_MODE_DELAY, 0x7FFF);
    int16_t right;
    step(0x4000, &right);
    for (int tick = 1; tick < 2 * REVERB_ECHO + 2 * FILTER_DELAY; tick++)
    {
        step(0, &right);
    }
    reverb_set_mode(&u.reverb, REVERB_MODE_DELAY);
    int late = 0;
    for (int tick = 0; tick < 2 * REVERB_ECHO; tick++)
    {
        const int16_t left = step(0, &right);
        if (tick >= 2 * FILTER_DELAY)
        {
            late |= left;
        }
    }
    CHECK_EQ(0, late);
}

void unit_tests(void)
{
    the_filters_delay_by_38_samples();
    the_filter_is_the_consoles();
    depth_zero_is_silent();
    a_preset_change_leaves_only_the_filter_to_play_out();
}
