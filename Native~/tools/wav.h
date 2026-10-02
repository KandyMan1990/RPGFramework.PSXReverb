#ifndef PSX_REVERB_WAV_H
#define PSX_REVERB_WAV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct wav_reader
{
    FILE *file;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits;
    uint16_t block_align;
    bool is_float;
    uint32_t frames;
    uint32_t frames_left;
} wav_reader;

// Opens a WAV of 8, 16, 24 or 32-bit integer or 32-bit float samples, with any number of channels, and leaves it ready
// to read its samples. On failure it says why in error.
bool wav_open(wav_reader *reader, const char *path, char *error, size_t error_size);

// Reads up to count frames as 16-bit stereo: mono feeds both sides, and more than two channels give their first two.
// Wider samples lose their low bits; float is scaled by 8000h and saturates. Returns how many frames it read.
size_t wav_read_stereo(wav_reader *reader, int16_t *frames, size_t count);

void wav_close(wav_reader *reader);

// A 16-bit stereo WAV: the header for the given number of frames, then the frames themselves.
bool wav_write_header(FILE *file, uint32_t sample_rate, uint32_t frames);
bool wav_write_stereo(FILE *file, const int16_t *frames, size_t count);

#endif
