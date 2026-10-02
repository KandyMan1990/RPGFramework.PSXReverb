#include "check.h"
#include "model_fingerprints.h"
#include "reverb.h"
#include "suites.h"

#include <stdio.h>

static reverb r;
static int16_t samples[MODEL_TICKS * 2];

static uint64_t fingerprint(const int16_t *values, size_t count)
{
    uint64_t hash = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < count; i++)
    {
        uint16_t bits = (uint16_t)values[i];
        hash = (hash ^ (bits & 0xFFu)) * 0x100000001B3ull;
        hash = (hash ^ (bits >> 8)) * 0x100000001B3ull;
    }
    return hash;
}

static void run(const model_case *c)
{
    reverb_set_mode(&r, c->mode);
    if (c->delay >= 0)
    {
        reverb_set_delay(&r, c->delay);
    }
    if (c->feedback >= 0)
    {
        reverb_set_feedback(&r, c->feedback);
    }

    uint32_t seed = 1;
    for (int tick = 0; tick < MODEL_TICKS; tick++)
    {
        if (tick == MODEL_TICKS / 2)
        {
            if (c->later_mode >= 0)
            {
                reverb_set_mode(&r, c->later_mode);
            }
            if (c->later_delay >= 0)
            {
                reverb_set_delay(&r, c->later_delay);
            }
        }
        seed = seed * 1664525u + 1013904223u;
        const int16_t input[2] = {(int16_t)(((int32_t)(seed >> 16) - 0x8000) >> c->shift),
                                  (int16_t)(((int32_t)((seed >> 8) & 0xFFFF) - 0x8000) >> c->shift)};
        reverb_process(&r, input, &samples[tick * 2]);
    }
}

// Writes the C's output where the test runs, for reverb_model.py --compare to find the first sample that differs.
static void save(int index)
{
    char path[64];
    snprintf(path, sizeof(path), "model_case_%d.raw", index);
    FILE *file = fopen(path, "wb");
    if (file)
    {
        fwrite(samples, sizeof(samples), 1, file);
        fclose(file);
    }
    fprintf(stderr, "model case %d differs; the C's output is in %s. Find where with\n", index, path);
    fprintf(stderr, "    python3 tests/reverb_model.py --compare %d <that file>\n", index);
}

void model_tests(void)
{
    for (int index = 0; index < (int)(sizeof(model_cases) / sizeof(model_cases[0])); index++)
    {
        run(&model_cases[index]);
        int matches = fingerprint(samples, MODEL_TICKS * 2) == model_cases[index].fingerprint;
        CHECK(matches);
        if (!matches)
        {
            save(index);
        }
    }
}
