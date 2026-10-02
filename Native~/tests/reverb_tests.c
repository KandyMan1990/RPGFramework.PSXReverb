#include "check.h"
#include "reverb.h"
#include "suites.h"

#include <string.h>

// Delay and echo at their default delay of 127: the same-side write sits (1FFFh - 1005h) * 4 samples beyond the comb
// tap that reads it back, and each all-pass stage adds 4 more.
enum
{
    COMB_DELAY = (0x1FFF - 0x1005) * 4,
    ECHO_TICK = COMB_DELAY + 4 + 4
};

typedef struct stereo
{
    int16_t left;
    int16_t right;
} stereo;

static reverb r;

static stereo step(int16_t left)
{
    const int16_t input[2] = {left, 0};
    int16_t output[2];
    reverb_process(&r, input, output);
    stereo result = {output[0], output[1]};
    return result;
}

// Feeds silence until the given tick, counting the first impulse as tick 0, and checks nothing came out on either side.
static void expect_silence_until(int tick, int from)
{
    int heard = 0;
    for (int i = from; i < tick; i++)
    {
        stereo out = step(0);
        heard |= out.left | out.right;
    }
    CHECK_EQ(0, heard);
}

// Sony's library tables, as psyz's test reads them back from the registers after setting each mode.
static void presets_match_the_console_library(void)
{
    static const uint16_t dapf1[REVERB_MODE_COUNT] = {0x0000, 0x007D, 0x0033, 0x00B1, 0x00E3,
                                                      0x01A5, 0x033D, 0x0001, 0x0001, 0x0017};
    static const uint16_t viir[REVERB_MODE_COUNT] = {0x0000, 0x6D80, 0x70F0, 0x70F0, 0x6F60,
                                                     0x6000, 0x7E00, 0x7FFF, 0x7FFF, 0x70F0};
    for (int mode = 0; mode < REVERB_MODE_COUNT; mode++)
    {
        const reverb_registers *g = &reverb_presets[mode].registers;
        CHECK_EQ(dapf1[mode], g->dAPF1);
        CHECK_EQ(viir[mode], g->vIIR);
        CHECK_EQ(mode == REVERB_MODE_OFF ? 0 : 0x8000, g->vLIN);
        CHECK_EQ(mode == REVERB_MODE_OFF ? 0 : 0x8000, g->vRIN);
    }
}

static void every_preset_fits_its_work_area(void)
{
    for (int mode = 0; mode < REVERB_MODE_COUNT; mode++)
    {
        const reverb_registers *g = &reverb_presets[mode].registers;
        const uint16_t addresses[] = {g->mLSAME,  g->mRSAME,  g->mLCOMB1, g->mRCOMB1, g->mLCOMB2,
                                      g->mRCOMB2, g->dLSAME,  g->dRSAME,  g->mLDIFF,  g->mRDIFF,
                                      g->mLCOMB3, g->mRCOMB3, g->mLCOMB4, g->mRCOMB4, g->dLDIFF,
                                      g->dRDIFF,  g->mLAPF1,  g->mRAPF1,  g->mLAPF2,  g->mRAPF2};
        uint32_t samples = reverb_presets[mode].work_area_bytes / 2;
        for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i++)
        {
            CHECK((uint32_t)addresses[i] * 4u < samples);
        }
    }
}

// The console's reflection quirk at vIIR = -1.0 is not reproduced, so no preset may use it.
static void no_preset_sets_the_reflection_volume_to_minus_one(void)
{
    for (int mode = 0; mode < REVERB_MODE_COUNT; mode++)
    {
        CHECK(reverb_presets[mode].registers.vIIR != 0x8000);
    }
}

// The library's formula at its defaults must give back PSX-SPX's echo and delay tables, which come from elsewhere.
static void echo_and_delay_defaults_reproduce_their_tables(void)
{
    reverb_set_mode(&r, REVERB_MODE_ECHO);
    reverb_set_delay(&r, 127);
    reverb_set_feedback(&r, 127);
    CHECK(memcmp(&r.registers, &reverb_presets[REVERB_MODE_ECHO].registers, sizeof(r.registers)) == 0);

    reverb_set_mode(&r, REVERB_MODE_DELAY);
    reverb_set_delay(&r, 127);
    reverb_set_feedback(&r, 0);
    CHECK(memcmp(&r.registers, &reverb_presets[REVERB_MODE_DELAY].registers, sizeof(r.registers)) == 0);
}

