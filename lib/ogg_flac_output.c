/*
 * Ogg FLAC recording finalization.
 * Copyright (C) 2026 Streamripper contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Rebuild transport/frame counters without decoding or re-encoding audio.
 * Live servers can prepend headers to pages from a long-running encoder;
 * those absolute granules and frame numbers do not describe a new recording.
 */
#include <string.h>
#include <ogg/ogg.h>
#include "ogg_flac_output.h"
#include "console_output.h"

#define MAX_FLAC_PACKET (16U * 1024U * 1024U)
#define MAX_FLAC_HEADERS (1024U * 1024U)

struct ogg_flac_output {
    ogg_sync_state sync;
    ogg_stream_state stream;
    BOOL stream_initialized, active, headers_done, have_audio, have_any_audio;
    BOOL drop_fragment, ended, finished;
    BOOL chain_closed;
    BOOL failed_emit;
    guint32 serial, next_input_sequence;
    guint rate, header_bytes;
    guint64 samples, frames, base_ms;
    guint64 written, chain_offset;
    GByteArray *packet, *pending;
    GByteArray *first_page;
    GPtrArray *headers;
    OggFlacEmit emit;
    OggFlacRewrite rewrite;
    void *context;
};

static error_code invalid(const char *message)
{
    sr_console_printf(stderr, "\nInvalid Ogg FLAC recording: %s\n", message);
    return SR_ERROR_PARSE_FAILURE;
}

static unsigned int crc8(const unsigned char *data, gsize size)
{
    unsigned int crc = 0;
    for (gsize i = 0; i < size; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            crc = ((crc << 1) ^ ((crc & 0x80) ? 0x07 : 0)) & 0xff;
    }
    return crc;
}

static unsigned int crc16(const unsigned char *data, gsize size)
{
    unsigned int crc = 0;
    for (gsize i = 0; i < size; i++) {
        crc ^= (unsigned int)data[i] << 8;
        for (int bit = 0; bit < 8; bit++)
            crc = ((crc << 1) ^ ((crc & 0x8000) ? 0x8005 : 0)) & 0xffff;
    }
    return crc;
}

static guint64 chain_duration(OggFlacOutput *output)
{
    return output->rate ? output->samples / output->rate * 1000 +
        output->samples % output->rate * 1000 / output->rate : 0;
}

static error_code emit_pending(OggFlacOutput *output, BOOL eos)
{
    if (!output->pending->len) return SR_SUCCESS;
    if (eos) {
        ogg_page page;
        output->pending->data[5] |= 4;
        page.header = output->pending->data;
        page.header_len = 27 + page.header[26];
        page.body = page.header + page.header_len;
        page.body_len = output->pending->len - page.header_len;
        ogg_page_checksum_set(&page);
    }
    error_code rc = output->emit(output->context, output->pending->data, output->pending->len);
    if (rc != SR_SUCCESS) output->failed_emit = TRUE;
    if (rc == SR_SUCCESS) {
        output->written += output->pending->len;
        g_byte_array_set_size(output->pending, 0);
    }
    return rc;
}

static error_code finish_chain(OggFlacOutput *output)
{
    if (output->chain_closed) return SR_SUCCESS;
    if (output->failed_emit) return SR_ERROR_CANT_WRITE_TO_FILE;
    error_code rc = emit_pending(output, TRUE);
    if (rc != SR_SUCCESS) return rc;
    if (output->first_page->len && output->have_audio) {
        ogg_page page;
        page.header = output->first_page->data;
        page.header_len = 27 + page.header[26];
        page.body = page.header + page.header_len;
        page.body_len = output->first_page->len - page.header_len;
        guint64 samples = output->samples <= G_GUINT64_CONSTANT(0xfffffffff) ? output->samples : 0;
        page.body[30] = (page.body[30] & 0xf0) | (samples >> 32);
        for (guint i = 0; i < 4; i++) page.body[34 - i] = (samples >> (8 * i)) & 0xff;
        ogg_page_checksum_set(&page);
        rc = output->rewrite(output->context, output->chain_offset,
                             output->first_page->data, output->first_page->len);
        if (rc != SR_SUCCESS) return rc;
    }
    output->chain_closed = TRUE;
    return SR_SUCCESS;
}

