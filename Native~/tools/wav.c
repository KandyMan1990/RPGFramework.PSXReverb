#include "wav.h"

#include "numeric.h"

#include <string.h>

enum
{
    FORMAT_PCM = 1,
    FORMAT_FLOAT = 3,
    FORMAT_EXTENSIBLE = 0xFFFE
};

static uint16_t read16(const uint8_t *bytes)
{
    const uint16_t value = (uint16_t)(bytes[0] | bytes[1] << 8);
    return value;
}

static uint32_t read32(const uint8_t *bytes)
{
    const uint32_t value =
        (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
    return value;
}

static void write16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value & 0xFF);
    bytes[1] = (uint8_t)(value >> 8);
}

static void write32(uint8_t *bytes, uint32_t value)
{
    for (int i = 0; i < 4; i++)
    {
        bytes[i] = (uint8_t)((value >> (8 * i)) & 0xFF);
    }
}

// Chunks are padded to an even length.
static bool skip(FILE *file, uint32_t size)
{
    const bool skipped = fseek(file, (long)size + (long)(size & 1), SEEK_CUR) == 0;
    return skipped;
}

static bool fail(wav_reader *reader, char *error, size_t error_size, const char *path, const char *why)
{
    snprintf(error, error_size, "%s: %s", path, why);
    wav_close(reader);
    return false;
}

bool wav_open(wav_reader *reader, const char *path, char *error, size_t error_size)
{
    memset(reader, 0, sizeof(*reader));
    reader->file = fopen(path, "rb");
    if (!reader->file)
    {
        return fail(reader, error, error_size, path, "cannot be opened");
    }

    uint8_t riff[12];
    if (fread(riff, 1, sizeof(riff), reader->file) != sizeof(riff) || memcmp(riff, "RIFF", 4) != 0 ||
        memcmp(riff + 8, "WAVE", 4) != 0)
    {
        return fail(reader, error, error_size, path, "is not a WAV file");
    }

    bool have_format = false;
    for (;;)
    {
        uint8_t header[8];
        if (fread(header, 1, sizeof(header), reader->file) != sizeof(header))
        {
            return fail(reader, error, error_size, path, "has no samples");
        }
        const uint32_t size = read32(header + 4);

        if (memcmp(header, "fmt ", 4) == 0)
        {
            uint8_t format[40] = {0};
            const uint32_t wanted = size < sizeof(format) ? size : (uint32_t)sizeof(format);
            if (size < 16 || fread(format, 1, wanted, reader->file) != wanted || !skip(reader->file, size - wanted))
            {
                return fail(reader, error, error_size, path, "has a broken format chunk");
            }
            uint16_t tag = read16(format);
            // An extensible format names its real one in the first two bytes of its sub-format.
            if (tag == FORMAT_EXTENSIBLE && size >= 26)
            {
                tag = read16(format + 24);
            }
            reader->channels = read16(format + 2);
            reader->sample_rate = read32(format + 4);
            reader->block_align = read16(format + 12);
            reader->bits = read16(format + 14);
            reader->is_float = tag == FORMAT_FLOAT;

            const bool integer = tag == FORMAT_PCM &&
                                 (reader->bits == 8 || reader->bits == 16 || reader->bits == 24 || reader->bits == 32);
            const bool floating = tag == FORMAT_FLOAT && reader->bits == 32;
            if (!integer && !floating)
            {
                return fail(reader, error, error_size, path,
                            "is not 8, 16, 24 or 32-bit integer or 32-bit float samples");
            }
            if (reader->channels == 0 || reader->block_align != reader->channels * (reader->bits / 8))
            {
                return fail(reader, error, error_size, path, "has a broken format chunk");
            }
            have_format = true;
        }
        else if (memcmp(header, "data", 4) == 0)
        {
            if (!have_format)
            {
                return fail(reader, error, error_size, path, "has samples before its format");
            }
            reader->frames = size / reader->block_align;
            reader->frames_left = reader->frames;
            return true;
        }
        else if (!skip(reader->file, size))
        {
            return fail(reader, error, error_size, path, "has no samples");
        }
    }
}

