/*
 * Whole-stream HLS and FLAC recording.
 *
 * Copyright (C) 2026 Streamripper contributors
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 or later.
 * This program comes WITHOUT ANY WARRANTY. See COPYING for details.
 *
 * The supported HLS/FLAC scope was reviewed against Jakub Kaprzyk's
 * jaakristioja/streamripper, commit 71f683145246b2bd8c1bcf3e55cc2d9d3c4878c7.
 * This implementation uses this fork's HTTP/TLS and file writer APIs.
 */
#include <string.h>
#include <stdlib.h>
#include <openssl/evp.h>
#include "stream_capture.h"
#include "http.h"
#include "socklib.h"
#include "filelib.h"
#include "callback.h"
#include "mchar.h"
#include "track_info.h"
#include "console_output.h"
#include "../third_party/charset.h"
#include "ogg_flac_output.h"

#define FETCH_LIMIT (128U * 1024U * 1024U)
#define PLAYLIST_LIMIT (1024U * 1024U)
#define SEGMENT_LIMIT 4096

typedef struct {
    char* url;
    char* key;
    guint64 sequence;
    unsigned char iv[16];
    BOOL explicit_iv;
    double duration;
} Segment;

struct stream_capture {
    BOOL hls, finished, writer_open, have_sequence, endlist, aac;
    Writer writer;
    char* playlist;
    GPtrArray* segments;
    guint index;
    guint64 last_sequence;
    gint64 poll_at;
    double poll_seconds;
    gint64 remaining;
    int icy_remaining;
    guint64 duration_ms;
    char* cached_key_url;
    unsigned char cached_key[16];
    unsigned char prefix[4];
    guint prefix_size;
    char display_title[MAX_TRACK_LEN];
    OggFlacOutput* ogg;
};

static error_code write_capture(RIP_MANAGER_INFO* rmi, const unsigned char* data, guint length);

static error_code emit_ogg(void* context, const unsigned char* data, gsize size) {
    return write_capture(context, data, size);
}

static error_code rewrite_ogg(void* context, guint64 offset, const unsigned char* data, gsize size) {
    RIP_MANAGER_INFO* rmi = context;
    struct stream_capture* state = rmi->capture;
    error_code rc = SR_SUCCESS;
    if (state->writer.m_started)
        rc = filelib_rewrite_at(state->writer.m_file, offset, data, size);
    if (rc == SR_SUCCESS && rmi->filelib_info.m_do_show)
        rc = filelib_rewrite_at(rmi->filelib_info.m_show_file, offset, data, size);
    if (rc != SR_SUCCESS)
        state->writer.m_cleanup_blocked = TRUE;
    return rc;
}

static void update_ogg_duration(RIP_MANAGER_INFO* rmi) {
    struct stream_capture* state = rmi->capture;
    guint64 milliseconds;
    if (state->ogg && ogg_flac_output_duration(state->ogg, &milliseconds)) {
        state->writer.m_duration_ms = milliseconds;
        state->writer.m_duration_known = TRUE;
        rmi->callback_duration_ms = milliseconds;
        rmi->callback_duration_known = TRUE;
    }
}

static error_code finish_ogg(RIP_MANAGER_INFO* rmi) {
    struct stream_capture* state = rmi->capture;
    if (!state->ogg)
        return SR_SUCCESS;
    error_code rc = ogg_flac_output_finish(state->ogg);
    update_ogg_duration(rmi);
    if (rc != SR_SUCCESS)
        state->writer.m_cleanup_blocked = TRUE;
    return rc;
}

static error_code invalid(const char* message) {
    sr_console_printf(stderr, "\nUnsupported or invalid stream: %s\n", message);
    return SR_ERROR_PARSE_FAILURE;
}

static void close_transport(RIP_MANAGER_INFO* rmi) {
    socklib_close(&rmi->stream_sock);
#ifdef HAVE_OPENSSL
    socklib_free_tls(&rmi->stream_sock);
#endif
}