static error_code queue_packet(OggFlacOutput *output, GByteArray *data, BOOL bos, guint64 samples)
{
    ogg_packet packet;
    memset(&packet, 0, sizeof(packet));
    packet.packet = data->data;
    packet.bytes = data->len;
    packet.b_o_s = bos;
    packet.granulepos = samples;
    if (ogg_stream_packetin(&output->stream, &packet)) return invalid("packet buffering failed");
    ogg_page page;
    while (ogg_stream_flush(&output->stream, &page)) {
        error_code rc = emit_pending(output, FALSE);
        if (rc != SR_SUCCESS) return rc;
        g_byte_array_append(output->pending, page.header, page.header_len);
        g_byte_array_append(output->pending, page.body, page.body_len);
        if (bos && !output->first_page->len)
            g_byte_array_append(output->first_page, output->pending->data, output->pending->len);
    }
    return SR_SUCCESS;
}

static error_code start_audio(OggFlacOutput *output)
{
    if (ogg_stream_init(&output->stream, (int)output->serial)) return SR_ERROR_CANT_ALLOC_MEMORY;
    output->stream_initialized = TRUE;
    for (guint i = 0; i < output->headers->len; i++) {
        error_code rc = queue_packet(output, g_ptr_array_index(output->headers, i), i == 0, 0);
        if (rc != SR_SUCCESS) return rc;
    }
    g_ptr_array_set_size(output->headers, 0);
    return SR_SUCCESS;
}

/* FLAC integers use a UTF-8-like encoding with up to seven bytes (36 bits). */
static guint encode_number(guint64 value, unsigned char *bytes)
{
    guint size = 1, capacity = 7;
    while (size < 7 && value >= (G_GUINT64_CONSTANT(1) << capacity)) {
        size++;
        capacity = size == 7 ? 36 : size * 5 + 1;
    }
    if (value >= (G_GUINT64_CONSTANT(1) << capacity)) return 0;
    if (size == 1) { bytes[0] = value; return 1; }
    for (guint i = size - 1; i > 0; i--) {
        bytes[i] = 0x80 | (value & 0x3f);
        value >>= 6;
    }
    bytes[0] = (0xff << (8 - size)) | value;
    return size;
}

static error_code audio_packet(OggFlacOutput *output, GByteArray *source)
{
    const unsigned char *data = source->data;
    guint size = source->len;
    if (size < 8 || data[0] != 0xff || (data[1] & 0xfe) != 0xf8 ||
        (data[3] & 1) || (data[2] & 15) == 15) return invalid("invalid FLAC frame header");
    guint index_size = 1;
    if (data[4] & 0x80) {
        index_size = 0;
        while (index_size < 8 && (data[4] & (0x80 >> index_size))) index_size++;
        if (index_size < 2 || index_size > 7 || 4 + index_size >= size - 2)
            return invalid("invalid FLAC frame number");
        for (guint i = 1; i < index_size; i++)
            if ((data[4 + i] & 0xc0) != 0x80) return invalid("invalid FLAC frame number");
    }
    guint header = 4 + index_size;
    guint code = data[2] >> 4, block;
    if (!code) return invalid("reserved FLAC block size");
    if (code == 1) block = 192;
    else if (code <= 5) block = 576 << (code - 2);
    else if (code == 6) {
        if (header >= size - 2) return invalid("truncated FLAC block size");
        block = data[header++] + 1;
    } else if (code == 7) {
        if (header + 2 > size - 2) return invalid("truncated FLAC block size");
        block = ((guint)data[header] << 8 | data[header + 1]) + 1;
        header += 2;
    } else block = 256 << (code - 8);
    guint rate_code = data[2] & 15;
    if (rate_code == 12) header++;
    else if (rate_code == 13 || rate_code == 14) header += 2;
    if (header >= size - 2 || crc8(data, header) != data[header] ||
        crc16(data, size - 2) != ((guint)data[size - 2] << 8 | data[size - 1]))
        return invalid("FLAC frame checksum mismatch");
    if (output->samples > G_MAXINT64 - block) return invalid("recording is too long");
    unsigned char number[7];
    guint64 value = data[1] & 1 ? output->samples : output->frames;
    if (!(data[1] & 1) && value > 0x7fffffffU) return invalid("FLAC frame number overflow");
    guint count = encode_number(value, number);
    if (!count) return invalid("FLAC sample number overflow");
    GByteArray *frame = g_byte_array_sized_new(size + 7);
    g_byte_array_append(frame, data, 4);
    g_byte_array_append(frame, number, count);
    g_byte_array_append(frame, data + 4 + index_size, header - 4 - index_size);
    unsigned char checksum = crc8(frame->data, frame->len);
    g_byte_array_append(frame, &checksum, 1);
    g_byte_array_append(frame, data + header + 1, size - header - 3);
    guint crc = crc16(frame->data, frame->len);
    unsigned char tail[2] = {crc >> 8, crc & 0xff};
    g_byte_array_append(frame, tail, 2);
    error_code rc = SR_SUCCESS;
    if (!output->have_audio) rc = start_audio(output);
    if (rc == SR_SUCCESS) rc = queue_packet(output, frame, FALSE, output->samples + block);
    g_byte_array_unref(frame);
    if (rc == SR_SUCCESS) {
        output->samples += block;
        output->frames++;
        output->have_audio = output->have_any_audio = TRUE;
    }
    return rc;
}

