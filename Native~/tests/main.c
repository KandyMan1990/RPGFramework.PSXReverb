#include "check.h"
#include "psx_reverb.h"
#include "suites.h"

#include <stdio.h>

static int failures = 0;

void check_true(int passed, const char *expression, const char *file, int line)
{
    if (!passed)
    {
        fprintf(stderr, "%s:%d: CHECK(%s) failed\n", file, line, expression);
        failures++;
    }
}

void check_equal(long long expected, long long actual, const char *expression, const char *file, int line)
{
    if (expected != actual)
    {
        fprintf(stderr, "%s:%d: %s was %lld, expected %lld\n", file, line, expression, actual, expected);
        failures++;
    }
}

int check_failures(void)
{
    return failures;
}

static void version_packs_major_minor_and_patch(void)
{
    uint32_t version = psx_reverb_version();
    CHECK_EQ(PSX_REVERB_VERSION_MAJOR, version >> 16);
    CHECK_EQ(PSX_REVERB_VERSION_MINOR, (version >> 8) & 0xFF);
    CHECK_EQ(PSX_REVERB_VERSION_PATCH, version & 0xFF);
}

int main(void)
{
    version_packs_major_minor_and_patch();
    reverb_tests();
    unit_tests();
    wav_tests();
    resampler_tests();
    host_tests();
    api_tests();
    model_tests();

    int result = check_failures() == 0 ? 0 : 1;
    printf("%s\n", result == 0 ? "All checks passed" : "Checks failed");
    return result;
}
