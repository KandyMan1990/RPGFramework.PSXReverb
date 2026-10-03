#include "check.h"

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