static error_code process_packet(OggFlacOutput *output)
{
    GByteArray *packet = output->packet;
    if (!output->headers->len && !output->headers_done) {
        if (packet->len != 51 || memcmp(packet->data, "\177FLAC\1\0", 7) ||
            memcmp(packet->data + 9, "fLaC", 4) || (packet->data[13] & 0x7f) != 0 ||
            packet->data[14] || packet->data[15] || packet->data[16] != 34)
            return invalid("invalid FLAC mapping header");
        output->rate = (guint)packet->data[27] << 12 |
                       (guint)packet->data[28] << 4 | packet->data[29] >> 4;
        if (!output->rate) return invalid("missing FLAC sample rate");
        /* Counts/digest from the source describe another recording, not ours. */
        memset(packet->data + 21, 0, 6);
        packet->data[30] &= 0xf0;
        memset(packet->data + 31, 0, 20);
    } else if (output->headers_done) {
        return audio_packet(output, packet);
    } else {
        if (packet->len < 4 || packet->data[0] == 0xff ||
            ((guint)packet->data[1] << 16 | (guint)packet->data[2] << 8 | packet->data[3]) != packet->len - 4)
            return invalid("invalid FLAC metadata block");
        if (output->headers->len == 1 && (packet->data[0] & 0x7f) != 4)
            return invalid("missing FLAC comments header");
        if ((packet->data[0] & 0x7f) == 3 || (packet->data[0] & 0x7f) == 5) {
            /* Original seek/cue offsets belong to the source stream. Ogg granules
               provide seeking for this recording; retain packet size as padding. */
            packet->data[0] = (packet->data[0] & 0x80) | 1;
            memset(packet->data + 4, 0, packet->len - 4);
        }
    }
    if (packet->len > MAX_FLAC_HEADERS - output->header_bytes) return invalid("FLAC headers exceed limit");
    output->header_bytes += packet->len;
    if (packet->data[0] & 0x80) output->headers_done = TRUE;
    GByteArray *copy = g_byte_array_sized_new(packet->len);
    g_byte_array_append(copy, packet->data, packet->len);
    g_ptr_array_add(output->headers, copy);
    return SR_SUCCESS;
}

static error_code begin_chain(OggFlacOutput *output, guint32 serial)
{
    error_code rc = finish_chain(output);
    if (rc != SR_SUCCESS) return rc;
    output->base_ms += chain_duration(output);
    output->samples = output->frames = 0;
    output->rate = output->header_bytes = 0;
    output->have_audio = output->headers_done = output->drop_fragment = output->ended = FALSE;
    output->active = TRUE;
    output->chain_closed = FALSE;
    output->chain_offset = output->written;
    output->serial = serial;
    g_byte_array_set_size(output->first_page, 0);
    g_byte_array_set_size(output->packet, 0);
    g_ptr_array_set_size(output->headers, 0);
    if (output->stream_initialized) {
        ogg_stream_clear(&output->stream);
        output->stream_initialized = FALSE;
    }
    return SR_SUCCESS;
}

