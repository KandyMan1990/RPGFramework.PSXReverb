#include "check.h"
#include "model_fingerprints.h"
#include "reverb.h"
#include "suites.h"
#include "unit.h"

#include <stdbool.h>
#include <stdio.h>

static reverb r;
static reverb_unit u;
static int16_t samples[MODEL_UNIT_TICKS * 2];

static uint64_t fingerprint(const int16_t *values, size_t count)
{
    uint64_t hash = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < count; i++)
    {
        const uint16_t bits = (uint16_t)values[i];
        hash = (hash ^ (bits & 0xFFu)) * 0x100000001B3ull;
        hash = (hash ^ (bits >> 8)) * 0x100000001B3ull;
    }
    return hash;
}

static void next_noise(uint32_t *seed, int shift, int16_t input[2])
{
    *seed = *seed * 1664525u + 1013904223u;
    input[0] = (int16_t)(((int32_t)(*seed >> 16) - 0x8000) >> shift);
    input[1] = (int16_t)(((int32_t)((*seed >> 8) & 0xFFFF) - 0x8000) >> shift);
}

static void set_up(reverb *target, int mode, int delay, int feedback)
{
    reverb_set_mode(target, mode);
    if (delay >= 0)
    {
        reverb_set_delay(target, delay);
    }
    if (feedback >= 0)
    {
        reverb_set_feedback(target, feedback);
    }
}

static void change_halfway(reverb *target, int later_mode, int later_delay)
{
    if (later_mode >= 0)
    {
        reverb_set_mode(target, later_mode);
    }
    if (later_delay >= 0)
    {
        reverb_set_delay(target, later_delay);
    }
}

static void run(const model_case *c)
{
    uint32_t seed = 1;
    set_up(&r, c->mode, c->delay, c->feedback);
    for (int tick = 0; tick < MODEL_TICKS; tick++)
    {
        if (tick == MODEL_TICKS / 2)
        {
            change_halfway(&r, c->later_mode, c->later_delay);
        }
        int16_t input[2];
        next_noise(&seed, c->shift, input);
        reverb_process(&r, input, &samples[tick * 2]);
    }
}

static void run_unit(const model_unit_case *c)
{
    uint32_t seed = 1;
    reverb_unit_init(&u);
    set_up(&u.reverb, c->mode, c->delay, c->feedback);
    u.depth[0] = (int16_t)c->depth_left;
    u.depth[1] = (int16_t)c->depth_right;
    for (int tick = 0; tick < MODEL_UNIT_TICKS; tick++)
    {
        if (tick == MODEL_UNIT_TICKS / 2)
        {
            change_halfway(&u.reverb, c->later_mode, c->later_delay);
        }
        int16_t input[2];
        next_noise(&seed, c->shift, input);
        reverb_unit_process(&u, input, &samples[tick * 2]);
    }
}

// Writes the C's output where the test runs, for reverb_model.py --compare to find the first sample that differs.
static void save(const char *kind, int index, int ticks)
{
    char path[64];
    snprintf(path, sizeof(path), "model_%scase_%d.raw", kind, index);
    FILE *file = fopen(path, "wb");
    if (file)
    {
        fwrite(samples, sizeof(samples[0]) * 2, (size_t)ticks, file);
        fclose(file);
    }
    fprintf(stderr, "model %scase %d differs; the C's output is in %s. Find where with\n", kind, index, path);
    fprintf(stderr, "    python3 tests/reverb_model.py --compare %s%d <that file>\n", *kind ? "unit " : "", index);
}

void model_tests(void)
{
    for (int index = 0; index < (int)(sizeof(model_cases) / sizeof(model_cases[0])); index++)
    {
        run(&model_cases[index]);
        const bool matches = fingerprint(samples, MODEL_TICKS * 2) == model_cases[index].fingerprint;
        CHECK(matches);
        if (!matches)
        {
            save("", index, MODEL_TICKS);
        }
    }

    for (int index = 0; index < (int)(sizeof(model_unit_cases) / sizeof(model_unit_cases[0])); index++)
    {
        run_unit(&model_unit_cases[index]);
        const bool matches = fingerprint(samples, MODEL_UNIT_TICKS * 2) == model_unit_cases[index].fingerprint;
        CHECK(matches);
        if (!matches)
        {
            save("unit_", index, MODEL_UNIT_TICKS);
        }
    }
}