static error_code open_url(RIP_MANAGER_INFO* rmi, const char* url, SR_HTTP_HEADER* info) {
    const char* proxy = rmi->prefs->proxyurl[0] ? rmi->prefs->proxyurl : getenv("http_proxy");
    close_transport(rmi);
    memset(info, 0, sizeof(*info));
    rmi->retry_after_seconds = 0;
    return http_sc_connect(rmi, &rmi->stream_sock, url, proxy && *proxy ? proxy : NULL,
                           info, rmi->prefs->useragent, rmi->prefs->if_name);
}

static error_code read_body(RIP_MANAGER_INFO* rmi, const SR_HTTP_HEADER* info,
                            guint limit, GByteArray** result) {
    GByteArray* body = g_byte_array_new();
    gint64 remaining = rmi->stream_sock.chunked ? -1 : info->content_length;
    error_code rc = SR_SUCCESS;
    if (remaining > limit)
        rc = invalid("HTTP body exceeds the recording limit");
    while (rc == SR_SUCCESS && remaining != 0) {
        char buffer[16384];
        int count = remaining < 0 ? sizeof(buffer) : MIN(remaining, (gint64)sizeof(buffer));
        int got = socklib_recvall(rmi, &rmi->stream_sock, buffer, count, rmi->prefs->timeout);
        if (got < 0) {
            rc = got;
            break;
        }
        if (!got) {
            if (remaining > 0)
                rc = SR_ERROR_RECV_FAILED;
            break;
        }
        if (body->len + (guint)got > limit) {
            rc = invalid("HTTP body exceeds the recording limit");
            break;
        }
        g_byte_array_append(body, (guint8*)buffer, got);
        if (remaining > 0)
            remaining -= got;
    }
    close_transport(rmi);
    if (rc != SR_SUCCESS) {
        g_byte_array_unref(body);
        return rc;
    }
    *result = body;
    return SR_SUCCESS;
}

static error_code fetch(RIP_MANAGER_INFO* rmi, const char* url, guint limit,
                        GByteArray** body, char** effective) {
    SR_HTTP_HEADER info;
    error_code rc = open_url(rmi, url, &info);
    if (rc == SR_SUCCESS) {
        if (effective)
            *effective = g_strdup(info.effective_url);
        rc = read_body(rmi, &info, limit, body);
    }
    else
        close_transport(rmi);
    return rc;
}

static char* resolve(const char* base, const char* relative) {
    char* url = g_uri_resolve_relative(base, relative, G_URI_FLAGS_ENCODED, NULL);
    if (!url)
        return NULL;
    if ((!g_str_has_prefix(url, "http://") && !g_str_has_prefix(url, "https://")) ||
        strlen(url) >= MAX_URL_LEN) {
        g_free(url);
        return NULL;
    }
    return url;
}

/* HLS attribute lists contain quoted values which may themselves contain commas. */
static char* attribute(const char* line, const char* name) {
    const char* p = strchr(line, ':');
    if (!p)
        return NULL;
    p++;
    while (*p) {
        while (*p == ',' || g_ascii_isspace(*p))
            p++;
        const char* key = p;
        while (*p && *p != '=' && *p != ',')
            p++;
        if (*p != '=')
            return NULL;
        size_t length = p - key;
        p++;
        BOOL quoted = *p == '"';
        if (quoted)
            p++;
        const char* value = p;
        while (*p && (quoted ? *p != '"' : *p != ','))
            p++;
        if (strlen(name) == length && !strncmp(key, name, length))
            return g_strndup(value, p - value);
        if (quoted && *p)
            p++;
        while (*p && *p != ',')
            p++;
    }
    return NULL;
}

static void segment_free(gpointer data) {
    Segment* segment = data;
    g_free(segment->url);
    g_free(segment->key);
    g_free(segment);
}