static void delay_time_sets_the_same_side_and_comb_addresses(void)
{
    reverb_set_mode(&r, REVERB_MODE_ECHO);
    reverb_set_delay(&r, 64);
    // (64 << 13) / 127 = 4128 and (64 << 12) / 127 = 2064, against the table's dAPF1 1, dAPF2 1, mRCOMB1 5, dRSAME 5,
    // mLAPF2 4 and mRAPF2 2.
    CHECK_EQ(4127, r.registers.mLSAME);
    CHECK_EQ(2063, r.registers.mRSAME);
    CHECK_EQ(2069, r.registers.mLCOMB1);
    CHECK_EQ(2069, r.registers.dLSAME);
    CHECK_EQ(2068, r.registers.mLAPF1);
    CHECK_EQ(2066, r.registers.mRAPF1);
}

static void feedback_flips_sign_at_its_top_step(void)
{
    reverb_set_mode(&r, REVERB_MODE_ECHO);
    reverb_set_feedback(&r, 64);
    CHECK_EQ(0x4102, r.registers.vWALL);
    reverb_set_feedback(&r, 126);
    CHECK_EQ(0x7FFB, r.registers.vWALL);
    reverb_set_feedback(&r, 127);
    CHECK_EQ(0x8100, r.registers.vWALL);
}

static void delay_and_feedback_are_held_to_their_range(void)
{
    reverb_registers one;
    reverb_set_mode(&r, REVERB_MODE_ECHO);
    reverb_set_delay(&r, 1);
    one = r.registers;
    reverb_set_delay(&r, 0);
    CHECK(memcmp(&r.registers, &one, sizeof(one)) == 0);
    reverb_set_delay(&r, 500);
    CHECK(memcmp(&r.registers, &reverb_presets[REVERB_MODE_ECHO].registers, sizeof(one)) == 0);

    reverb_set_feedback(&r, -3);
    CHECK_EQ(0, r.registers.vWALL);
    reverb_set_feedback(&r, 300);
    CHECK_EQ(0x8100, r.registers.vWALL);
}

static void other_modes_ignore_delay_and_feedback(void)
{
    reverb_set_mode(&r, REVERB_MODE_HALL);
    reverb_set_delay(&r, 10);
    reverb_set_feedback(&r, 10);
    CHECK(memcmp(&r.registers, &reverb_presets[REVERB_MODE_HALL].registers, sizeof(r.registers)) == 0);
}

static void off_is_silent(void)
{
    reverb_set_mode(&r, REVERB_MODE_OFF);
    int heard = 0;
    for (int i = 0; i < REVERB_RATE; i++)
    {
        stereo out = step((int16_t)(i % 2 ? 20000 : -20000));
        heard |= out.left | out.right;
    }
    CHECK_EQ(0, heard);
}

// Input volume is -1.0, so 4000h goes in as -0.5 and comes back once as -0.5, rounded down at each halving.
static void delay_echoes_an_impulse_once_inverted(void)
{
    reverb_set_mode(&r, REVERB_MODE_DELAY);
    stereo out = step(0x4000);
    CHECK_EQ(0, out.left);
    expect_silence_until(ECHO_TICK, 1);
    out = step(0);
    CHECK_EQ(-16384, out.left);
    CHECK_EQ(0, out.right);
}

static void delay_time_moves_the_echo(void)
{
    reverb_set_mode(&r, REVERB_MODE_DELAY);
    reverb_set_delay(&r, 64);
    step(0x4000);
    // The same-side write at 4127 and the comb tap at 2069, then the two all-pass stages.
    expect_silence_until((4127 - 2069) * 4 + 8, 1);
    stereo out = step(0);
    CHECK_EQ(-16384, out.left);
}

// Echo's feedback of -0.99 sends the first echo back round inverted. Worked through: the same-side write is -16384,
// then -1 on every sample after as the reflection's rounding leaks it; the feedback returns 16256, which the
// reflection takes to 16255, the comb to 32509 and the first all-pass halves to 16254.
static void echo_feeds_back_inverted(void)
{
    reverb_set_mode(&r, REVERB_MODE_ECHO);
    step(0x4000);
    for (int i = 1; i < ECHO_TICK; i++)
    {
        step(0);
    }
    CHECK_EQ(-16384, step(0).left);
    for (int i = ECHO_TICK + 1; i < ECHO_TICK + COMB_DELAY; i++)
    {
        step(0);
    }
    CHECK_EQ(16254, step(0).left);
}

// Left reaches right only through the different-side reflections: the left write at mLDIFF 25Ch is read back by the
// right through dLDIFF 18Fh, 820 samples later, then by the right's third comb tap 356 after that. Worked through:
// -14456 on the left, 11293 then 9964 on the right, 10596 from the comb, 5298 and 3414 through the all-passes, which
// scale the final 2100 out with no delay of their own.
// The sample after brings in both reflections' previous-sample terms: the left's -14456 decays to -1702, which the
// right takes to 2345 alongside its own 9964 held, and 494 comes out.
static void left_crosses_to_right_through_the_different_side(void)
{
    reverb_set_mode(&r, REVERB_MODE_STUDIO_A);
    int heard = step(0x4000).right;
    for (int i = 1; i < 1176; i++)
    {
        heard |= step(0).right;
    }
    CHECK_EQ(0, heard);
    CHECK_EQ(2100, step(0).right);
    CHECK_EQ(494, step(0).right);
}