static error_code process_page(OggFlacOutput *output, ogg_page *page)
{
    guint32 serial = (guint32)ogg_page_serialno(page);
    guint32 sequence = (guint32)ogg_page_pageno(page);
    if (ogg_page_bos(page)) {
        if (ogg_page_continued(page)) return invalid("continued beginning-of-stream page");
        if (output->active && output->packet->len) return invalid("incomplete packet before a chained stream");
        error_code rc = begin_chain(output, serial);
        if (rc != SR_SUCCESS) return rc;
    } else {
        if (!output->active || output->ended || serial != output->serial)
            return invalid("unexpected logical stream");
        BOOL first_audio = output->headers_done && !output->have_audio && !output->packet->len;
        if (sequence != output->next_input_sequence && !first_audio)
            return invalid("missing Ogg page");
        if (ogg_page_continued(page) && !output->packet->len) {
            if (!first_audio) return invalid("missing FLAC packet fragment");
            output->drop_fragment = TRUE;
        } else if (!ogg_page_continued(page) && output->packet->len)
            return invalid("interrupted FLAC packet");
    }
    output->next_input_sequence = sequence + 1;
    guint offset = 0;
    for (guint i = 0; i < page->header[26]; i++) {
        guint size = page->header[27 + i];
        if (!output->drop_fragment) {
            if (size > MAX_FLAC_PACKET - output->packet->len) return invalid("FLAC packet exceeds limit");
            g_byte_array_append(output->packet, page->body + offset, size);
        }
        offset += size;
        if (size < 255) {
            if (output->drop_fragment) output->drop_fragment = FALSE;
            else {
                error_code rc = process_packet(output);
                if (rc != SR_SUCCESS) return rc;
                g_byte_array_set_size(output->packet, 0);
            }
        }
    }
    if (ogg_page_eos(page)) {
        if (output->packet->len || output->drop_fragment) return invalid("incomplete end-of-stream packet");
        output->ended = TRUE;
        return finish_chain(output);
    }
    return SR_SUCCESS;
}

OggFlacOutput *ogg_flac_output_new(OggFlacEmit emit, OggFlacRewrite rewrite, void *context)
{
    OggFlacOutput *output = g_new0(OggFlacOutput, 1);
    if (ogg_sync_init(&output->sync)) { g_free(output); return NULL; }
    output->packet = g_byte_array_new();
    output->pending = g_byte_array_new();
    output->first_page = g_byte_array_new();
    output->headers = g_ptr_array_new_with_free_func((GDestroyNotify)g_byte_array_unref);
    output->emit = emit;
    output->rewrite = rewrite;
    output->context = context;
    return output;
}

error_code ogg_flac_output_write(OggFlacOutput *output, const unsigned char *data, gsize size)
{
    if (output->finished || size > MAX_FLAC_PACKET) return invalid("invalid input size or finalized output");
    char *buffer = ogg_sync_buffer(&output->sync, size);
    if (!buffer) return SR_ERROR_CANT_ALLOC_MEMORY;
    memcpy(buffer, data, size);
    if (ogg_sync_wrote(&output->sync, size)) return invalid("input buffering failed");
    ogg_page page;
    int result;
    while ((result = ogg_sync_pageout(&output->sync, &page)) != 0) {
        if (result < 0) return invalid("Ogg page checksum or framing mismatch");
        error_code rc = process_page(output, &page);
        if (rc != SR_SUCCESS) return rc;
    }
    return SR_SUCCESS;
}

error_code ogg_flac_output_finish(OggFlacOutput *output)
{
    if (output->finished) return SR_SUCCESS;
    error_code rc = finish_chain(output);
    if (rc == SR_SUCCESS) output->finished = TRUE;
    /* A partial final page/frame has never reached the writer and is discarded. */
    return rc;
}

BOOL ogg_flac_output_duration(OggFlacOutput *output, guint64 *milliseconds)
{
    *milliseconds = output->base_ms + chain_duration(output);
    return output->have_any_audio;
}

void ogg_flac_output_free(OggFlacOutput *output)
{
    if (!output) return;
    if (output->stream_initialized) ogg_stream_clear(&output->stream);
    ogg_sync_clear(&output->sync);
    g_byte_array_unref(output->packet);
    g_byte_array_unref(output->pending);
    g_byte_array_unref(output->first_page);
    g_ptr_array_unref(output->headers);
    g_free(output);
}
