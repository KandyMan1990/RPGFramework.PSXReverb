#ifndef CHECK_H
#define CHECK_H

#ifdef __cplusplus
extern "C"
{
#endif

#define CHECK(condition) check_true((condition) != 0, #condition, __FILE__, __LINE__)
#define CHECK_EQ(expected, actual) check_equal((long long)(expected), (long long)(actual), #actual, __FILE__, __LINE__)

void check_true(int passed, const char *expression, const char *file, int line);
void check_equal(long long expected, long long actual, const char *expression, const char *file, int line);
int check_failures(void);

#ifdef __cplusplus
}
#endif

#endif