// The mirror: the right write at mRDIFF 18Eh, the left's dRDIFF B5h 868 samples later, its comb tap at 22Fh 180 after.
static void right_crosses_to_left_through_the_different_side(void)
{
    reverb_set_mode(&r, REVERB_MODE_STUDIO_A);
    const int16_t impulse[2] = {0, 0x4000};
    int16_t output[2];
    reverb_process(&r, impulse, output);
    int heard = output[0];
    for (int i = 1; i < 1048; i++)
    {
        heard |= step(0).left;
    }
    CHECK_EQ(0, heard);
    CHECK_EQ(2100, step(0).left);
}

// The ring's end is crossed between the impulse and its echo.
static void the_ring_wraps_without_moving_the_echo(void)
{
    reverb_set_mode(&r, REVERB_MODE_DELAY);
    int start = r.ring_length - 1000;
    for (int i = 0; i < start; i++)
    {
        step(0);
    }
    step(0x4000);
    expect_silence_until(ECHO_TICK, 1);
    CHECK_EQ(-16384, step(0).left);
}

// Full-scale input times -1.0 is +1.0, one past what 16 bits hold. It saturates on the way in to 32767, which the
// reflection takes to 32766, the comb to 65530 and the first all-pass halves to 32765 — near the top rather than
// wrapped round to the bottom.
static void full_scale_input_saturates(void)
{
    reverb_set_mode(&r, REVERB_MODE_DELAY);
    step(INT16_MIN);
    expect_silence_until(ECHO_TICK, 1);
    CHECK_EQ(32765, step(0).left);
}

// With feedback at +0.9999, a constant full-scale input keeps the same-side reflection at its ceiling. Without
// saturation it would wrap negative.
static void feedback_saturates_rather_than_wraps(void)
{
    reverb_set_mode(&r, REVERB_MODE_ECHO);
    reverb_set_feedback(&r, 126);
    int lowest = INT16_MAX;
    for (int i = 0; i < 3 * COMB_DELAY; i++)
    {
        stereo out = step(INT16_MIN);
        if (i >= ECHO_TICK && out.left < lowest)
        {
            lowest = out.left;
        }
    }
    CHECK(lowest > 32000);
}

static void changing_preset_cuts_the_tail(void)
{
    reverb_set_mode(&r, REVERB_MODE_DELAY);
    step(0x4000);
    for (int i = 1; i < 100; i++)
    {
        step(0);
    }
    reverb_set_mode(&r, REVERB_MODE_DELAY);
    expect_silence_until(ECHO_TICK + 100, 0);
}

// Two seconds of noise through every preset: the sanitizers in the debug build stop on any out-of-bounds access or
// overflow, and every preset but off must answer.
static void every_preset_runs_on_noise(void)
{
    for (int mode = 0; mode < REVERB_MODE_COUNT; mode++)
    {
        uint32_t seed = 1;
        int heard = 0;
        reverb_set_mode(&r, mode);
        for (int i = 0; i < 2 * REVERB_RATE; i++)
        {
            seed = seed * 1664525u + 1013904223u;
            const int16_t input[2] = {(int16_t)((int32_t)(seed >> 16) - 0x8000),
                                      (int16_t)((int32_t)((seed >> 8) & 0xFFFF) - 0x8000)};
            int16_t output[2];
            reverb_process(&r, input, output);
            heard |= output[0] | output[1];
        }
        CHECK_EQ(mode != REVERB_MODE_OFF, heard != 0);
    }
}

void reverb_tests(void)
{
    presets_match_the_console_library();
    every_preset_fits_its_work_area();
    no_preset_sets_the_reflection_volume_to_minus_one();
    echo_and_delay_defaults_reproduce_their_tables();
    delay_time_sets_the_same_side_and_comb_addresses();
    feedback_flips_sign_at_its_top_step();
    delay_and_feedback_are_held_to_their_range();
    other_modes_ignore_delay_and_feedback();
    off_is_silent();
    delay_echoes_an_impulse_once_inverted();
    delay_time_moves_the_echo();
    echo_feeds_back_inverted();
    left_crosses_to_right_through_the_different_side();
    right_crosses_to_left_through_the_different_side();
    the_ring_wraps_without_moving_the_echo();
    full_scale_input_saturates();
    feedback_saturates_rather_than_wraps();
    changing_preset_cuts_the_tail();
    every_preset_runs_on_noise();
}
