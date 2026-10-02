#include "check.h"
#include "suites.h"
#include "wav.h"

#include <math.h>
#include <string.h>

// WAV files built byte by byte, written where the tests run.
typedef struct builder
{
    uint8_t bytes[512];
    size_t size;
} builder;

static void put(builder *b, const void *data, size_t size)
{
    memcpy(b->bytes + b->size, data, size);
    b->size += size;
}

static void put16(builder *b, uint32_t value)
{
    const uint8_t bytes[2] = {(uint8_t)(value & 0xFF), (uint8_t)((value >> 8) & 0xFF)};
    put(b, bytes, sizeof(bytes));
}

static void put32(builder *b, uint32_t value)
{
    put16(b, value & 0xFFFF);
    put16(b, value >> 16);
}

static void begin(builder *b)
{
    b->size = 0;
    put(b, "RIFF", 4);
    put32(b, 0);
    put(b, "WAVE", 4);
}

static void format(builder *b, uint32_t tag, uint32_t channels, uint32_t bits)
{
    put(b, "fmt ", 4);
    put32(b, 16);
    put16(b, tag);
    put16(b, channels);
    put32(b, 44100);
    put32(b, 44100 * channels * bits / 8);
    put16(b, channels * bits / 8);
    put16(b, bits);
}

static void chunk(builder *b, const char *id, const void *data, uint32_t size)
{
    put(b, id, 4);
    put32(b, size);
    put(b, data, size);
    if (size & 1)
    {
        put(b, "", 1);
    }
}

static void save(builder *b, const char *path)
{
    uint32_t riff_size = (uint32_t)b->size - 8;
    const uint8_t size[4] = {(uint8_t)(riff_size & 0xFF), (uint8_t)((riff_size >> 8) & 0xFF),
                             (uint8_t)((riff_size >> 16) & 0xFF), (uint8_t)(riff_size >> 24)};
    memcpy(b->bytes + 4, size, 4);
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file)
    {
        fwrite(b->bytes, 1, b->size, file);
        fclose(file);
    }
}

// Opens a file and reads every frame it has, up to eight.
static size_t read_all(const char *path, int16_t frames[16], wav_reader *reader)
{
    char error[256];
    bool opened = wav_open(reader, path, error, sizeof(error));
    CHECK(opened);
    size_t count = opened ? wav_read_stereo(reader, frames, 8) : 0;
    wav_close(reader);
    return count;
}

static void sixteen_bit_stereo_round_trips(void)
{
    const int16_t written[6] = {0, -1, INT16_MAX, INT16_MIN, 1234, -4321};
    FILE *file = fopen("wav_round_trip.wav", "wb");
    CHECK(file != NULL);
    if (!file)
    {
        return;
    }
    CHECK(wav_write_header(file, 44100, 3));
    CHECK(wav_write_stereo(file, written, 3));
    fclose(file);

    wav_reader reader;
    int16_t frames[16];
    CHECK_EQ(3, read_all("wav_round_trip.wav", frames, &reader));
    CHECK_EQ(44100, reader.sample_rate);
    CHECK_EQ(2, reader.channels);
    CHECK_EQ(16, reader.bits);
    CHECK(memcmp(frames, written, sizeof(written)) == 0);
}

// Wider samples keep their top 16 bits, rounding down: 180h is 1, -80h is -1.
static void mono_24_bit_feeds_both_sides(void)
{
    const uint8_t data[] = {0xFF, 0xFF, 0x7F, 0x00, 0x00, 0x80, 0x80, 0x01, 0x00, 0x80, 0xFF, 0xFF};
    builder b;
    begin(&b);
    format(&b, 1, 1, 24);
    chunk(&b, "data", data, sizeof(data));
    save(&b, "wav_mono_24.wav");

    wav_reader reader;
    int16_t frames[16];
    CHECK_EQ(4, read_all("wav_mono_24.wav", frames, &reader));
    const int16_t expected[8] = {INT16_MAX, INT16_MAX, INT16_MIN, INT16_MIN, 1, 1, -1, -1};
    for (int i = 0; i < 8; i++)
    {
        CHECK_EQ(expected[i], frames[i]);
    }
}