static int16_t decode(const uint8_t *bytes, uint16_t bits, bool is_float)
{
    int32_t value;
    if (is_float)
    {
        const uint32_t raw = read32(bytes);
        float sample;
        memcpy(&sample, &raw, sizeof(sample));
        value = float_to16(sample);
    }
    else if (bits == 8)
    {
        value = ((int32_t)bytes[0] - 128) * 256;
    }
    else if (bits == 16)
    {
        value = read16(bytes);
        value -= value >= 0x8000 ? 0x10000 : 0;
    }
    else if (bits == 24)
    {
        value = (int32_t)(bytes[0] | bytes[1] << 8 | bytes[2] << 16);
        value -= value >= 0x800000 ? 0x1000000 : 0;
        value >>= 8;
    }
    else
    {
        const uint32_t raw = read32(bytes);
        value = (int32_t)(raw >> 16);
        value -= value >= 0x8000 ? 0x10000 : 0;
    }
    const int16_t sample = (int16_t)value;
    return sample;
}

size_t wav_read_stereo(wav_reader *reader, int16_t *frames, size_t count)
{
    static uint8_t buffer[64 * 1024];
    const size_t per_read = sizeof(buffer) / reader->block_align;
    size_t done = 0;
    while (done < count && reader->frames_left > 0)
    {
        size_t wanted = count - done;
        wanted = wanted < per_read ? wanted : per_read;
        wanted = wanted < reader->frames_left ? wanted : reader->frames_left;
        const size_t got = fread(buffer, reader->block_align, wanted, reader->file);
        const size_t bytes_per_sample = reader->bits / 8u;
        for (size_t i = 0; i < got; i++)
        {
            const uint8_t *frame = buffer + i * reader->block_align;
            const uint8_t *right = reader->channels > 1 ? frame + bytes_per_sample : frame;
            frames[(done + i) * 2] = decode(frame, reader->bits, reader->is_float);
            frames[(done + i) * 2 + 1] = decode(right, reader->bits, reader->is_float);
        }
        done += got;
        reader->frames_left -= (uint32_t)got;
        if (got < wanted)
        {
            reader->frames_left = 0;
        }
    }
    return done;
}

void wav_close(wav_reader *reader)
{
    if (reader->file)
    {
        fclose(reader->file);
        reader->file = NULL;
    }
}

bool wav_write_header(FILE *file, uint32_t sample_rate, uint32_t frames)
{
    uint8_t header[44];
    memcpy(header, "RIFF", 4);
    write32(header + 4, 36 + frames * 4);
    memcpy(header + 8, "WAVEfmt ", 8);
    write32(header + 16, 16);
    write16(header + 20, FORMAT_PCM);
    write16(header + 22, 2);
    write32(header + 24, sample_rate);
    write32(header + 28, sample_rate * 4);
    write16(header + 32, 4);
    write16(header + 34, 16);
    memcpy(header + 36, "data", 4);
    write32(header + 40, frames * 4);
    const bool written = fwrite(header, 1, sizeof(header), file) == sizeof(header);
    return written;
}

bool wav_write_stereo(FILE *file, const int16_t *frames, size_t count)
{
    static uint8_t buffer[16 * 1024];
    size_t done = 0;
    while (done < count)
    {
        const size_t batch = count - done < sizeof(buffer) / 4 ? count - done : sizeof(buffer) / 4;
        for (size_t i = 0; i < batch * 2; i++)
        {
            write16(buffer + i * 2, (uint16_t)frames[done * 2 + i]);
        }
        if (fwrite(buffer, 4, batch, file) != batch)
        {
            return false;
        }
        done += batch;
    }
    return true;
}
