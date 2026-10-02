#include "unit.h"
#include "wav.h"

#include <stdlib.h>
#include <string.h>

static const char *const mode_names[REVERB_MODE_COUNT] = {"off",  "room",  "studio-a", "studio-b", "studio-c",
                                                          "hall", "space", "echo",     "delay",    "pipe"};

static const char usage[] =
    "Usage: psxreverb_render [options] INPUT.wav OUTPUT.wav\n"
    "\n"
    "Runs a WAV through the console's reverb unit and writes it out as 16-bit stereo at 44.1 kHz.\n"
    "\n"
    "  --mode NAME       off, room, studio-a, studio-b, studio-c, hall, space, echo, delay or pipe (hall)\n"
    "  --depth N         the reverb's output volume, 0-127 as the console's tools have it (64)\n"
    "  --delay N         echo and delay only: delay time, 1-127 (127)\n"
    "  --feedback N      echo and delay only: feedback, 0-127 (127 for echo, 0 for delay)\n"
    "  --tail SECONDS    silence added after the input so the reverb can ring out (4)\n"
    "  --wet             the reverb alone, rather than mixed with the input as the console's mixer does\n"
    "\n"
    "The input must be 44.1 kHz until other rates are supported. macOS converts with\n"
    "  afconvert -f WAVE -d LEI16@44100 INPUT.wav CONVERTED.wav\n";

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
    int mode = REVERB_MODE_HALL;
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
    if (reader.sample_rate != UNIT_RATE)
    {
        fprintf(stderr,
                "psxreverb_render: %s is %u Hz; the reverb runs at 44,100 Hz until other rates are supported.\n",
                paths[0], (unsigned)reader.sample_rate);
        fprintf(stderr, "macOS converts with: afconvert -f WAVE -d LEI16@44100 %s CONVERTED.wav\n", paths[0]);
        wav_close(&reader);
        return 1;
    }

    uint64_t total = (uint64_t)reader.frames + (uint64_t)tail_seconds * UNIT_RATE;
    if (36 + total * 4 > UINT32_MAX)
    {
        fprintf(stderr, "psxreverb_render: the output would be larger than a WAV can hold\n");
        wav_close(&reader);
        return 1;
    }

    FILE *output = fopen(paths[1], "wb");
    if (!output || !wav_write_header(output, UNIT_RATE, (uint32_t)total))
    {
        fprintf(stderr, "psxreverb_render: %s cannot be written\n", paths[1]);
        wav_close(&reader);
        if (output)
        {
            fclose(output);
        }
        return 1;
    }

    static reverb_unit unit;
    reverb_unit_init(&unit);
    reverb_set_mode(&unit.reverb, mode);
    if (delay >= 0)
    {
        reverb_set_delay(&unit.reverb, delay);
    }
    if (feedback >= 0)
    {
        reverb_set_feedback(&unit.reverb, feedback);
    }
    // The console's tools give depth as 0-127, which reaches the register shifted up by 8.
    unit.depth[0] = (int16_t)(depth << 8);
    unit.depth[1] = (int16_t)(depth << 8);

    enum
    {
        BLOCK = 4096
    };
    static int16_t block[BLOCK * 2];
    uint64_t clipped = 0;
    bool written = true;
    for (uint64_t done = 0; done < total && written;)
    {
        size_t count = total - done < BLOCK ? (size_t)(total - done) : BLOCK;
        // Past the end of the input, or of a file shorter than its header says, is silence.
        size_t read = wav_read_stereo(&reader, block, count);
        memset(block + read * 2, 0, (count - read) * 2 * sizeof(block[0]));
        for (size_t i = 0; i < count; i++)
        {
            int16_t *frame = block + i * 2;
            int16_t wet[2];
            reverb_unit_process(&unit, frame, wet);
            for (int side = 0; side < 2; side++)
            {
                int32_t mixed = wet_only ? wet[side] : frame[side] + wet[side];
                if (mixed < INT16_MIN || mixed > INT16_MAX)
                {
                    clipped++;
                }
                frame[side] = saturate16(mixed);
            }
        }
        written = wav_write_stereo(output, block, count);
        done += count;
    }

    wav_close(&reader);
    if (fclose(output) != 0 || !written)
    {
        fprintf(stderr, "psxreverb_render: %s could not be written in full\n", paths[1]);
        return 1;
    }

    printf("%s, depth %d: %.1f s in, %.1f s out -> %s\n", mode_names[mode], depth,
           (double)reader.frames / (double)UNIT_RATE, (double)total / (double)UNIT_RATE, paths[1]);
    if (clipped > 0)
    {
        printf("%llu samples clipped in the mix; lower the input or the depth, or use --wet\n",
               (unsigned long long)clipped);
    }
    return 0;
}