static char** playlist_lines(GByteArray* body) {
    if (!body->len || memchr(body->data, 0, body->len) ||
        !g_utf8_validate((char*)body->data, body->len, NULL))
        return NULL;
    char* text = g_strndup((char*)body->data, body->len);
    char** lines = g_strsplit(text, "\n", -1);
    g_free(text);
    for (guint i = 0; lines[i]; i++)
        g_strstrip(lines[i]);
    if (strcmp(lines[0], "#EXTM3U")) {
        g_strfreev(lines);
        return NULL;
    }
    return lines;
}

static error_code load_playlist(RIP_MANAGER_INFO* rmi, GByteArray* initial, const char* initial_url) {
    struct stream_capture* state = rmi->capture;
    GByteArray* body = initial;
    char* base = g_strdup(initial_url);
    error_code rc = SR_SUCCESS;
    for (unsigned depth = 0; depth < 6; depth++) {
        char** lines = playlist_lines(body);
        g_byte_array_unref(body);
        body = NULL;
        if (!lines) {
            rc = invalid("expected an UTF-8 HLS playlist");
            break;
        }
        char* chosen = NULL;
        guint64 chosen_bandwidth = 0, best_distance = G_MAXUINT64;
        BOOL master = FALSE;
        for (guint i = 0; lines[i]; i++) {
            if (!g_str_has_prefix(lines[i], "#EXT-X-STREAM-INF:"))
                continue;
            master = TRUE;
            char* audio_group = attribute(lines[i], "AUDIO");
            if (audio_group) {
                g_free(audio_group);
                rc = invalid("HLS variants with separate audio renditions are unsupported");
                break;
            }
            char* bandwidth = attribute(lines[i], "BANDWIDTH");
            char* end = NULL;
            guint64 amount = bandwidth ? g_ascii_strtoull(bandwidth, &end, 10) : 0;
            if (end && *end)
                amount = 0;
            g_free(bandwidth);
            guint j = i + 1;
            while (lines[j] && (!*lines[j] || *lines[j] == '#'))
                j++;
            if (!amount || !lines[j]) {
                rc = invalid("invalid HLS variant");
                break;
            }
            int preference = rmi->prefs->hls_variant;
            guint64 distance = preference > 0 ? (amount > (guint)preference ? amount - preference : preference - amount) : 0;
            if (!chosen || (preference == 0 && amount > chosen_bandwidth) ||
                (preference == -1 && amount < chosen_bandwidth) ||
                (preference > 0 && distance < best_distance)) {
                g_free(chosen);
                chosen = resolve(base, lines[j]);
                chosen_bandwidth = amount;
                best_distance = distance;
            }
        }
        if (rc != SR_SUCCESS) {
            g_free(chosen);
            g_strfreev(lines);
            break;
        }
        if (master) {
            g_strfreev(lines);
            if (!chosen) {
                rc = invalid("no usable HLS variant");
                break;
            }
            char* effective = NULL;
            rc = fetch(rmi, chosen, PLAYLIST_LIMIT, &body, &effective);
            g_free(chosen);
            if (rc != SR_SUCCESS) {
                g_free(effective);
                break;
            }
            g_free(base);
            base = effective;
            continue;
        }
        GPtrArray* segments = g_ptr_array_new_with_free_func(segment_free);
        guint64 sequence = 0;
        BOOL endlist = FALSE, explicit_iv = FALSE;
        unsigned char iv[16] = {0};
        char* key = NULL;
        double target = 6, duration = -1;
        for (guint i = 1; lines[i] && rc == SR_SUCCESS; i++) {
            char* line = lines[i];
            if (g_str_has_prefix(line, "#EXT-X-MEDIA-SEQUENCE:")) {
                char* end;
                const char* value = line + strlen("#EXT-X-MEDIA-SEQUENCE:");
                sequence = g_ascii_strtoull(value, &end, 10);
                if (!*value || *value == '-' || *end || segments->len)
                    rc = invalid("invalid media sequence");
            }
            else if (g_str_has_prefix(line, "#EXT-X-TARGETDURATION:")) {
                target = g_ascii_strtod(strchr(line, ':') + 1, NULL);
                if (!(target > 0 && target <= 86400))
                    rc = invalid("invalid target duration");
            }
            else if (g_str_has_prefix(line, "#EXTINF:")) {
                duration = g_ascii_strtod(line + 8, NULL);
                if (!(duration >= 0 && duration <= 86400))
                    rc = invalid("invalid segment duration");
            }
            else if (g_str_has_prefix(line, "#EXT-X-KEY:")) {
                char* method = attribute(line, "METHOD");
                char* uri = attribute(line, "URI");
                char* iv_text = attribute(line, "IV");
                char* format = attribute(line, "KEYFORMAT");
                if (format && strcmp(format, "identity"))
                    rc = invalid("only HLS identity key format is supported");
                g_free(format);
                g_clear_pointer(&key, g_free);
                explicit_iv = FALSE;
                if (method && !strcmp(method, "AES-128")) {
                    key = uri ? resolve(base, uri) : NULL;
                    if (!key)
                        rc = invalid("AES-128 requires a key URI");
                    if (iv_text) {
                        const char* hex = g_str_has_prefix(iv_text, "0x") ? iv_text + 2 : iv_text;
                        size_t digits = strlen(hex);
                        if (!digits || digits > 32)
                            rc = invalid("AES-128 IV must contain 1 to 32 hexadecimal digits");
                        else {
                            explicit_iv = TRUE;
                            memset(iv, 0, sizeof(iv));
                            for (guint n = 0; n < digits; n++) {
                                int value = g_ascii_xdigit_value(hex[digits - n - 1]);
                                if (value < 0) {
                                    rc = invalid("invalid AES-128 IV");
                                    break;
                                }
                                iv[15 - n / 2] |= value << ((n % 2) * 4);
                            }
                        }
                    }
                }
                else
                    if (!method || strcmp(method, "NONE"))
                        rc = invalid("only HLS METHOD=NONE and AES-128 are supported");
                g_free(method);
                g_free(uri);
                g_free(iv_text);
            }
            else if (!strcmp(line, "#EXT-X-ENDLIST"))
                endlist = TRUE;
            else if (g_str_has_prefix(line, "#EXT-X-MAP:") || g_str_has_prefix(line, "#EXT-X-BYTERANGE:") ||
                g_str_has_prefix(line, "#EXT-X-GAP") || g_str_has_prefix(line, "#EXT-X-I-FRAMES-ONLY"))
                rc = invalid("fMP4, byte ranges, gaps and I-frame-only HLS are unsupported");
            else if (*line && *line != '#') {
                Segment* segment = g_new0(Segment, 1);
                segment->url = resolve(base, line);
                segment->key = g_strdup(key);
                segment->sequence = sequence++;
                segment->duration = duration;
                segment->explicit_iv = explicit_iv;
                memcpy(segment->iv, iv, 16);
                if (!segment->url || duration < 0 || sequence == 0 || segments->len >= SEGMENT_LIMIT) {
                    segment_free(segment);
                    rc = invalid("invalid or oversized media playlist");
                }
                else
                    g_ptr_array_add(segments, segment);
                duration = -1;
            }
        }
        g_free(key);
        g_strfreev(lines);
        if (rc != SR_SUCCESS) {
            g_ptr_array_unref(segments);
            break;
        }
        if (state->have_sequence && segments->len &&
            ((Segment*)g_ptr_array_index(segments, 0))->sequence > state->last_sequence + 1) {
            g_ptr_array_unref(segments);
            rc = invalid("HLS window advanced past an unrecorded segment");
            break;
        }
        if (state->segments)
            g_ptr_array_unref(state->segments);
        state->segments = segments;
        g_clear_pointer(&state->cached_key_url, g_free);
        state->index = 0;
        state->endlist = endlist;
        state->poll_seconds = MAX(1.0, target / 2);
        state->poll_at = g_get_monotonic_time() + (gint64)(state->poll_seconds * G_USEC_PER_SEC);
        g_free(state->playlist);
        state->playlist = g_strdup(base);
        if (!state->writer_open && segments->len) {
            const char* url = ((Segment*)g_ptr_array_index(segments, 0))->url;
            char *path = g_strdup(url), *query = strpbrk(path, "?#");
            if (query)
                *query = 0;
            state->aac = g_str_has_suffix(path, ".aac");
            g_free(path);
        }
        g_free(base);
        return SR_SUCCESS;
    }
    if (body)
        g_byte_array_unref(body);
    g_free(base);
    return rc == SR_SUCCESS ? invalid("too many nested HLS master playlists") : rc;
}

