#ifndef SIGNAL_H
#define SIGNAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

extern const double PI;

// Noise from a linear congruential generator, from -1.0 to just under 1.0. Each call moves the seed on.
float noise(uint32_t *seed);

// Both read the left of interleaved stereo.
//
// The amplitude of a tone at the given frequency, fitted by least squares, which is exact for a pure tone however many
// cycles the samples hold.
double tone_amplitude(const float *stereo, size_t frames, double frequency, double rate);

// The root mean square of what is left once that tone is taken away.
double residual_rms(const float *stereo, size_t frames, double frequency, double rate);

#ifdef __cplusplus
}
#endif

#endif
