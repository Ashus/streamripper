/* Focused Windows regression checks, GPL-2.0-or-later. */
#include "srtypes.h"
#include "parse.h"
#include "mchar.h"
#include "prefs.h"
#include "rip_manager.h"
#include "ripstream_wav.h"
#include "http.h"
#include "socklib.h"
#include "findsep.h"
#include "track_info.h"
#include "framed_audio.h"
#include "filelib.h"
#include <stdio.h>
#include <stdlib.h>
#include <psapi.h>
#include <glib/gstdio.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAILED at line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

static SIZE_T private_bytes(void)
{
    PROCESS_MEMORY_COUNTERS_EX mem = {0};
    CHECK(GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&mem, sizeof(mem)));
    return mem.PrivateUsage;
}

static void callback(RIP_MANAGER_INFO *rmi, int message, void *data)
{
    (void)rmi; (void)message; (void)data;
}

static int decode_file(const char *input, const char *output, int block_size)
{
    FILE *fp = fopen(input, "rb");
    char *buffer = g_malloc(block_size);
    FHANDLE file = CreateFileA(output, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    WavOutput *wav = NULL;
    error_code rc;
    size_t count;
    CHECK(fp && file != INVALID_HANDLE_VALUE);
    rc = wav_output_init(&wav, file);
    while (rc == SR_SUCCESS && (count = fread(buffer, 1, block_size, fp)) != 0)
        rc = wav_output_write(wav, buffer, count);
    error_code final_rc = wav_output_finish(&wav);
    CHECK(wav == NULL);
    fclose(fp);
    CloseHandle(file);
    g_free(buffer);
    return rc == SR_SUCCESS && final_rc == SR_SUCCESS ? 0 : 1;
}

static void parser_cycle(RIP_MANAGER_INFO *rmi)
{
    TRACK_INFO ti = {0};
    init_metadata_parser(rmi, NULL);
    for (int i = 0; i < 100; i++) {
        g_strlcpy(ti.raw_metadata, i % 2 ? "Artist - Track MP3PRO" : "Plain title", sizeof(ti.raw_metadata));
        parse_metadata(rmi, &ti);
        CHECK(ti.have_track_info);
        CHECK(!strstr(ti.title, "MP3PRO"));
    }
    parser_free(rmi);
    CHECK(rmi->parse_rules == NULL);
}

int main(int argc, char **argv)
{
    if (argc == 5 && !strcmp(argv[1], "--proxy-connect-cycles")) {
        RIP_MANAGER_INFO rmi = {0};
        STREAM_PREFS prefs = {0};
        HSOCKET connection = {0};
        SR_HTTP_HEADER response;
        DWORD before, after;
        SIZE_T bytes_before = 0;
        prefs.timeout = 2;
        g_strlcpy(prefs.tls_ca_file, argv[4], sizeof(prefs.tls_ca_file));
        rmi.prefs = &prefs;
        CHECK(socklib_init() == SR_SUCCESS);
        for (int iteration = 0; iteration < 13; iteration++) {
            error_code rc = http_sc_connect(&rmi, &connection, argv[2], argv[3], &response,
                                            "Proxy regression", NULL);
            socklib_close(&connection);
            socklib_free_tls(&connection);
            CHECK(rc == SR_SUCCESS);
            CHECK(connection.tls == NULL && connection.proxy_tls == NULL);
            if (!iteration) {
                CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
                bytes_before = private_bytes();
            }
        }
        CHECK(GetProcessHandleCount(GetCurrentProcess(), &after));
        CHECK(after <= before + 2);
        CHECK(private_bytes() < bytes_before + 32 * 1024 * 1024);
        socklib_cleanup();
        puts("PASS: repeated CONNECT/TLS cycles release sockets and both SSL layers");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--ipv6-requests")) {
        char request[MAX_HEADER_LEN + MAX_HOST_LEN + SR_MAX_PATH];
        CHECK(http_construct_sc_request("http://[::1]:8080/radio?q=a%26b", NULL, request, "test") == SR_SUCCESS);
        CHECK(strstr(request, "GET /radio?q=a%26b HTTP/1.1\r\n"));
        CHECK(strstr(request, "\r\nHost: [::1]:8080\r\n"));
        CHECK(http_construct_sc_request("http://[2001:db8::1]/radio", "http://[::1]:9000", request, "test") == SR_SUCCESS);
        CHECK(strstr(request, "GET http://[2001:db8::1]:80/radio HTTP/1.1\r\n"));
        CHECK(strstr(request, "\r\nHost: [2001:db8::1]:80\r\n"));
        CHECK(http_construct_sc_request("https://[::1]/", NULL, request, "test") == SR_SUCCESS);
        CHECK(strstr(request, "\r\nHost: [::1]:443\r\n"));
        CHECK(http_construct_sc_request("http://[fe80::1%2512]:8080/", NULL, request, "test") == SR_SUCCESS);
        CHECK(strstr(request, "\r\nHost: [fe80::1]:8080\r\n"));
        CHECK(http_construct_page_request("http://[::1]:8080/page", TRUE, request) == SR_SUCCESS);
        CHECK(strstr(request, "GET http://[::1]:8080/page HTTP/1.1\r\n"));
        CHECK(strstr(request, "\r\nHost: [::1]:8080\r\n"));
        CHECK(strstr(request, "Chrome/152.0.0.0 Safari/537.36\r\n"));
        CHECK(strstr(request, "\r\nAccept-Encoding: identity\r\n"));
        CHECK(strstr(request, "\r\nConnection: close\r\n"));
        CHECK(!strstr(request, "MSIE") && !strstr(request, "gzip") && !strstr(request, "Keep-Alive"));
        puts("PASS: IPv6 request authorities, proxy forms, default ports and scope removal");
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "--ipv6-connect-cycles")) {
        RIP_MANAGER_INFO rmi = {0};
        DWORD before, after;
        CHECK(socklib_init() == SR_SUCCESS);
        for (int iteration = 0; iteration < 21; iteration++) {
            for (int port = 2; port <= 3; port++) {
                CHECK(socklib_open(&rmi.stream_sock, "localhost", atoi(argv[port]), NULL, 2, 0) == SR_SUCCESS);
                socklib_close(&rmi.stream_sock);
            }
            if (!iteration) CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
        }
        CHECK(GetProcessHandleCount(GetCurrentProcess(), &after));
        CHECK(after <= before + 2);
        socklib_cleanup();
        puts("PASS: repeated IPv4/IPv6 connections release racing socket handles");
        return 0;
    }
#if OGG_VORBIS_FOUND
    /* Granule timing is independent of byte count and chained sample rates. */
    {
        Writer writer = {0};
        unsigned char page[58] = {0};
        guint32 rate = GUINT32_TO_LE(44100);
        guint64 granule = GUINT64_TO_LE(88200);
        memcpy(page, "OggS", 4); page[5] = 2; page[14] = 1;
        page[26] = 1; page[27] = 30; page[28] = 1;
        memcpy(page + 29, "vorbis", 6); memcpy(page + 40, &rate, 4);
        filelib_update_ogg_duration(&writer, page, sizeof(page));
        CHECK(writer.m_duration_known && writer.m_duration_ms == 0);
        page[5] = 0; memcpy(page + 6, &granule, 8);
        filelib_update_ogg_duration(&writer, page, 27);
        CHECK(writer.m_duration_ms == 2000);
        page[5] = 2; page[14] = 2; memset(page + 6, 0, 8);
        rate = GUINT32_TO_LE(48000); memcpy(page + 40, &rate, 4);
        filelib_update_ogg_duration(&writer, page, sizeof(page));
        page[5] = 4; granule = GUINT64_TO_LE(48000); memcpy(page + 6, &granule, 8);
        filelib_update_ogg_duration(&writer, page, 27);
        CHECK(writer.m_duration_ms == 3000 && !writer.m_cleanup_blocked);
        page[5] = 2; page[28] = 0;
        filelib_update_ogg_duration(&writer, page, sizeof(page));
        CHECK(!writer.m_duration_known && writer.m_cleanup_blocked);
    }
#endif
    if (argc == 5 && !strcmp(argv[1], "--stress-frames")) {
        gchar *audio = NULL;
        gsize size;
        SIZE_T before = 0;
        DWORD initial_handles = 0, final_handles;
        FHANDLE file = CreateFileA(argv[4], GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        CHECK(file != INVALID_HANDLE_VALUE);
        CHECK(g_file_get_contents(argv[3], &audio, &size, NULL));
        for (int i = 0; i < 103; i++) {
            FramedAudio *output = NULL;
            CHECK(framed_audio_init(&output, file,
                  !strcmp(argv[2], "aac") ? CONTENT_TYPE_AAC : CONTENT_TYPE_MP3, -1) == SR_SUCCESS);
            CHECK(framed_audio_write(output, audio, size) == SR_SUCCESS);
            CHECK(framed_audio_finish(&output) == SR_SUCCESS);
            CHECK(output == NULL);
            if (i == 2) {
                before = private_bytes();
                GetProcessHandleCount(GetCurrentProcess(), &initial_handles);
            }
        }
        GetProcessHandleCount(GetCurrentProcess(), &final_handles);
        SIZE_T after = private_bytes();
        printf("Framing %s: 100 lifecycles, handle growth: %ld, private-byte growth: %lld\n",
               argv[2], (long)final_handles - (long)initial_handles,
               (long long)after - (long long)before);
        CHECK(final_handles <= initial_handles + 1);
        CHECK(after <= before + 4 * 1024 * 1024);
        CloseHandle(file); g_free(audio);
        return 0;
    }
    if (argc == 6 && (!strcmp(argv[1], "--frame-copy") || !strcmp(argv[1], "--frame-duration"))) {
        FILE *input = fopen(argv[3], "rb");
        FHANDLE file = CreateFileA(argv[4], GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        FramedAudio *output = NULL;
        size_t count, block = atoi(argv[5]);
        guint64 duration;
        char *buffer = g_malloc(block);
        CHECK(input && file != INVALID_HANDLE_VALUE && block);
        CHECK(framed_audio_init(&output, file,
              !strcmp(argv[2], "aac") ? CONTENT_TYPE_AAC : CONTENT_TYPE_MP3, -1) == SR_SUCCESS);
        while ((count = fread(buffer, 1, block, input)))
            CHECK(framed_audio_write(output, buffer, count) == SR_SUCCESS);
        CHECK(framed_audio_finish_duration(&output, &duration) == SR_SUCCESS);
        if (!strcmp(argv[1], "--frame-duration"))
            printf("%" G_GUINT64_FORMAT "\n", duration);
        CHECK(output == NULL);
        CHECK(framed_audio_finish(&output) == SR_SUCCESS);
        CloseHandle(file); fclose(input); g_free(buffer);
        return 0;
    }
    /* Identical parsed titles still need a boundary when selection changes. */
    TRACK_INFO excluded = {0}, accepted = {0};
    accepted.save_track = TRUE;
    CHECK(track_info_different(&excluded, &accepted));
    excluded.save_track = TRUE;
    CHECK(!track_info_different(&excluded, &accepted));

    if (argc == 3 && !strcmp(argv[1], "--silence")) {
        gchar *audio = NULL;
        gsize size;
        u_long start, end;
        CHECK(g_file_get_contents(argv[2], &audio, &size, NULL));
        CHECK(size > 16000);
        findsep_silence(audio, size, 0, 1000, 50, 0, 0, &start, &end);
        findsep_silence_2(audio, size, 0, 1000, 50, 0, 0, &start, &end);
        g_free(audio);
        puts("Both silence detectors completed.");
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "--decode"))
        return decode_file(argv[2], argv[3], atoi(argv[4]));
    if (argc == 4 && !strcmp(argv[1], "--stress-decode")) {
        DWORD initial_handles, final_handles;
        for (int i = 0; i < 3; i++) CHECK(decode_file(argv[2], argv[3], 997) == 0);
        SIZE_T initial = private_bytes();
        GetProcessHandleCount(GetCurrentProcess(), &initial_handles);
        for (int i = 0; i < 100; i++) CHECK(decode_file(argv[2], argv[3], 997) == 0);
        SIZE_T final = private_bytes();
        GetProcessHandleCount(GetCurrentProcess(), &final_handles);
        printf("WAV: 100 decoder lifecycles, handle growth: %ld, private-byte growth: %lld\n",
               (long)final_handles - (long)initial_handles, (long long)final - (long long)initial);
        CHECK(final_handles <= initial_handles);
        CHECK(final <= initial + 4 * 1024 * 1024);
        return 0;
    }
    STREAM_PREFS prefs = {0};
    RIP_MANAGER_INFO rmi = {0};
    DWORD handles_before, handles_after;
    SIZE_T before, after;
    prefs_get_stream_prefs(&prefs, "stream defaults");
    CHECK(!prefs.wav && !prefs.http10 && !prefs.tls_insecure);
    rmi.prefs = &prefs;
    register_codesets(&rmi, &prefs.cs_opt);
    for (int i = 0; i < 10; i++) parser_cycle(&rmi);
    before = private_bytes();
    for (int i = 0; i < 1000; i++) parser_cycle(&rmi);
    after = private_bytes();
    printf("Parser: 100000 metadata changes, private-byte growth: %lld\n",
           (long long)after - (long long)before);
    CHECK(after <= before + 8 * 1024 * 1024);

    /* Malformed final custom rule must leave a valid sentinel. */
    gchar *rule_name = NULL;
    int fd = g_file_open_tmp("streamripper-rules-XXXXXX", &rule_name, NULL);
    CHECK(fd >= 0);
    _close(fd);
    CHECK(g_file_set_contents(rule_name, "m/unfinished", -1, NULL));
    init_metadata_parser(&rmi, rule_name);
    TRACK_INFO ti = {0};
    g_strlcpy(ti.raw_metadata, "No match", sizeof(ti.raw_metadata));
    parse_metadata(&rmi, &ti);
    CHECK(!strcmp(ti.title, "No match"));
    parser_free(&rmi);
    {
        Writer writer = {0};
        g_strlcpy(writer.m_incomplete_filename, rule_name, sizeof(writer.m_incomplete_filename));
        writer.m_is_incomplete = TRUE;
        prefs.incomplete_min_duration_ms = 1000;
        filelib_cleanup_incomplete(&rmi, &writer);
        CHECK(g_file_test(rule_name, G_FILE_TEST_EXISTS)); /* Unknown duration. */
        writer.m_duration_known = TRUE;
        writer.m_duration_ms = 1000;
        filelib_cleanup_incomplete(&rmi, &writer);
        CHECK(g_file_test(rule_name, G_FILE_TEST_EXISTS)); /* Exactly the minimum. */
        writer.m_duration_ms = 999;
        writer.m_cleanup_blocked = TRUE;
        filelib_cleanup_incomplete(&rmi, &writer);
        CHECK(g_file_test(rule_name, G_FILE_TEST_EXISTS)); /* Failed finalization. */
        writer.m_cleanup_blocked = FALSE;
        writer.m_is_incomplete = FALSE;
        filelib_cleanup_incomplete(&rmi, &writer);
        CHECK(g_file_test(rule_name, G_FILE_TEST_EXISTS)); /* Completed recording. */
        writer.m_is_incomplete = TRUE;
        filelib_cleanup_incomplete(&rmi, &writer);
        CHECK(!g_file_test(rule_name, G_FILE_TEST_EXISTS));
        prefs.incomplete_min_duration_ms = 0;
    }
    g_free(rule_name);

    /* Missing status delimiter must be rejected without dereferencing NULL. */
    SR_HTTP_HEADER info;
    char header[] = "HTTP/1.1\r\n\r\n";
    CHECK(http_parse_sc_header("http://localhost/", header, &info) != SR_SUCCESS);
    memset(&info, 0, sizeof(info));
    char tiny[8];
    CHECK(http_construct_sc_response(&info, tiny, sizeof(tiny), 1) == SR_ERROR_BUFFER_TOO_SMALL);

    rip_manager_init();
    GetProcessHandleCount(GetCurrentProcess(), &handles_before);
    before = private_bytes();
    for (int i = 0; i < 200; i++) {
        RIP_MANAGER_INFO *manager = NULL;
        g_strlcpy(prefs.url, "invalid://test", sizeof(prefs.url));
        CHECK(rip_manager_start(&manager, &prefs, callback) == SR_SUCCESS);
        rip_manager_stop(manager);
        rip_manager_stop(manager); /* idempotent */
        free(manager);
    }
    after = private_bytes();
    GetProcessHandleCount(GetCurrentProcess(), &handles_after);
    rip_manager_cleanup();
    printf("Manager: 200 failed connections, handle growth: %ld, private-byte growth: %lld\n",
           (long)handles_after - (long)handles_before, (long long)after - (long long)before);
    CHECK(handles_after <= handles_before + 2);
    CHECK(after <= before + 8 * 1024 * 1024);
    puts("Core regression checks passed.");
    return 0;
}
