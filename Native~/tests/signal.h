#ifndef SIGNAL_H
#define SIGNAL_H

#include <stddef.h>

// Both read the left of interleaved stereo.
//
// The amplitude of a tone at the given frequency, fitted by least squares, which is exact for a pure tone however many
// cycles the samples hold.
double tone_amplitude(const float *stereo, size_t frames, double frequency, double rate);

// The root mean square of what is left once that tone is taken away.
double residual_rms(const float *stereo, size_t frames, double frequency, double rate);

#endif
