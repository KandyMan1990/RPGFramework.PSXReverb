#include "host.h"
#include "wav.h"

#include <stdlib.h>
#include <string.h>

static const char *const mode_names[REVERB_MODE_COUNT] = {"off",  "room",  "studio-a", "studio-b", "studio-c",
                                                          "hall", "space", "echo",     "delay",    "pipe"};

static const char usage[] =
    "Usage: psxreverb_render [options] INPUT.wav OUTPUT.wav\n"
    "\n"
    "Runs a WAV through the console's reverb unit and writes it out as 16-bit stereo at the same rate.\n"
    "\n"
    "  --mode NAME       off, room, studio-a, studio-b, studio-c, hall, space, echo, delay or pipe (studio-c)\n"
    "  --depth N         the reverb's output volume, 0-127 as the console's tools have it (64)\n"
    "  --delay N         echo and delay only: delay time, 1-127 (127)\n"
    "  --feedback N      echo and delay only: feedback, 0-127 (127 for echo, 0 for delay)\n"
    "  --tail SECONDS    silence added after the input so the reverb can ring out (4)\n"
    "  --wet             the reverb alone, rather than mixed with the input as the console's mixer does\n"
    "\n"
    "Any of the usual rates from 8 to 192 kHz. At 44.1 kHz the reverb runs as the console's does, sample for\n"
    "sample; at others it is resampled to 44.1 kHz and back, flat across its band, and arrives a little later.\n";

static bool parse_int(const char *text, int low, int high, int *value)
{
    char *end;
    long parsed = strtol(text, &end, 10);
    bool valid = *text != '\0' && *end == '\0' && parsed >= low && parsed <= high;
    if (valid)
    {
        *value = (int)parsed;
    }
    return valid;
}

static int find_mode(const char *name)
{
    for (int mode = 0; mode < REVERB_MODE_COUNT; mode++)
    {
        if (strcmp(name, mode_names[mode]) == 0)
        {
            return mode;
        }
    }
    return -1;
}

static int16_t saturate16(int32_t value)
{
    int16_t saturated = (int16_t)(value < INT16_MIN ? INT16_MIN : value > INT16_MAX ? INT16_MAX : value);
    return saturated;
}

