#ifndef PSX_REVERB_NUMERIC_H
#define PSX_REVERB_NUMERIC_H

#include <stdint.h>

static inline int clamp(int value, int low, int high)
{
    const int clamped = value < low ? low : value > high ? high : value;
    return clamped;
}

static inline int16_t saturate16(int32_t value)
{
    const int16_t saturated = (int16_t)(value < INT16_MIN ? INT16_MIN : value > INT16_MAX ? INT16_MAX : value);
    return saturated;
}

// A float sample, full scale being 1.0, as 16 bits: rounded half away from zero and saturated, a NaN as 0.
static inline int16_t float_to16(float sample)
{
    const float scaled = sample * 32768.0f;
    // A NaN is never equal to itself. GCC calls the conditional an int, so it is narrowed explicitly after.
    const int32_t rounded = scaled != scaled      ? 0
                            : scaled <= -32768.0f ? INT16_MIN
                            : scaled >= 32767.0f  ? INT16_MAX
                                                  : (int32_t)(scaled + (scaled < 0 ? -0.5f : 0.5f));
    const int16_t value = (int16_t)rounded;
    return value;
}

#endif
