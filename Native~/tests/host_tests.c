#include "check.h"
#include "host.h"
#include "suites.h"

#include <math.h>

static const double PI = 3.14159265358979323846;

static host_reverb h;
static reverb_unit u;

static void start(uint32_t rate, int mode)
{
    CHECK(host_reverb_init(&h, rate));
    reverb_set_mode(&h.unit.reverb, mode);
    h.unit.depth[0] = INT16_MAX;
    h.unit.depth[1] = INT16_MAX;
}

static float noise(uint32_t *seed)
{
    *seed = *seed * 1664525u + 1013904223u;
    float sample = (float)((int32_t)(*seed >> 16) - 0x8000) / 32768.0f;
    return sample;
}

// At 44.1 kHz nothing is resampled, so the float path is the unit's own, sample for sample.
static void at_44100_the_host_is_the_unit(void)
{
    start(44100, REVERB_MODE_HALL);
    reverb_unit_init(&u);
    reverb_set_mode(&u.reverb, REVERB_MODE_HALL);
    u.depth[0] = INT16_MAX;
    u.depth[1] = INT16_MAX;
    uint32_t seed = 1;
    int differ = 0;
    for (int i = 0; i < 2 * 44100; i++)
    {
        float frame[2] = {noise(&seed), noise(&seed)};
        const int16_t in[2] = {(int16_t)(frame[0] * 32768.0f), (int16_t)(frame[1] * 32768.0f)};
        int16_t expected[2];
        reverb_unit_process(&u, in, expected);
        host_reverb_process(&h, frame, frame, 1);
        differ |= (int16_t)(frame[0] * 32768.0f) != expected[0] || (int16_t)(frame[1] * 32768.0f) != expected[1];
    }
    CHECK_EQ(0, differ);
    host_reverb_free(&h);
}

// A 1 kHz tone into delay comes back as itself inverted, its level within a thousandth, 2 * 16368 + 38 samples at
// 44.1 kHz later plus what the resamplers add, to a fraction of a sample at every rate. One sample out at 48 kHz would
// be an error of 0.03, thirty times what is allowed.
static void delay_returns_the_tone_on_time(void)
{
    static const uint32_t rates[] = {44100, 48000, 96000, 32000, 22050};
    for (size_t r = 0; r < sizeof(rates) / sizeof(rates[0]); r++)
    {
        uint32_t rate = rates[r];
        start(rate, REVERB_MODE_DELAY);
        double arrival = (2.0 * 16368 + 38) / 44100.0 * rate + host_reverb_added_latency(&h);
        size_t frames = (size_t)arrival + rate / 5;
        double early = 0.0, error = 0.0;
        for (size_t i = 0; i < frames; i++)
        {
            float tone = (float)(0.25 * sin(2.0 * PI * 1000.0 * (double)i / rate));
            float frame[2] = {tone, tone};
            host_reverb_process(&h, frame, frame, 1);
            if ((double)i < arrival - 100.0)
            {
                early = fmax(early, fabs(frame[0]));
            }
            else if ((double)i > arrival + 200.0)
            {
                double expected = -0.25 * sin(2.0 * PI * 1000.0 * ((double)i - arrival) / rate);
                error = fmax(error, fmax(fabs(frame[0] - expected), fabs(frame[1] - expected)));
            }
        }
        CHECK(early < 1e-4);
        CHECK(error < 1e-3);
        host_reverb_free(&h);
    }
}

static void unreachable_rates_are_refused(void)
{
    CHECK(!host_reverb_init(&h, 44101));
    CHECK(!host_reverb_init(&h, 4000));
}

// The queue starts with just enough silence never to run dry; the debug build asserts it at every frame.
static void the_queue_never_runs_dry(void)
{
    static const uint32_t rates[] = {8000, 11025, 16000, 22050, 24000, 32000, 48000, 88200, 96000, 176400, 192000};
    for (size_t r = 0; r < sizeof(rates) / sizeof(rates[0]); r++)
    {
        start(rates[r], REVERB_MODE_ROOM);
        uint32_t seed = 1;
        for (uint32_t i = 0; i < rates[r]; i++)
        {
            float frame[2] = {noise(&seed), noise(&seed)};
            host_reverb_process(&h, frame, frame, 1);
        }
        CHECK(h.queue_count <= HOST_QUEUE);
        host_reverb_free(&h);
    }
}

void host_tests(void)
{
    at_44100_the_host_is_the_unit();
    delay_returns_the_tone_on_time();
    unreachable_rates_are_refused();
    the_queue_never_runs_dry();
}