error_code stream_capture_prepare(RIP_MANAGER_INFO* rmi) {
    if (rmi->http_info.content_type == CONTENT_TYPE_OGG) {
        /* Peek through the verified transport, retaining every byte for either decoder. */
        unsigned char prefix[64];
        int count = rmi->http_info.meta_interval > 0 ? MIN(64, rmi->http_info.meta_interval) : 64;
        if (rmi->http_info.content_length >= 0)
            count = MIN(count, rmi->http_info.content_length);
        int got = socklib_recvall(rmi, &rmi->stream_sock, (char*)prefix, count, rmi->prefs->timeout);
        if (got < 0)
            return got;
        memcpy(rmi->stream_sock.prefetched, prefix, got);
        rmi->stream_sock.prefetched_size = got;
        if (got > 27 && !memcmp(prefix, "OggS", 4)) {
            unsigned int packet = 27 + prefix[26];
            if (packet + 5 <= (unsigned)got && !memcmp(prefix + packet, "\177FLAC", 5)) {
                rmi->http_info.content_type = CONTENT_TYPE_FLAC;
                rmi->flac_in_ogg = TRUE;
            }
        }
    }
    int type = rmi->http_info.content_type;
    if (type != CONTENT_TYPE_FLAC && type != CONTENT_TYPE_HLS)
        return SR_SUCCESS;
    if (rmi->prefs->rules_file[0])
        return invalid("--metadata-rules is unavailable for whole-stream HLS/FLAC recording");
    if (GET_EXTERNAL_CMD(rmi->prefs->flags))
        return invalid("--external-metadata is unavailable for whole-stream HLS/FLAC recording");
    struct stream_capture* state = g_new0(struct stream_capture, 1);
    rmi->capture = state;
    state->writer.m_file = INVALID_FHANDLE;
    state->remaining = rmi->stream_sock.chunked ? -1 : rmi->http_info.content_length;
    state->icy_remaining = rmi->http_info.meta_interval;
    state->hls = type == CONTENT_TYPE_HLS;
    if (!state->hls) {
        if (rmi->flac_in_ogg) {
            state->ogg = ogg_flac_output_new(emit_ogg, rewrite_ogg, rmi);
            if (!state->ogg)
                return SR_ERROR_CANT_ALLOC_MEMORY;
        }
        return SR_SUCCESS;
    }
    GByteArray* body = NULL;
    error_code rc = read_body(rmi, &rmi->http_info, PLAYLIST_LIMIT, &body);
    if (rc == SR_SUCCESS)
        rc = load_playlist(rmi, body, rmi->http_info.effective_url);
    return rc;
}

