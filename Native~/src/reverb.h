#ifndef PSX_REVERB_REVERB_H
#define PSX_REVERB_REVERB_H

#include <stdint.h>

enum
{
    REVERB_MODE_OFF,
    REVERB_MODE_ROOM,
    REVERB_MODE_STUDIO_A,
    REVERB_MODE_STUDIO_B,
    REVERB_MODE_STUDIO_C,
    REVERB_MODE_HALL,
    REVERB_MODE_SPACE,
    REVERB_MODE_ECHO,
    REVERB_MODE_DELAY,
    REVERB_MODE_PIPE,
    REVERB_MODE_COUNT
};

enum
{
    REVERB_RATE = 22050,
    // Echo and delay's work area, the largest, in samples.
    REVERB_RING_CAPACITY = 0x18040 / 2
};

// The console's reverb registers in its order, raw. Addresses count 8 bytes, four samples; volumes are signed,
// 8000h being -1.0.
typedef struct reverb_registers
{
    uint16_t dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL;
    uint16_t vAPF1, vAPF2, mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2;
    uint16_t dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4;
    uint16_t dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2, vLIN, vRIN;
} reverb_registers;

typedef struct reverb_preset
{
    uint32_t work_area_bytes;
    reverb_registers registers;
} reverb_preset;

extern const reverb_preset reverb_presets[REVERB_MODE_COUNT];

// Where each read and write falls, in samples ahead of the current position, and each volume as a signed value.
// Index 0 is left, 1 right. Derived from the registers whenever they change.
typedef struct reverb_taps
{
    int32_t same_write[2], same_previous[2], same_feedback[2];
    int32_t diff_write[2], diff_previous[2], diff_feedback[2];
    int32_t comb[4][2];
    int32_t apf1_write[2], apf1_feedback[2], apf2_write[2], apf2_feedback[2];
    int32_t input_volume[2], comb_volume[4];
    int32_t iir_volume, wall_volume, apf1_volume, apf2_volume;
} reverb_taps;

typedef struct reverb
{
    int mode;
    int delay;
    int feedback;
    reverb_registers registers;
    reverb_taps taps;
    int32_t ring_length;
    int32_t position;
    int16_t ring[REVERB_RING_CAPACITY];
} reverb;

// Loads a preset and clears the ring, so whatever was ringing stops. Echo starts at delay and feedback 127, delay at
// delay 127 and feedback 0, as the console's library sets them.
void reverb_set_mode(reverb *r, int mode);

// Echo and delay only, as on the console; other modes ignore them. Delay is held to 1-127 and feedback to 0-127.
void reverb_set_delay(reverb *r, int delay);
void reverb_set_feedback(reverb *r, int feedback);

// One sample at 22,050 Hz for both sides, before the output volume.
void reverb_process(reverb *r, const int16_t input[2], int16_t output[2]);

#endif
