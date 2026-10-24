/*
 * Streaming MP3 to WAV output (GPL-2.0-or-later).
 * Based on the optional WAV feature in the Magentron/dmikushin fork.
 * Uses libmad's fixed-point sample conversion; preserves partial frames
 * between writes and keeps independent decoder state for each output file.
 */
#include "ripstream_wav.h"
#include <mad.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#ifndef WIN32
#include <unistd.h>
#endif

struct wav_output {
    FHANDLE file;
    AudioFileAllocation allocation;
    struct mad_stream stream;
    struct mad_frame frame;
    struct mad_synth synth;
    GByteArray *pending;
    unsigned int channels;
    unsigned int rate;
    uint32_t bytes;
    error_code error;
};

static void put16(unsigned char *p, unsigned int value)
{
    p[0] = value;
    p[1] = value >> 8;
}

static void put32(unsigned char *p, uint32_t value)
{
    put16(p, value);
    put16(p + 2, value >> 16);
}

static error_code write_all(WavOutput *out, const unsigned char *data, size_t size)
{
    FHANDLE file = out->file;
    audio_file_reserve(file, &out->allocation, size);
    while (size) {
#ifdef WIN32
        DWORD count;
        if (!WriteFile(file, data, (DWORD)size, &count, NULL) || !count)
            return SR_ERROR_CANT_WRITE_TO_FILE;
#else
        ssize_t count = write(file, data, size);
        if (count <= 0) return SR_ERROR_CANT_WRITE_TO_FILE;
#endif
        data += count;
        size -= count;
    }
    return SR_SUCCESS;
}

static error_code write_header(WavOutput *out)
{
    unsigned char h[44] = {0};
    unsigned int channels = out->channels ? out->channels : 2;
    unsigned int rate = out->rate ? out->rate : 44100;
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36 + out->bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put16(h + 20, 1);
    put16(h + 22, channels);
    put32(h + 24, rate);
    put32(h + 28, rate * channels * 2);
    put16(h + 32, channels * 2);
    put16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    put32(h + 40, out->bytes);
#ifdef WIN32
    {
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        if (!SetFilePointerEx(out->file, zero, NULL, FILE_BEGIN))
            return SR_ERROR_CANT_WRITE_TO_FILE;
    }
#else
    if (lseek(out->file, 0, SEEK_SET) < 0) return SR_ERROR_CANT_WRITE_TO_FILE;
#endif
    return write_all(out, h, sizeof(h));
}

/* Round and clip libmad's fixed-point samples to signed 16-bit PCM. */
static int scale(mad_fixed_t sample)
{
    sample += 1L << (MAD_F_FRACBITS - 16);
    if (sample >= MAD_F_ONE) sample = MAD_F_ONE - 1;
    else if (sample < -MAD_F_ONE) sample = -MAD_F_ONE;
    return sample >> (MAD_F_FRACBITS + 1 - 16);
}

static error_code decode(WavOutput *out)
{
    const unsigned char *remaining = NULL;
    unsigned int consumed;
    mad_stream_buffer(&out->stream, out->pending->data, out->pending->len);
    while (1) {
        unsigned char pcm[1152 * 2 * 2];
        unsigned int i, channel, length;
        if (mad_frame_decode(&out->frame, &out->stream)) {
            if (out->stream.error == MAD_ERROR_BUFLEN) {
                remaining = out->stream.next_frame;
                break;
            }
            if (MAD_RECOVERABLE(out->stream.error)) continue;
            return SR_ERROR_DECODE_FAILURE;
        }
        mad_synth_frame(&out->synth, &out->frame);
        if (!out->channels) {
            out->channels = out->synth.pcm.channels;
            out->rate = out->synth.pcm.samplerate;
        }
        if (out->channels != out->synth.pcm.channels ||
            out->rate != out->synth.pcm.samplerate)
            return SR_ERROR_DECODE_FAILURE;
        length = out->synth.pcm.length * out->channels * 2;
        if (out->bytes > UINT32_MAX - 36 - length) {
            fprintf(stderr, "WAV output exceeds the RIFF 4 GiB size limit.\n");
            return SR_ERROR_CANT_WRITE_TO_FILE;
        }
        for (i = 0; i < out->synth.pcm.length; i++) {
            for (channel = 0; channel < out->channels; channel++) {
                int sample = scale(out->synth.pcm.samples[channel][i]);
                put16(pcm + (i * out->channels + channel) * 2, (uint16_t)sample);
            }
        }
        if (write_all(out, pcm, length) != SR_SUCCESS)
            return SR_ERROR_CANT_WRITE_TO_FILE;
        out->bytes += length;
    }
    consumed = remaining ? (unsigned int)(remaining - out->pending->data) : out->pending->len;
    g_byte_array_remove_range(out->pending, 0, consumed);
    return SR_SUCCESS;
}

error_code wav_output_init(WavOutput **output, FHANDLE file)
{
    WavOutput *out = g_new0(WavOutput, 1);
    *output = out;
    out->file = file;
    out->pending = g_byte_array_new();
    mad_stream_init(&out->stream);
    mad_frame_init(&out->frame);
    mad_synth_init(&out->synth);
    out->error = write_header(out);
    return out->error;
}

error_code wav_output_write(WavOutput *out, const char *data, size_t size)
{
    if (out->error != SR_SUCCESS) return out->error;
    /* Decoder buffering must stay bounded even for malformed input. */
    while (size) {
        size_t count = MIN(size, 16384);
        if (out->pending->len + count > 65536)
            return out->error = SR_ERROR_DECODE_FAILURE;
        g_byte_array_append(out->pending, (const guint8 *)data, count);
        out->error = decode(out);
        if (out->error != SR_SUCCESS) return out->error;
        data += count;
        size -= count;
    }
    return SR_SUCCESS;
}

error_code wav_output_finish(WavOutput **output)
{
    return wav_output_finish_duration(output, NULL);
}

error_code wav_output_finish_duration(WavOutput **output, guint64 *duration_ms)
{
    WavOutput *out = *output;
    error_code rc, header_rc;
    const guint8 guard[MAD_BUFFER_GUARD] = {0};
    if (!out) return SR_SUCCESS;
    if (out->pending->len && out->error == SR_SUCCESS) {
        g_byte_array_append(out->pending, guard, sizeof(guard));
        out->error = decode(out);
    }
    rc = out->error;
    if (rc == SR_SUCCESS && !out->channels) rc = SR_ERROR_DECODE_FAILURE;
    header_rc = write_header(out);
    audio_file_release(out->file, &out->allocation);
    if (duration_ms)
        *duration_ms = out->rate && out->channels
            ? (guint64)out->bytes * 1000 / (out->rate * out->channels * 2) : 0;
    mad_synth_finish(&out->synth);
    mad_frame_finish(&out->frame);
    mad_stream_finish(&out->stream);
    g_byte_array_unref(out->pending);
    g_free(out);
    *output = NULL;
    return rc != SR_SUCCESS ? rc : header_rc;
}
