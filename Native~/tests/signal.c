#include "signal.h"

#include <math.h>

static const double PI = 3.14159265358979323846;

// Solves for a and b in a sin + b cos, the best fit to the samples.
static void fit(const float *stereo, size_t frames, double frequency, double rate, double *a, double *b)
{
    double ss = 0.0, sc = 0.0, cc = 0.0, xs = 0.0, xc = 0.0;
    for (size_t i = 0; i < frames; i++)
    {
        double angle = 2.0 * PI * frequency * (double)i / rate;
        double s = sin(angle);
        double c = cos(angle);
        double x = stereo[2 * i];
        ss += s * s;
        sc += s * c;
        cc += c * c;
        xs += x * s;
        xc += x * c;
    }
    double determinant = ss * cc - sc * sc;
    *a = (xs * cc - xc * sc) / determinant;
    *b = (xc * ss - xs * sc) / determinant;
}

double tone_amplitude(const float *stereo, size_t frames, double frequency, double rate)
{
    double a, b;
    fit(stereo, frames, frequency, rate, &a, &b);
    double amplitude = sqrt(a * a + b * b);
    return amplitude;
}

double residual_rms(const float *stereo, size_t frames, double frequency, double rate)
{
    double a, b;
    fit(stereo, frames, frequency, rate, &a, &b);
    double sum = 0.0;
    for (size_t i = 0; i < frames; i++)
    {
        double angle = 2.0 * PI * frequency * (double)i / rate;
        double left = stereo[2 * i] - (a * sin(angle) + b * cos(angle));
        sum += left * left;
    }
    double rms = sqrt(sum / (double)frames);
    return rms;
}