static error_code write_capture(RIP_MANAGER_INFO* rmi, const unsigned char* data, guint length) {
    struct stream_capture* state = rmi->capture;
    error_code rc;
    if (!state->writer_open) {
        track_info_clear(&state->writer.m_ti);
        TRACK_INFO* ti = &state->writer.m_ti;
        ti->save_track = TRUE;
        ti->have_track_info = TRUE;
        gstring_from_string(rmi, ti->title, MAX_TRACK_LEN, rmi->streamname, CODESET_METADATA);
        TRACK_INFO display;
        track_info_clear(&display);
        rc = callback_start_track(rmi, &display);
        if (rc != SR_SUCCESS)
            return rc;
        rmi->callback_incomplete = FALSE;
        rmi->callback_filtered = FALSE;
        g_strlcpy(rmi->callback_filename, state->display_title[0] ? state->display_title : "-",
                  sizeof(rmi->callback_filename));
        if (state->hls && state->aac)
            rmi->filelib_info.m_extension = m_(".aac");
        rc = filelib_start(rmi, &state->writer, ti);
        if (rc != SR_SUCCESS)
            return rc;
        state->writer.m_started = GET_INDIVIDUAL_TRACKS(rmi->prefs->flags);
        state->writer_open = TRUE;
    }
    if (state->writer.m_started) {
        rc = filelib_write_track(&state->writer, (char*)data, length);
        if (rc != SR_SUCCESS)
            return rc;
    }
    rc = filelib_write_show(rmi, (char*)data, length);
    if (rc == SR_SUCCESS)
        callback_put_data(rmi, length);
    return rc;
}