static void float_scales_and_saturates(void)
{
    const float samples[6] = {0.5f, -1.0f, 1.0f, -0.25f, 2.0f, NAN};
    uint8_t data[sizeof(samples)];
    for (int i = 0; i < 6; i++)
    {
        uint32_t raw;
        memcpy(&raw, &samples[i], sizeof(raw));
        for (int byte = 0; byte < 4; byte++)
        {
            data[i * 4 + byte] = (uint8_t)((raw >> (8 * byte)) & 0xFF);
        }
    }
    builder b;
    begin(&b);
    format(&b, 3, 2, 32);
    chunk(&b, "data", data, sizeof(data));
    save(&b, "wav_float.wav");

    wav_reader reader;
    int16_t frames[16];
    CHECK_EQ(3, read_all("wav_float.wav", frames, &reader));
    const int16_t expected[6] = {16384, INT16_MIN, INT16_MAX, -8192, INT16_MAX, 0};
    for (int i = 0; i < 6; i++)
    {
        CHECK_EQ(expected[i], frames[i]);
    }
}

static void eight_and_32_bit_integers(void)
{
    const uint8_t eight[] = {0x80, 0xFF, 0x00, 0x81};
    builder b;
    begin(&b);
    format(&b, 1, 2, 8);
    chunk(&b, "data", eight, sizeof(eight));
    save(&b, "wav_8.wav");
    wav_reader reader;
    int16_t frames[16];
    CHECK_EQ(2, read_all("wav_8.wav", frames, &reader));
    CHECK_EQ(0, frames[0]);
    CHECK_EQ(127 * 256, frames[1]);
    CHECK_EQ(INT16_MIN, frames[2]);
    CHECK_EQ(256, frames[3]);

    const uint8_t wide[] = {0xFF, 0xFF, 0xFF, 0x7F, 0x00, 0x00, 0x00, 0x80};
    begin(&b);
    format(&b, 1, 2, 32);
    chunk(&b, "data", wide, sizeof(wide));
    save(&b, "wav_32.wav");
    CHECK_EQ(1, read_all("wav_32.wav", frames, &reader));
    CHECK_EQ(INT16_MAX, frames[0]);
    CHECK_EQ(INT16_MIN, frames[1]);
}

// An extensible header naming PCM in its sub-format, and a chunk of odd length padded before the samples.
static void extensible_format_and_unknown_chunks(void)
{
    static const uint8_t pcm_guid[16] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                                         0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
    builder b;
    begin(&b);
    put(&b, "fmt ", 4);
    put32(&b, 40);
    put16(&b, 0xFFFE);
    put16(&b, 2);
    put32(&b, 44100);
    put32(&b, 44100 * 4);
    put16(&b, 4);
    put16(&b, 16);
    put16(&b, 22);
    put16(&b, 16);
    put32(&b, 3);
    put(&b, pcm_guid, sizeof(pcm_guid));
    chunk(&b, "LIST", "abc", 3);
    const uint8_t data[] = {0x01, 0x00, 0xFF, 0xFF};
    chunk(&b, "data", data, sizeof(data));
    save(&b, "wav_extensible.wav");

    wav_reader reader;
    int16_t frames[16];
    CHECK_EQ(1, read_all("wav_extensible.wav", frames, &reader));
    CHECK_EQ(1, frames[0]);
    CHECK_EQ(-1, frames[1]);
}

static void more_than_two_channels_give_the_first_two(void)
{
    const uint8_t data[] = {0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04, 0x00};
    builder b;
    begin(&b);
    format(&b, 1, 4, 16);
    chunk(&b, "data", data, sizeof(data));
    save(&b, "wav_quad.wav");

    wav_reader reader;
    int16_t frames[16];
    CHECK_EQ(1, read_all("wav_quad.wav", frames, &reader));
    CHECK_EQ(1, frames[0]);
    CHECK_EQ(2, frames[1]);
}

static void unsupported_files_are_refused(void)
{
    wav_reader reader;
    char error[256];
    CHECK(!wav_open(&reader, "wav_does_not_exist.wav", error, sizeof(error)));

    builder b;
    begin(&b);
    format(&b, 2, 2, 4);
    chunk(&b, "data", "\0\0\0\0", 4);
    save(&b, "wav_adpcm.wav");
    CHECK(!wav_open(&reader, "wav_adpcm.wav", error, sizeof(error)));
    CHECK(strstr(error, "is not 8, 16, 24 or 32-bit") != NULL);

    begin(&b);
    memcpy(b.bytes, "RIFX", 4);
    save(&b, "wav_not_riff.wav");
    CHECK(!wav_open(&reader, "wav_not_riff.wav", error, sizeof(error)));
}

void wav_tests(void)
{
    sixteen_bit_stereo_round_trips();
    mono_24_bit_feeds_both_sides();
    float_scales_and_saturates();
    eight_and_32_bit_integers();
    extensible_format_and_unknown_chunks();
    more_than_two_channels_give_the_first_two();
    unsupported_files_are_refused();
}
