#include "check.h"
#include "host.h"
#include "psx_reverb.h"
#include "signal.h"
#include "suites.h"

static host_reverb expected;

static void noise_block(uint32_t *seed, float *block, size_t frames)
{
    for (size_t i = 0; i < frames * 2; i++)
    {
        block[i] = noise(seed);
    }
}

// A second of noise through both, which must come out the same to the bit: the interface runs the host layer it wraps.
static bool matches_host(psx_reverb *r, uint32_t rate)
{
    static float in[1024 * 2], out_api[1024 * 2], out_host[1024 * 2];
    uint32_t seed = 7;
    bool same = true;
    for (uint32_t done = 0; done < rate; done += 1024)
    {
        noise_block(&seed, in, 1024);
        psx_reverb_process(r, in, out_api, 1024);
        host_reverb_process(&expected, in, out_host, 1024);
        for (size_t i = 0; i < 1024 * 2; i++)
        {
            same = same && out_api[i] == out_host[i];
        }
    }
    return same;
}

static void start_expected(uint32_t rate, int mode, int depth)
{
    CHECK(host_reverb_init(&expected, rate));
    reverb_set_mode(&expected.unit.reverb, mode);
    expected.unit.depth[0] = (int16_t)(depth << 8);
    expected.unit.depth[1] = (int16_t)(depth << 8);
}

static void rates_it_cannot_reach_give_none(void)
{
    psx_reverb *r = psx_reverb_create(44101);
    CHECK(r == NULL);
    psx_reverb_destroy(r);
    r = psx_reverb_create(0);
    CHECK(r == NULL);
    psx_reverb_destroy(r);
    r = psx_reverb_create(48000);
    CHECK(r != NULL);
    psx_reverb_destroy(r);
}

static void it_starts_at_studio_c_and_depth_40(void)
{
    psx_reverb *r = psx_reverb_create(48000);
    start_expected(48000, REVERB_MODE_STUDIO_C, 40);
    CHECK(matches_host(r, 48000));
    host_reverb_free(&expected);
    psx_reverb_destroy(r);
}

// 0.5 on an even sample reaches delay through the filter's centre tap as 2000h and returns as -8192; depth 40 is
// 2800h, so -8192 * 10240 >> 15 = -2560 comes out, 2 * 16368 + 38 samples later.
static float delay_echo_after(int second_preset)
{
    psx_reverb *r = psx_reverb_create(44100);
    psx_reverb_set_preset(r, PSX_REVERB_DELAY);
    float frame[2] = {0.5f, 0.5f};
    psx_reverb_process(r, frame, frame, 1);
    for (int i = 1; i < 2 * 16368 + 38; i++)
    {
        if (i == 100)
        {
            psx_reverb_set_preset(r, second_preset);
        }
        frame[0] = frame[1] = 0.0f;
        psx_reverb_process(r, frame, frame, 1);
    }
    frame[0] = frame[1] = 0.0f;
    psx_reverb_process(r, frame, frame, 1);
    psx_reverb_destroy(r);
    return frame[0];
}

static void only_a_different_preset_cuts_the_tail(void)
{
    CHECK(delay_echo_after(PSX_REVERB_DELAY) == -2560.0f / 32768.0f);
    CHECK(delay_echo_after(PSX_REVERB_ECHO) == 0.0f);
}

// Delay and feedback set under studio C take hold when echo is chosen; delay keeps its feedback of 0 whatever is set.
static void echo_settings_wait_for_echo_and_delay(void)
{
    psx_reverb *r = psx_reverb_create(44100);
    psx_reverb_set_delay(r, 64);
    psx_reverb_set_feedback(r, 100);
    psx_reverb_set_preset(r, PSX_REVERB_ECHO);
    start_expected(44100, REVERB_MODE_ECHO, 40);
    reverb_set_delay(&expected.unit.reverb, 64);
    reverb_set_feedback(&expected.unit.reverb, 100);
    CHECK(matches_host(r, 44100));
    host_reverb_free(&expected);
    psx_reverb_destroy(r);

    r = psx_reverb_create(44100);
    psx_reverb_set_delay(r, 64);
    psx_reverb_set_feedback(r, 100);
    psx_reverb_set_preset(r, PSX_REVERB_DELAY);
    start_expected(44100, REVERB_MODE_DELAY, 40);
    reverb_set_delay(&expected.unit.reverb, 64);
    CHECK(matches_host(r, 44100));
    host_reverb_free(&expected);
    psx_reverb_destroy(r);
}

static void out_of_range_values_are_held(void)
{
    psx_reverb *r = psx_reverb_create(44100);
    psx_reverb_set_preset(r, 42);
    psx_reverb_set_depth(r, 500);
    start_expected(44100, REVERB_MODE_PIPE, 127);
    CHECK(matches_host(r, 44100));
    host_reverb_free(&expected);
    psx_reverb_destroy(r);
}

void api_tests(void)
{
    rates_it_cannot_reach_give_none();
    it_starts_at_studio_c_and_depth_40();
    only_a_different_preset_cuts_the_tail();
    echo_settings_wait_for_echo_and_delay();
    out_of_range_values_are_held();
}