static error_code decrypt_segment(RIP_MANAGER_INFO* rmi, Segment* segment, GByteArray** body) {
    struct stream_capture* state = rmi->capture;
    if (!segment->key)
        return SR_SUCCESS;
    if (!state->cached_key_url || strcmp(state->cached_key_url, segment->key)) {
        GByteArray* key = NULL;
        error_code rc = fetch(rmi, segment->key, 16, &key, NULL);
        if (rc != SR_SUCCESS)
            return rc;
        if (key->len != 16) {
            g_byte_array_unref(key);
            return invalid("AES-128 key must be exactly 16 bytes");
        }
        memcpy(state->cached_key, key->data, 16);
        g_byte_array_unref(key);
        g_free(state->cached_key_url);
        state->cached_key_url = g_strdup(segment->key);
    }
    unsigned char iv[16] = {0};
    if (segment->explicit_iv)
        memcpy(iv, segment->iv, 16);
    else
        for (guint i = 0; i < 8; i++)
            iv[15 - i] = (unsigned char)(segment->sequence >> (i * 8));
    GByteArray* plain = g_byte_array_sized_new((*body)->len + 16);
    g_byte_array_set_size(plain, (*body)->len + 16);
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int first = 0, last = 0;
    BOOL valid = ctx && EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, state->cached_key, iv) == 1 &&
        EVP_DecryptUpdate(ctx, plain->data, &first, (*body)->data, (*body)->len) == 1 &&
        EVP_DecryptFinal_ex(ctx, plain->data + first, &last) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!valid) {
        g_byte_array_unref(plain);
        return invalid("AES-128 segment decryption failed");
    }
    g_byte_array_set_size(plain, first + last);
    g_byte_array_unref(*body);
    *body = plain;
    return SR_SUCCESS;
}

