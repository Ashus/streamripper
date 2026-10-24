/* Native MP3/ADTS file framing and finalization, GPL-2.0-or-later.
 * Compressed audio frames are copied unchanged. No decoder or encoder is used.
 */
#include "framed_audio.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifndef WIN32
#include <unistd.h>
#endif

typedef struct {
    unsigned int size, rate, samples, bitrate, version, channels, side;
} AudioHeader;

/* Divisible by every supported MPEG/ADTS sample rate, including 44100/48000. */
#define AUDIO_CLOCK G_GUINT64_CONSTANT(28224000)

struct framed_audio {
    FHANDLE file;
    int type;
    GByteArray *pending;
    gboolean synced, variable;
    unsigned int rate, channels, bitrate, index_size, index_offset;
    unsigned char index[576];
    guint64 frames, bytes, duration_ticks, input_bytes;
    gint64 index_position, tlen_position;
    error_code error;
};

static void put32(unsigned char *p, guint32 value)
{
    p[0] = value >> 24; p[1] = value >> 16;
    p[2] = value >> 8; p[3] = value;
}

static gint64 position(FHANDLE file, gint64 offset, int origin)
{
#ifdef WIN32
    LARGE_INTEGER distance, result;
    distance.QuadPart = offset;
    if (!SetFilePointerEx(file, distance, &result, origin)) return -1;
    return result.QuadPart;
#else
    return lseek(file, offset, origin);
#endif
}

static error_code write_all(FHANDLE file, const unsigned char *data, size_t size)
{
    while (size) {
#ifdef WIN32
        DWORD count;
        if (!WriteFile(file, data, (DWORD)MIN(size, 16384), &count, NULL) || !count)
            return SR_ERROR_CANT_WRITE_TO_FILE;
#else
        ssize_t count = write(file, data, size);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return SR_ERROR_CANT_WRITE_TO_FILE;
#endif
        data += count; size -= count;
    }
    return SR_SUCCESS;
}

static gboolean header(const unsigned char *p, size_t available, int type, AudioHeader *h)
{
    static const unsigned int rates[] = {44100, 48000, 32000};
    static const unsigned int adts_rates[] = {
        96000, 88200, 64000, 48000, 44100, 32000, 24000,
        22050, 16000, 12000, 11025, 8000, 7350
    };
    static const unsigned int mpeg1[] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320};
    static const unsigned int mpeg2[] = {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160};
    unsigned int version, rate_index, bitrate_index;
    memset(h, 0, sizeof(*h));
    if (type == CONTENT_TYPE_AAC) {
        unsigned int minimum;
        if (available < 7 || p[0] != 0xff || (p[1] & 0xf6) != 0xf0) return FALSE;
        rate_index = (p[2] >> 2) & 15;
        if (rate_index >= G_N_ELEMENTS(adts_rates)) return FALSE;
        h->size = ((p[3] & 3) << 11) | (p[4] << 3) | (p[5] >> 5);
        minimum = (p[1] & 1) ? 7 : 9;
        if (h->size <= minimum) return FALSE;
        h->rate = adts_rates[rate_index];
        h->samples = 1024 * ((p[6] & 3) + 1);
        return TRUE;
    }
    if (available < 4 || p[0] != 0xff || (p[1] & 0xe0) != 0xe0) return FALSE;
    version = (p[1] >> 3) & 3;
    rate_index = (p[2] >> 2) & 3;
    bitrate_index = p[2] >> 4;
    /* Free-format and non-Layer-III streams need a different parser. */
    if (version == 1 || (p[1] & 6) != 2 || rate_index == 3 ||
        !bitrate_index || bitrate_index == 15 || (p[3] & 3) == 2) return FALSE;
    h->version = version;
    h->rate = rates[rate_index] >> (version == 3 ? 0 : version == 2 ? 1 : 2);
    h->bitrate = (version == 3 ? mpeg1 : mpeg2)[bitrate_index] * 1000;
    h->samples = version == 3 ? 1152 : 576;
    h->size = (version == 3 ? 144 : 72) * h->bitrate / h->rate + ((p[2] >> 1) & 1);
    h->channels = (p[3] >> 6) == 3 ? 1 : 2;
    h->side = version == 3 ? (h->channels == 1 ? 17 : 32) : (h->channels == 1 ? 9 : 17);
    return h->size >= 4 + h->side;
}

