#include "reverb.h"

#include <string.h>

static int32_t signed_volume(uint16_t raw)
{
    int32_t volume = raw >= 0x8000 ? (int32_t)raw - 0x10000 : (int32_t)raw;
    return volume;
}

static int32_t saturate(int32_t value)
{
    int32_t saturated = value < INT16_MIN ? INT16_MIN : value > INT16_MAX ? INT16_MAX : value;
    return saturated;
}

// -1.0 has no positive counterpart in 16 bits; the console gives the largest it has.
static int32_t negate(int32_t volume)
{
    int32_t negated = volume == INT16_MIN ? INT16_MAX : -volume;
    return negated;
}

static int clamp(int value, int low, int high)
{
    int clamped = value < low ? low : value > high ? high : value;
    return clamped;
}

// Every offset is kept within the ring, so no setting can reach outside it. Where the console's own address would
// leave its work area (an address register of 0 read one sample back), this wraps inside the ring instead; in every
// preset that value only reaches comb taps whose volume is 0.
static int32_t offset(const reverb *r, int32_t address, int32_t back)
{
    int32_t samples = (address * 4 - back) % r->ring_length;
    if (samples < 0)
    {
        samples += r->ring_length;
    }
    return samples;
}

static void derive_taps(reverb *r)
{
    const reverb_registers *g = &r->registers;
    reverb_taps *t = &r->taps;

    t->same_write[0] = offset(r, g->mLSAME, 0);
    t->same_write[1] = offset(r, g->mRSAME, 0);
    t->same_previous[0] = offset(r, g->mLSAME, 1);
    t->same_previous[1] = offset(r, g->mRSAME, 1);
    t->same_feedback[0] = offset(r, g->dLSAME, 0);
    t->same_feedback[1] = offset(r, g->dRSAME, 0);

    t->diff_write[0] = offset(r, g->mLDIFF, 0);
    t->diff_write[1] = offset(r, g->mRDIFF, 0);
    t->diff_previous[0] = offset(r, g->mLDIFF, 1);
    t->diff_previous[1] = offset(r, g->mRDIFF, 1);
    t->diff_feedback[0] = offset(r, g->dRDIFF, 0);
    t->diff_feedback[1] = offset(r, g->dLDIFF, 0);

    t->comb[0][0] = offset(r, g->mLCOMB1, 0);
    t->comb[0][1] = offset(r, g->mRCOMB1, 0);
    t->comb[1][0] = offset(r, g->mLCOMB2, 0);
    t->comb[1][1] = offset(r, g->mRCOMB2, 0);
    t->comb[2][0] = offset(r, g->mLCOMB3, 0);
    t->comb[2][1] = offset(r, g->mRCOMB3, 0);
    t->comb[3][0] = offset(r, g->mLCOMB4, 0);
    t->comb[3][1] = offset(r, g->mRCOMB4, 0);

    t->apf1_write[0] = offset(r, g->mLAPF1, 0);
    t->apf1_write[1] = offset(r, g->mRAPF1, 0);
    t->apf1_feedback[0] = offset(r, g->mLAPF1 - g->dAPF1, 0);
    t->apf1_feedback[1] = offset(r, g->mRAPF1 - g->dAPF1, 0);
    t->apf2_write[0] = offset(r, g->mLAPF2, 0);
    t->apf2_write[1] = offset(r, g->mRAPF2, 0);
    t->apf2_feedback[0] = offset(r, g->mLAPF2 - g->dAPF2, 0);
    t->apf2_feedback[1] = offset(r, g->mRAPF2 - g->dAPF2, 0);

    t->input_volume[0] = signed_volume(g->vLIN);
    t->input_volume[1] = signed_volume(g->vRIN);
    t->comb_volume[0] = signed_volume(g->vCOMB1);
    t->comb_volume[1] = signed_volume(g->vCOMB2);
    t->comb_volume[2] = signed_volume(g->vCOMB3);
    t->comb_volume[3] = signed_volume(g->vCOMB4);
    t->iir_volume = signed_volume(g->vIIR);
    t->wall_volume = signed_volume(g->vWALL);
    t->apf1_volume = signed_volume(g->vAPF1);
    t->apf2_volume = signed_volume(g->vAPF2);
}

static int32_t ring_index(const reverb *r, int32_t ahead)
{
    int32_t index = r->position + ahead;
    if (index >= r->ring_length)
    {
        index -= r->ring_length;
    }
    return index;
}

static int32_t ring_read(const reverb *r, int32_t ahead)
{
    int32_t sample = r->ring[ring_index(r, ahead)];
    return sample;
}