int main(int argc, char **argv)
{
    int mode = REVERB_MODE_STUDIO_C;
    int depth = 64;
    int delay = -1;
    int feedback = -1;
    int tail_seconds = 4;
    bool wet_only = false;
    const char *paths[2] = {NULL, NULL};
    int path_count = 0;

    for (int i = 1; i < argc; i++)
    {
        const char *argument = argv[i];
        const char *next = i + 1 < argc ? argv[i + 1] : "";
        bool valid = true;
        bool takes_value = true;
        if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0)
        {
            fputs(usage, stdout);
            return 0;
        }
        else if (strcmp(argument, "--mode") == 0)
        {
            mode = find_mode(next);
            valid = mode >= 0;
            i++;
        }
        else if (strcmp(argument, "--depth") == 0)
        {
            valid = parse_int(next, 0, 127, &depth);
            i++;
        }
        else if (strcmp(argument, "--delay") == 0)
        {
            valid = parse_int(next, 1, 127, &delay);
            i++;
        }
        else if (strcmp(argument, "--feedback") == 0)
        {
            valid = parse_int(next, 0, 127, &feedback);
            i++;
        }
        else if (strcmp(argument, "--tail") == 0)
        {
            valid = parse_int(next, 0, 600, &tail_seconds);
            i++;
        }
        else
        {
            takes_value = false;
            if (strcmp(argument, "--wet") == 0)
            {
                wet_only = true;
            }
            else if (argument[0] != '-' && path_count < 2)
            {
                paths[path_count++] = argument;
            }
            else
            {
                valid = false;
            }
        }

        if (!valid)
        {
            fprintf(stderr, "psxreverb_render: %s%s%s is not understood\n\n%s", argument, takes_value ? " " : "",
                    takes_value ? next : "", usage);
            return 2;
        }
    }
    if (path_count != 2)
    {
        fputs(usage, stderr);
        return 2;
    }
    if ((delay >= 0 || feedback >= 0) && mode != REVERB_MODE_ECHO && mode != REVERB_MODE_DELAY)
    {
        fprintf(stderr, "psxreverb_render: --delay and --feedback only apply to echo and delay\n");
        return 2;
    }

    wav_reader reader;
    char error[512];
    if (!wav_open(&reader, paths[0], error, sizeof(error)))
    {
        fprintf(stderr, "psxreverb_render: %s\n", error);
        return 1;
    }
    static host_reverb host;
    uint32_t rate = reader.sample_rate;
    if (!host_reverb_init(&host, rate))
    {
        fprintf(stderr, "psxreverb_render: %s is %u Hz, which the reverb cannot be resampled to\n", paths[0],
                (unsigned)rate);
        wav_close(&reader);
        return 1;
    }

    uint64_t total = (uint64_t)reader.frames + (uint64_t)tail_seconds * rate;
    FILE *output = 36 + total * 4 > UINT32_MAX ? NULL : fopen(paths[1], "wb");
    if (!output || !wav_write_header(output, rate, (uint32_t)total))
    {
        fprintf(stderr, "psxreverb_render: %s cannot be written%s\n", paths[1],
                36 + total * 4 > UINT32_MAX ? ": it would be larger than a WAV can hold" : "");
        wav_close(&reader);
        host_reverb_free(&host);
        if (output)
        {
            fclose(output);
        }
        return 1;
    }

    reverb_set_mode(&host.unit.reverb, mode);
    if (delay >= 0)
    {
        reverb_set_delay(&host.unit.reverb, delay);
    }
    if (feedback >= 0)
    {
        reverb_set_feedback(&host.unit.reverb, feedback);
    }
    // The console's tools give depth as 0-127, which reaches the register shifted up by 8.
    host.unit.depth[0] = (int16_t)(depth << 8);
    host.unit.depth[1] = (int16_t)(depth << 8);

    enum
    {
        BLOCK = 4096
    };
    static int16_t block[BLOCK * 2];
    static float wet[BLOCK * 2];
    uint64_t clipped = 0;
    bool written = true;
    for (uint64_t done = 0; done < total && written;)
    {
        size_t count = total - done < BLOCK ? (size_t)(total - done) : BLOCK;
        // Past the end of the input, or of a file shorter than its header says, is silence.
        size_t read = wav_read_stereo(&reader, block, count);
        memset(block + read * 2, 0, (count - read) * 2 * sizeof(block[0]));
        for (size_t i = 0; i < count * 2; i++)
        {
            wet[i] = (float)block[i] / 32768.0f;
        }
        host_reverb_process(&host, wet, wet, count);
        // At 44.1 kHz the reverb's float is its 16-bit sample exactly, so this mix is the console's.
        for (size_t i = 0; i < count * 2; i++)
        {
            float scaled = wet[i] * 32768.0f;
            int32_t reverb_sample = (int32_t)(scaled + (scaled < 0 ? -0.5f : 0.5f));
            int32_t mixed = wet_only ? reverb_sample : block[i] + reverb_sample;
            if (mixed < INT16_MIN || mixed > INT16_MAX)
            {
                clipped++;
            }
            block[i] = saturate16(mixed);
        }
        written = wav_write_stereo(output, block, count);
        done += count;
    }

    double added = host_reverb_added_latency(&host);
    wav_close(&reader);
    host_reverb_free(&host);
    if (fclose(output) != 0 || !written)
    {
        fprintf(stderr, "psxreverb_render: %s could not be written in full\n", paths[1]);
        return 1;
    }

    printf("%s, depth %d: %.1f s in, %.1f s out -> %s\n", mode_names[mode], depth,
           (double)reader.frames / (double)rate, (double)total / (double)rate, paths[1]);
    if (rate != UNIT_RATE)
    {
        printf("Resampled from %u Hz, which brings the reverb %.2f ms later than the console would.\n",
               (unsigned)rate, 1000.0 * added / (double)rate);
    }
    if (clipped > 0)
    {
        printf("%llu samples clipped in the mix; lower the input or the depth, or use --wet\n",
               (unsigned long long)clipped);
    }
    return 0;
}