gboolean framed_audio_adts_boundary(const unsigned char *data, size_t size,
                                    size_t target, size_t *boundary)
{
    size_t pos;
    AudioHeader h, next;
    for (pos = 0; pos + 7 <= size; pos++) {
        if (!header(data + pos, size - pos, CONTENT_TYPE_AAC, &h)) continue;
        if (pos + h.size + 7 > size ||
            !header(data + pos + h.size, size - pos - h.size, CONTENT_TYPE_AAC, &next))
            continue;
        /* Walk the verified frame chain, including the frame containing target. */
        while (pos < target && pos + h.size + 7 <= size) {
            size_t following = pos + h.size;
            if (!header(data + following, size - following, CONTENT_TYPE_AAC, &next)) break;
            pos = following; h = next;
        }
        if (pos >= target) { *boundary = pos; return TRUE; }
    }
    return FALSE;
}

static error_code begin_mp3(FramedAudio *out, const unsigned char *p, const AudioHeader *h)
{
    AudioHeader index;
    memcpy(out->index, p, 4);
    out->index[1] |= 1; /* The synthetic index frame has no CRC. */
    out->index[2] = ((h->version == 3 ? 9 : 8) << 4) | (p[2] & 12);
    out->index[3] = p[3] & 0xc0;
    if (!header(out->index, sizeof(out->index), CONTENT_TYPE_MP3, &index) ||
        index.size > sizeof(out->index)) return SR_ERROR_DECODE_FAILURE;
    out->index_size = index.size;
    out->index_offset = 4 + h->side;
    memcpy(out->index + out->index_offset, "Xing", 4);
    put32(out->index + out->index_offset + 4, 3); /* Frame count and byte count. */
    out->index_position = position(out->file, 0, SEEK_CUR);
    if (out->index_position < 0) return SR_ERROR_CANT_WRITE_TO_FILE;
    out->rate = h->rate; out->channels = h->channels; out->bitrate = h->bitrate;
    out->bytes = out->index_size;
    return write_all(out->file, out->index, out->index_size);
}

static error_code drain(FramedAudio *out, gboolean final)
{
    size_t pos = 0;
    const size_t minimum = out->type == CONTENT_TYPE_AAC ? 7 : 4;
    while (pos + minimum <= out->pending->len) {
        const unsigned char *p = out->pending->data + pos;
        size_t available = out->pending->len - pos;
        AudioHeader h, next;
        if (!header(p, available, out->type, &h)) {
            out->synced = FALSE; pos++; continue;
        }
        if (available < h.size) {
            /* Never scan inside a known partial frame's payload at EOF. */
            if (!final || out->synced) break;
            out->synced = FALSE; pos++; continue;
        }
        if (!out->synced) {
            if (available < h.size + minimum && !final) break;
            if (available >= h.size + minimum && !header(p + h.size, available - h.size, out->type, &next)) {
                pos++; continue;
            }
            out->synced = TRUE;
        }
        if (out->type == CONTENT_TYPE_MP3) {
            unsigned int offset = 4 + h.side;
            /* A source file's duration header describes another recording. */
            if ((h.size >= offset + 4 && (!memcmp(p + offset, "Xing", 4) || !memcmp(p + offset, "Info", 4))) ||
                (h.size >= 40 && !memcmp(p + 36, "VBRI", 4))) {
                pos += h.size; continue;
            }
            if (!out->index_size) {
                error_code rc = begin_mp3(out, p, &h);
                if (rc != SR_SUCCESS) return rc;
            }
            if (h.rate != out->rate || h.channels != out->channels)
                return SR_ERROR_DECODE_FAILURE;
            if (h.bitrate != out->bitrate) out->variable = TRUE;
            if (out->frames >= UINT32_MAX || out->bytes > UINT32_MAX - h.size)
                return SR_ERROR_CANT_WRITE_TO_FILE;
        }
        {
            error_code rc = write_all(out->file, p, h.size);
            if (rc != SR_SUCCESS) return rc;
        }
        out->frames++; out->bytes += h.size;
        out->duration_ticks += h.samples * (AUDIO_CLOCK / h.rate);
        pos += h.size;
    }
    if (pos) g_byte_array_remove_range(out->pending, 0, (guint)pos);
    return SR_SUCCESS;
}