static error_code rip_hls(RIP_MANAGER_INFO* rmi) {
    struct stream_capture* state = rmi->capture;
    while (state->index < state->segments->len) {
        Segment* segment = g_ptr_array_index(state->segments, state->index);
        if (state->have_sequence && segment->sequence <= state->last_sequence) {
            state->index++;
            continue;
        }
        GByteArray* body = NULL;
        error_code rc = fetch(rmi, segment->url, FETCH_LIMIT, &body, NULL);
        if (rc != SR_SUCCESS)
            return rc;
        rc = decrypt_segment(rmi, segment, &body);
        if (rc == SR_SUCCESS) {
            guint offset = 0;
            if (state->aac && body->len >= 10 && !memcmp(body->data, "ID3", 3)) {
                for (guint i = 6; i < 10; i++) {
                    if (body->data[i] & 0x80) {
                        offset = body->len;
                        break;
                    }
                    offset = (offset << 7) | body->data[i];
                }
                if (offset <= body->len)
                    offset += 10;
            }
            BOOL valid = state->aac ? offset + 2 <= body->len && body->data[offset] == 0xff && (body->data[offset + 1] & 0xf6) == 0xf0 : body->len >= 188 && body->len % 188 == 0;
            if (valid && !state->aac)
                for (guint i = 0; i < body->len; i += 188)
                    if (body->data[i] != 0x47) {
                        valid = FALSE;
                        break;
                    }
            if (!valid)
                rc = invalid("expected MPEG-TS (.ts) or packed ADTS AAC (.aac) segments");
        }
        if (rc == SR_SUCCESS)
            rc = write_capture(rmi, body->data, body->len);
        g_byte_array_unref(body);
        if (rc != SR_SUCCESS)
            return rc;
        state->duration_ms += (guint64)(segment->duration * 1000);
        state->writer.m_duration_ms = state->duration_ms;
        state->writer.m_duration_known = TRUE;
        rmi->callback_duration_ms = state->duration_ms;
        rmi->callback_duration_known = TRUE;
        state->last_sequence = segment->sequence;
        state->have_sequence = TRUE;
        state->index++;
        return SR_SUCCESS;
    }
    if (state->endlist) {
        state->finished = TRUE;
        return SR_SUCCESS_END_OF_STREAM;
    }
    /* Polling and cancellation share the same short wait granularity as reconnects. */
    while (g_get_monotonic_time() < state->poll_at) {
        if (g_atomic_int_get(&rmi->stopped))
            return SR_ERROR_ABORT_PIPE_SIGNALLED;
        g_usleep(100000);
    }
    GByteArray* body = NULL;
    char* effective = NULL;
    error_code rc = fetch(rmi, state->playlist, PLAYLIST_LIMIT, &body, &effective);
    if (rc == SR_SUCCESS)
        rc = load_playlist(rmi, body, effective);
    g_free(effective);
    return rc;
}

static error_code rip_flac(RIP_MANAGER_INFO* rmi) {
    struct stream_capture* state = rmi->capture;
    if (!state->remaining) {
        if (!state->writer_open)
            return invalid("missing FLAC stream header");
        state->finished = TRUE;
        return SR_SUCCESS_END_OF_STREAM;
    }
    if (rmi->meta_interval > 0 && !state->icy_remaining) {
        unsigned char length;
        int got = socklib_recvall(rmi, &rmi->stream_sock, (char*)&length, 1, rmi->prefs->timeout);
        if (got != 1)
            return got < 0 ? got : SR_ERROR_RECV_FAILED;
        if (state->remaining > 0)
            state->remaining--;
        char metadata[4081] = {0};
        int bytes = length * 16;
        got = socklib_recvall(rmi, &rmi->stream_sock, metadata, bytes, rmi->prefs->timeout);
        if (got != bytes)
            return got < 0 ? got : SR_ERROR_RECV_FAILED;
        if (state->remaining > 0) {
            state->remaining -= bytes;
            if (state->remaining < 0)
                return SR_ERROR_RECV_FAILED;
        }
        if (g_str_has_prefix(metadata, "StreamTitle='")) {
            char *title = metadata + 13, *end = strstr(title, "';");
            if (end) {
                *end = 0;
                g_strstrip(title);
                mchar decoded[MAX_TRACK_LEN];
                gstring_from_string(rmi, decoded, MAX_TRACK_LEN, title, CODESET_METADATA);
                filelib_log_metadata(rmi, decoded);
                g_strlcpy(state->display_title, decoded, sizeof(state->display_title));
                g_strlcpy(rmi->callback_filename, decoded[0] ? decoded : "-",
                          sizeof(rmi->callback_filename));
            }
        }
        state->icy_remaining = rmi->meta_interval;
        return SR_SUCCESS;
    }
    unsigned char data[16384];
    int count = sizeof(data);
    if (state->remaining > 0)
        count = MIN((gint64)count, state->remaining);
    if (rmi->meta_interval > 0)
        count = MIN(count, state->icy_remaining);
    int got = socklib_recv_some(rmi, &rmi->stream_sock, (char*)data, count, rmi->prefs->timeout);
    if (got < 0)
        return got;
    if (!got) {
        if (rmi->stream_sock.chunked && rmi->stream_sock.chunk_done) {
            state->finished = TRUE;
            return SR_SUCCESS_END_OF_STREAM;
        }
        return SR_ERROR_RECV_FAILED;
    }
    if (state->remaining > 0)
        state->remaining -= got;
    if (rmi->meta_interval > 0)
        state->icy_remaining -= got;
    if (state->ogg) {
        error_code rc = ogg_flac_output_write(state->ogg, data, got);
        update_ogg_duration(rmi);
        if (rc != SR_SUCCESS)
            state->writer.m_cleanup_blocked = TRUE;
        return rc;
    }
    guint offset = 0;
    if (!state->writer_open) {
        guint count = MIN((guint)got, 4 - state->prefix_size);
        memcpy(state->prefix + state->prefix_size, data, count);
        state->prefix_size += count;
        if (state->prefix_size < 4)
            return SR_SUCCESS;
        if (memcmp(state->prefix, rmi->flac_in_ogg ? "OggS" : "fLaC", 4))
            return invalid("missing FLAC stream header");
        error_code rc = write_capture(rmi, state->prefix, 4);
        if (rc != SR_SUCCESS)
            return rc;
        offset = count;
    }
    return offset < (guint)got ? write_capture(rmi, data + offset, got - offset) : SR_SUCCESS;
}

