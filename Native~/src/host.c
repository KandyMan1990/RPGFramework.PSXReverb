#include "host.h"

#include <assert.h>
#include <string.h>

static int16_t to16(float sample)
{
    float scaled = sample * 32768.0f;
    // A NaN is never equal to itself.
    int16_t value = scaled != scaled         ? 0
                    : scaled <= -32768.0f    ? INT16_MIN
                    : scaled >= 32767.0f     ? INT16_MAX
                                             : (int16_t)(scaled + (scaled < 0 ? -0.5f : 0.5f));
    return value;
}

static void enqueue(host_reverb *h, const float frame[2])
{
    assert(h->queue_count < HOST_QUEUE);
    uint32_t slot = (h->queue_first + h->queue_count) % HOST_QUEUE;
    h->queue[slot][0] = frame[0];
    h->queue[slot][1] = frame[1];
    h->queue_count++;
}

// Runs the two resamplers' counting over one whole cycle of their ratio, after which both are back where they began,
// and finds the most the reverb's output falls behind its input: the silence the queue must start with.
static uint32_t frames_to_prime(const host_reverb *h)
{
    resampler to_unit = h->to_unit;
    resampler from_unit = h->from_unit;
    const float silence[2] = {0.0f, 0.0f};
    float reduced[RESAMPLER_MAX_OUTPUTS][2];
    float expanded[RESAMPLER_MAX_OUTPUTS][2];
    uint32_t produced = 0;
    uint32_t most_behind = 0;
    for (uint32_t taken = 1; taken <= h->to_unit.down; taken++)
    {
        uint32_t count = resampler_push(&to_unit, silence, reduced);
        for (uint32_t i = 0; i < count; i++)
        {
            produced += resampler_push(&from_unit, silence, expanded);
        }
        if (taken > produced && taken - produced > most_behind)
        {
            most_behind = taken - produced;
        }
    }
    return most_behind;
}

bool host_reverb_init(host_reverb *h, uint32_t rate)
{
    reverb_unit_init(&h->unit);
    h->rate = rate;
    h->direct = rate == UNIT_RATE;
    h->queue_first = 0;
    h->queue_count = 0;
    h->primed = 0;
    memset(&h->to_unit, 0, sizeof(h->to_unit));
    memset(&h->from_unit, 0, sizeof(h->from_unit));
    if (h->direct)
    {
        return true;
    }

    if (!resampler_init(&h->to_unit, rate, UNIT_RATE) || !resampler_init(&h->from_unit, UNIT_RATE, rate))
    {
        host_reverb_free(h);
        return false;
    }
    // The counting runs on copies that share the histories, which stay silent: silence in, silence out.
    h->primed = frames_to_prime(h);
    const float silence[2] = {0.0f, 0.0f};
    for (uint32_t i = 0; i < h->primed; i++)
    {
        enqueue(h, silence);
    }
    return true;
}

void host_reverb_free(host_reverb *h)
{
    resampler_free(&h->to_unit);
    resampler_free(&h->from_unit);
}

static void run_unit(host_reverb *h, const float input[2], float output[2])
{
    const int16_t in[2] = {to16(input[0]), to16(input[1])};
    int16_t wet[2];
    reverb_unit_process(&h->unit, in, wet);
    output[0] = wet[0] / 32768.0f;
    output[1] = wet[1] / 32768.0f;
}

void host_reverb_process(host_reverb *h, const float *input, float *output, size_t frames)
{
    for (size_t i = 0; i < frames; i++)
    {
        const float in[2] = {input[2 * i], input[2 * i + 1]};
        if (h->direct)
        {
            run_unit(h, in, &output[2 * i]);
            continue;
        }

        float reduced[RESAMPLER_MAX_OUTPUTS][2];
        uint32_t count = resampler_push(&h->to_unit, in, reduced);
        for (uint32_t r = 0; r < count; r++)
        {
            float wet[2];
            float expanded[RESAMPLER_MAX_OUTPUTS][2];
            run_unit(h, reduced[r], wet);
            uint32_t made = resampler_push(&h->from_unit, wet, expanded);
            for (uint32_t e = 0; e < made; e++)
            {
                enqueue(h, expanded[e]);
            }
        }

        assert(h->queue_count > 0);
        output[2 * i] = h->queue[h->queue_first][0];
        output[2 * i + 1] = h->queue[h->queue_first][1];
        h->queue_first = (h->queue_first + 1) % HOST_QUEUE;
        h->queue_count--;
    }
}

double host_reverb_added_latency(const host_reverb *h)
{
    if (h->direct)
    {
        return 0.0;
    }
    double latency = resampler_delay(&h->to_unit) +
                     resampler_delay(&h->from_unit) * h->rate / (double)UNIT_RATE + h->primed;
    return latency;
}