error_code framed_audio_init(FramedAudio **output, FHANDLE file, int type, gint64 tlen_offset)
{
    FramedAudio *out = g_new0(FramedAudio, 1);
    out->file = file; out->type = type; out->tlen_position = tlen_offset;
    out->pending = g_byte_array_new();
    *output = out;
    return SR_SUCCESS;
}

error_code framed_audio_write(FramedAudio *out, const char *data, size_t size)
{
    out->input_bytes += size;
    while (size && out->error == SR_SUCCESS) {
        /* Retain at most one partial 8191-byte ADTS frame plus lookahead;
           bounded input chunks keep memory independent of recording length. */
        size_t count = MIN(size, 8192);
        g_byte_array_append(out->pending, (const guint8 *)data, count);
        out->error = drain(out, FALSE);
        data += count; size -= count;
    }
    return out->error;
}

error_code framed_audio_finish(FramedAudio **output)
{
    return framed_audio_finish_duration(output, NULL);
}

error_code framed_audio_finish_duration(FramedAudio **output, guint64 *duration_ms)
{
    FramedAudio *out = *output;
    gint64 end;
    error_code rc;
    guint64 duration;
    if (!out) return SR_SUCCESS;
    if (out->error == SR_SUCCESS) out->error = drain(out, TRUE);
    if (out->input_bytes && !out->frames && out->error == SR_SUCCESS)
        out->error = SR_ERROR_DECODE_FAILURE;
    /* Round only once, including streams that change their ADTS sample rate. */
    duration = out->duration_ticks / (AUDIO_CLOCK / 1000);
    if (duration_ms) *duration_ms = duration;
    end = position(out->file, 0, SEEK_CUR);
    if (out->index_size) {
        unsigned char *tag = out->index + out->index_offset;
        memcpy(tag, out->variable ? "Xing" : "Info", 4);
        put32(tag + 8, (guint32)out->frames);
        put32(tag + 12, (guint32)out->bytes);
        if (position(out->file, out->index_position, SEEK_SET) < 0 ||
            write_all(out->file, out->index, out->index_size) != SR_SUCCESS)
            out->error = SR_ERROR_CANT_WRITE_TO_FILE;
    }
    if (out->tlen_position >= 0 && out->frames) {
        unsigned char tag[32] = {0};
        int length;
        memcpy(tag, "TLEN", 4);
        length = g_snprintf((char *)tag + 11, sizeof(tag) - 11,
                            "%" G_GUINT64_FORMAT, duration);
        put32(tag + 4, length + 1); /* ID3v2.3, Latin-1 digit string. */
        if (position(out->file, out->tlen_position, SEEK_SET) < 0 ||
            write_all(out->file, tag, sizeof(tag)) != SR_SUCCESS)
            out->error = SR_ERROR_CANT_WRITE_TO_FILE;
    }
    if (end < 0 || position(out->file, end, SEEK_SET) < 0)
        out->error = SR_ERROR_CANT_WRITE_TO_FILE;
    rc = out->error;
    g_byte_array_unref(out->pending);
    g_free(out); *output = NULL;
    return rc;
}
