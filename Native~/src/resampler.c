#include "resampler.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum
{
    MAX_PHASES = 1024,
    MAX_TAPS = 512
};

static const double PI = 3.14159265358979323846;
static const double ATTENUATION = 100.0;

static uint32_t greatest_common_divisor(uint32_t a, uint32_t b)
{
    while (b != 0)
    {
        uint32_t remainder = a % b;
        a = b;
        b = remainder;
    }
    return a;
}

// The zeroth-order modified Bessel function, which shapes the Kaiser window.
static double bessel_i0(double x)
{
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 500 && term > 1e-16 * sum; k++)
    {
        double factor = x / (2.0 * k);
        term *= factor * factor;
        sum += term;
    }
    return sum;
}

bool resampler_init(resampler *r, uint32_t rate_in, uint32_t rate_out)
{
    memset(r, 0, sizeof(*r));
    uint32_t divisor = greatest_common_divisor(rate_in, rate_out);
    r->up = rate_out / divisor;
    r->down = rate_in / divisor;
    if (r->up > MAX_PHASES || (r->up + r->down - 1) / r->down > RESAMPLER_MAX_OUTPUTS)
    {
        return false;
    }

    // The reverb holds nothing above 11,025 Hz, so the filter need only keep that flat and reject what would alias or
    // image into it: everything above the lower rate less the band. The transition is half the lower rate wide.
    double lower = rate_in < rate_out ? rate_in : rate_out;
    double pass = fmin(11025.0, 0.45 * lower);
    double stop = lower - pass;
    double rate_up = (double)r->up * rate_in;
    double cutoff = (pass + stop) / 2.0 / rate_up;
    double width = (stop - pass) / rate_up;
    uint32_t estimate = (uint32_t)ceil((ATTENUATION - 7.95) / (14.36 * width)) + 1;
    r->taps = (estimate + r->up - 1) / r->up;
    if (r->taps > MAX_TAPS)
    {
        return false;
    }

    uint32_t length = r->taps * r->up;
    r->coefficients = malloc(sizeof(float) * length);
    r->history = calloc(2u * r->taps, sizeof(float));
    if (!r->coefficients || !r->history)
    {
        resampler_free(r);
        return false;
    }

    double beta = 0.1102 * (ATTENUATION - 8.7);
    double centre = (length - 1) / 2.0;
    double window_scale = bessel_i0(beta);
    double values[MAX_TAPS];
    for (uint32_t phase = 0; phase < r->up; phase++)
    {
        double sum = 0.0;
        for (uint32_t tap = 0; tap < r->taps; tap++)
        {
            double position = phase + (double)tap * r->up;
            double offset = position - centre;
            double sinc = offset == 0.0 ? 2.0 * cutoff : sin(2.0 * PI * cutoff * offset) / (PI * offset);
            double along = 2.0 * position / (length - 1) - 1.0;
            double window = bessel_i0(beta * sqrt(fmax(0.0, 1.0 - along * along))) / window_scale;
            values[tap] = sinc * window;
            sum += values[tap];
        }
        // Each phase passes a constant at exactly unity.
        for (uint32_t tap = 0; tap < r->taps; tap++)
        {
            r->coefficients[phase * r->taps + tap] = (float)(values[tap] / sum);
        }
    }
    return true;
}

void resampler_free(resampler *r)
{
    free(r->coefficients);
    free(r->history);
    r->coefficients = NULL;
    r->history = NULL;
}

uint32_t resampler_push(resampler *r, const float input[2], float output[RESAMPLER_MAX_OUTPUTS][2])
{
    r->newest = r->newest + 1 == r->taps ? 0 : r->newest + 1;
    r->history[r->newest] = input[0];
    r->history[r->taps + r->newest] = input[1];

    uint32_t count = 0;
    while (r->ahead == 0)
    {
        const float *coefficients = r->coefficients + r->phase * r->taps;
        for (int side = 0; side < 2; side++)
        {
            const float *history = r->history + (uint32_t)side * r->taps;
            float sum = 0.0f;
            uint32_t index = r->newest;
            for (uint32_t tap = 0; tap < r->taps; tap++)
            {
                sum += coefficients[tap] * history[index];
                index = index == 0 ? r->taps - 1 : index - 1;
            }
            output[count][side] = sum;
        }
        count++;
        r->phase += r->down;
        r->ahead += r->phase / r->up;
        r->phase %= r->up;
    }
    r->ahead--;
    return count;
}

double resampler_delay(const resampler *r)
{
    double delay = ((double)r->taps * r->up - 1.0) / 2.0 / r->up;
    return delay;
}