static void ring_write(reverb *r, int32_t ahead, int32_t sample)
{
    r->ring[ring_index(r, ahead)] = (int16_t)saturate(sample);
}

static int is_echo_or_delay(const reverb *r)
{
    int result = r->mode == REVERB_MODE_ECHO || r->mode == REVERB_MODE_DELAY;
    return result;
}

void reverb_set_mode(reverb *r, int mode)
{
    const reverb_preset *preset = &reverb_presets[mode];
    r->mode = mode;
    r->delay = is_echo_or_delay(r) ? 127 : 0;
    r->feedback = mode == REVERB_MODE_ECHO ? 127 : 0;
    r->registers = preset->registers;
    r->ring_length = (int32_t)(preset->work_area_bytes / 2);
    r->position = 0;
    memset(r->ring, 0, sizeof(r->ring[0]) * (size_t)r->ring_length);
    derive_taps(r);
}

void reverb_set_delay(reverb *r, int delay)
{
    if (!is_echo_or_delay(r))
    {
        return;
    }

    // At 0 the console's same-side taps leave the work area and read whatever else is in its sound memory.
    r->delay = clamp(delay, 1, 127);

    const reverb_registers *preset = &reverb_presets[r->mode].registers;
    int32_t whole = (r->delay << 13) / 127;
    int32_t half = (r->delay << 12) / 127;
    r->registers.mLSAME = (uint16_t)(whole - preset->dAPF1);
    r->registers.mRSAME = (uint16_t)(half - preset->dAPF2);
    r->registers.mLCOMB1 = (uint16_t)(half + preset->mRCOMB1);
    r->registers.dLSAME = (uint16_t)(half + preset->dRSAME);
    r->registers.mLAPF1 = (uint16_t)(half + preset->mLAPF2);
    r->registers.mRAPF1 = (uint16_t)(half + preset->mRAPF2);
    derive_taps(r);
}

void reverb_set_feedback(reverb *r, int feedback)
{
    if (!is_echo_or_delay(r))
    {
        return;
    }

    r->feedback = clamp(feedback, 0, 127);
    // 127 gives 8100h, which as a volume is -0.99: the console's feedback flips sign at its top step.
    r->registers.vWALL = (uint16_t)((r->feedback * 0x8100) / 127);
    derive_taps(r);
}

void reverb_process(reverb *r, const int16_t input[2], int16_t output[2])
{
    const reverb_taps *t = &r->taps;
    int32_t hold = 0x8000 - t->iir_volume;

    // Products are shifted down by 14 and their sum halved, as the console rounds. Shifting each product by 15
    // instead is the same maths but differs in the last bit.
    for (int side = 0; side < 2; side++)
    {
        int32_t in = (input[side] * t->input_volume[side]) >> 14;
        int32_t same_in = saturate((((ring_read(r, t->same_feedback[side]) * t->wall_volume) >> 14) + in) >> 1);
        int32_t diff_in = saturate((((ring_read(r, t->diff_feedback[side]) * t->wall_volume) >> 14) + in) >> 1);
        int32_t same = saturate(
            (((same_in * t->iir_volume) >> 14) + ((ring_read(r, t->same_previous[side]) * hold) >> 14)) >> 1);
        int32_t diff = saturate(
            (((diff_in * t->iir_volume) >> 14) + ((ring_read(r, t->diff_previous[side]) * hold) >> 14)) >> 1);
        ring_write(r, t->same_write[side], same);
        ring_write(r, t->diff_write[side], diff);

        int32_t comb = 0;
        for (int tap = 0; tap < 4; tap++)
        {
            comb += (ring_read(r, t->comb[tap][side]) * t->comb_volume[tap]) >> 14;
        }

        int32_t apf1_feedback = ring_read(r, t->apf1_feedback[side]);
        int32_t apf2_feedback = ring_read(r, t->apf2_feedback[side]);
        int32_t apf1 = saturate((comb + ((apf1_feedback * negate(t->apf1_volume)) >> 14)) >> 1);
        int32_t apf2 = saturate(
            apf1_feedback +
            ((((apf1 * t->apf1_volume) >> 14) + ((apf2_feedback * negate(t->apf2_volume)) >> 14)) >> 1));
        output[side] = (int16_t)saturate(apf2_feedback + ((apf2 * t->apf2_volume) >> 15));
        ring_write(r, t->apf1_write[side], apf1);
        ring_write(r, t->apf2_write[side], apf2);
    }

    r->position = r->position + 1 == r->ring_length ? 0 : r->position + 1;
}
