#include "check.h"
#include "resampler.h"
#include "signal.h"
#include "suites.h"

#include <math.h>
#include <stdlib.h>

// One second of a tone through a resampler, the output's first and last stretch dropped where the filter is filling
// and emptying. Returns the frames kept, in a buffer the caller frees.
static float *convert_tone(uint32_t rate_in, uint32_t rate_out, double frequency, size_t *kept)
{
    resampler r;
    *kept = 0;
    if (!resampler_init(&r, rate_in, rate_out))
    {
        CHECK(false);
        return NULL;
    }
    const size_t capacity = (size_t)rate_out + RESAMPLER_MAX_OUTPUTS;
    float *output = malloc(sizeof(float) * 2 * capacity);
    size_t made = 0;
    for (uint32_t i = 0; i < rate_in; i++)
    {
        const float sample = (float)(0.5 * sin(2.0 * PI * frequency * i / rate_in));
        const float frame[2] = {sample, sample};
        float out[RESAMPLER_MAX_OUTPUTS][2];
        const uint32_t count = resampler_push(&r, frame, out);
        for (uint32_t c = 0; c < count && made < capacity; c++, made++)
        {
            output[2 * made] = out[c][0];
            output[2 * made + 1] = out[c][1];
        }
    }
    const size_t settle = (size_t)(2.0 * resampler_delay(&r) * rate_out / rate_in) + 16;
    resampler_free(&r);
    *kept = made - 2 * settle;
    for (size_t i = 0; i < *kept * 2; i++)
    {
        output[i] = output[i + 2 * settle];
    }
    return output;
}

static void ratios_are_exact_and_bounded(void)
{
    static const struct
    {
        uint32_t in, out, up, down;
    } ratios[] = {{48000, 44100, 147, 160}, {44100, 48000, 160, 147}, {192000, 44100, 147, 640},
                  {8000, 44100, 441, 80},   {22050, 44100, 2, 1},     {44100, 88200, 2, 1}};
    for (size_t i = 0; i < sizeof(ratios) / sizeof(ratios[0]); i++)
    {
        resampler r;
        CHECK(resampler_init(&r, ratios[i].in, ratios[i].out));
        CHECK_EQ(ratios[i].up, r.up);
        CHECK_EQ(ratios[i].down, r.down);
        resampler_free(&r);
    }
    resampler r;
    // 44,100 phases, and eleven outputs to one input.
    CHECK(!resampler_init(&r, 44101, 44100));
    CHECK(!resampler_init(&r, 4000, 44100));
}

// Within 0.01 dB from 100 Hz to 10 kHz, both ways, at every rate whose band reaches that far.
static void the_band_is_flat(void)
{
    static const uint32_t rates[] = {48000, 96000, 32000, 192000, 22050, 8000};
    static const double frequencies[] = {100.0, 1000.0, 5000.0, 10000.0};
    for (size_t r = 0; r < sizeof(rates) / sizeof(rates[0]); r++)
    {
        for (size_t f = 0; f < sizeof(frequencies) / sizeof(frequencies[0]); f++)
        {
            const double lower = rates[r] < 44100 ? rates[r] : 44100;
            if (frequencies[f] > fmin(11025.0, 0.45 * lower))
            {
                continue;
            }
            for (int way = 0; way < 2; way++)
            {
                const uint32_t in = way ? 44100 : rates[r];
                const uint32_t out = way ? rates[r] : 44100;
                size_t kept;
                float *output = convert_tone(in, out, frequencies[f], &kept);
                if (output)
                {
                    const double decibels = 20.0 * log10(tone_amplitude(output, kept, frequencies[f], out) / 0.5);
                    CHECK(fabs(decibels) < 0.01);
                    free(output);
                }
            }
        }
    }
}

// 40 kHz at 96 kHz would fold to 4.1 kHz at 44.1 kHz.
static void aliases_are_rejected(void)
{
    size_t kept;
    float *output = convert_tone(96000, 44100, 40000.0, &kept);
    if (output)
    {
        CHECK(residual_rms(output, kept, 4100.0, 44100.0) + tone_amplitude(output, kept, 4100.0, 44100.0) < 0.5e-4);
        free(output);
    }
}

// 10 kHz at 44.1 kHz has an image at 34.1 kHz, which would fold to 13.9 kHz at 48 kHz.
static void images_are_rejected(void)
{
    size_t kept;
    float *output = convert_tone(44100, 48000, 10000.0, &kept);
    if (output)
    {
        CHECK(residual_rms(output, kept, 10000.0, 48000.0) < 0.5e-4);
        free(output);
    }
}

void resampler_tests(void)
{
    ratios_are_exact_and_bounded();
    the_band_is_flat();
    aliases_are_rejected();
    images_are_rejected();
}