error_code stream_capture_rip(RIP_MANAGER_INFO* rmi) {
    struct stream_capture* state = rmi->capture;
    error_code rc = state->hls ? rip_hls(rmi) : rip_flac(rmi);
    if (rc == SR_SUCCESS_END_OF_STREAM) {
        error_code final_rc = finish_ogg(rmi);
        if (final_rc != SR_SUCCESS)
            return final_rc;
    }
    if (rc == SR_SUCCESS_END_OF_STREAM && state->writer.m_started) {
        rc = filelib_close(rmi, &state->writer);
        state->writer.m_started = FALSE;
        if (rc == SR_SUCCESS)
            rc = filelib_rename_to_complete(rmi, &state->writer);
        filelib_cleanup_incomplete(rmi, &state->writer);
        if (rc == SR_SUCCESS)
            rc = SR_SUCCESS_END_OF_STREAM;
    }
    return rc;
}

BOOL stream_capture_is_hls(RIP_MANAGER_INFO* rmi) {
    return rmi->capture && rmi->capture->hls;
}

int stream_capture_file_type(RIP_MANAGER_INFO* rmi) {
    return stream_capture_is_hls(rmi) && rmi->capture->aac ? CONTENT_TYPE_AAC : rmi->http_info.content_type;
}

void stream_capture_destroy(RIP_MANAGER_INFO* rmi) {
    struct stream_capture* state = rmi->capture;
    if (!state)
        return;
    error_code final_rc = finish_ogg(rmi);
    if (final_rc != SR_SUCCESS)
        callback_post_error(rmi, final_rc);
    if (state->writer.m_started) {
        error_code rc = filelib_close(rmi, &state->writer);
        if (rc != SR_SUCCESS)
            callback_post_error(rmi, rc);
        filelib_cleanup_incomplete(rmi, &state->writer);
    }
    if (state->segments)
        g_ptr_array_unref(state->segments);
    ogg_flac_output_free(state->ogg);
    g_free(state->playlist);
    g_free(state->cached_key_url);
    g_free(state);
    rmi->capture = NULL;
}
