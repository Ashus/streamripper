/* HTTP parser security regressions, GPL-2.0-or-later. */
#include "http.h"
#include "socklib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAILED at line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

typedef struct {
    unsigned char before[32];
    SR_HTTP_HEADER header;
    unsigned char after[32];
} GuardedHeader;

static void parse_header(char* text, GuardedHeader* result) {
    memset(result, 0xa5, sizeof(*result));
    CHECK(http_parse_sc_header("http://127.0.0.1/test", text, &result->header) == SR_SUCCESS);
    for (size_t i = 0; i < sizeof(result->before); i++) {
        CHECK(result->before[i] == 0xa5);
        CHECK(result->after[i] == 0xa5);
    }
    CHECK(memchr(result->header.http_location, 0, sizeof(result->header.http_location)));
    CHECK(memchr(result->header.server, 0, sizeof(result->header.server)));
    CHECK(memchr(result->header.icy_name, 0, sizeof(result->header.icy_name)));
    CHECK(memchr(result->header.icy_url, 0, sizeof(result->header.icy_url)));
    CHECK(memchr(result->header.icy_genre, 0, sizeof(result->header.icy_genre)));
}

static void server_versions(void) {
    const struct {
        const char* server;
        const char* expected;
    } cases[] = {
        {"Server: SHOUTcast Server/1.9.8<", "SHOUTcast/1.9.8"},
        {"Server: SHOUTcast Server/<", "SHOUTcast/"},
        {"Server: SHOUTcast Server/", "SHOUTcast/"},
        {"Server: SHOUTcast Server/\r\nX-Secret: must-not-be-copied", "SHOUTcast/"},
        {"Server: SHOUTcast", "SHOUTcast/"},
        {"X-Test: icecast version 1.3<", "icecast/1.3"},
        {"X-Test: icecast version <", "icecast/"},
        {"X-Test: icecast version ", "icecast/"},
        {"X-Test: icecast version \r\nX-Secret: must-not-be-copied", "icecast/"},
        {"X-Test: icecast", "icecast/"}
    };
    for (size_t i = 0; i < G_N_ELEMENTS(cases); i++) {
        GuardedHeader result;
        char* text = g_strdup_printf("HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\n%s\r\n\r\n", cases[i].server);
        parse_header(text, &result);
        CHECK(strcmp(result.header.server, cases[i].expected) == 0);
        g_free(text);
    }
}

static void oversized_headers(void) {
    const size_t lengths[] = {0, 1, 63, 64, 510, 511, 512, 1023, 1024, 4095, 4096, 8191, 20000};
    for (size_t i = 0; i < G_N_ELEMENTS(lengths); i++) {
        GuardedHeader result;
        char* value = g_strnfill(lengths[i], 'A');
        char* text = g_strdup_printf("HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\n"
                                     "Location: %s\r\nServer: %s\r\nicy-name: %s\r\nicy-url: %s\r\n\r\n",
                                     value, value, value, value);
        parse_header(text, &result);
        CHECK(strlen(result.header.http_location) <= MIN(lengths[i], sizeof(result.header.http_location) - 1));
        CHECK(strlen(result.header.server) <= MIN(lengths[i], sizeof(result.header.server) - 1));
        CHECK(strlen(result.header.icy_name) <= MIN(lengths[i], sizeof(result.header.icy_name) - 1));
        CHECK(strlen(result.header.icy_url) <= MIN(lengths[i], sizeof(result.header.icy_url) - 1));
        CHECK(strspn(result.header.http_location, "A") == strlen(result.header.http_location));
        CHECK(strspn(result.header.server, "A") == strlen(result.header.server));
        g_free(text);

        text = g_strdup_printf("HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\nServer: Zwitterion v%s<\r\n\r\n", value);
        parse_header(text, &result);
        CHECK(g_str_has_prefix(result.header.server, "Zwitterion v"));
        g_free(text);

        const char* prefixes[] = {"Server: SHOUTcast Server/", "X-Test: icecast version "};
        for (size_t p = 0; p < G_N_ELEMENTS(prefixes); p++) {
            text = g_strdup_printf("HTTP/1.0 200 OK\r\nContent-Type: audio/mpeg\r\n%s%s<\r\n\r\n", prefixes[p], value);
            parse_header(text, &result);
            CHECK(strlen(result.header.server) == strlen(p ? "icecast/" : "SHOUTcast/") + MIN(lengths[i], 63));
            g_free(text);
        }
        g_free(value);
    }
}

static void oversized_urls(void) {
    const size_t lengths[] = {511, 512, 1023, 1024, 8191, 8192, 20000};
    const struct {
        const char* prefix;
        const char* suffix;
    } patterns[] = {
        {"http://", ":pass@127.0.0.1/test"},
        {"http://user:", "@127.0.0.1/test"},
        {"http://", "/test"},
        {"http://127.0.0.1/", ""}
    };
    for (size_t i = 0; i < G_N_ELEMENTS(lengths); i++) {
        char* value = g_strnfill(lengths[i], 'A');
        for (size_t p = 0; p < G_N_ELEMENTS(patterns); p++) {
            struct {
                unsigned char before[32];
                char request[MAX_HEADER_LEN];
                unsigned char after[32];
            } result;
            memset(&result, 0xa5, sizeof(result));
            char* url = g_strconcat(patterns[p].prefix, value, patterns[p].suffix, NULL);
            error_code rc = http_construct_sc_request(url, NULL, result.request, "Security regression");
            CHECK(rc == SR_SUCCESS || rc == SR_ERROR_INVALID_URL);
            for (size_t g = 0; g < sizeof(result.before); g++) {
                CHECK(result.before[g] == 0xa5);
                CHECK(result.after[g] == 0xa5);
            }
            if (rc == SR_SUCCESS)
                CHECK(memchr(result.request, 0, sizeof(result.request)));
            g_free(url);
        }
        g_free(value);
    }
}

static int connect_once(const char* url) {
    STREAM_PREFS prefs = {0};
    RIP_MANAGER_INFO rmi = {0};
    HSOCKET sock = {0};
    SR_HTTP_HEADER response;
    prefs.timeout = 2;
    prefs.ip_version = 4;
    rmi.prefs = &prefs;
    CHECK(socklib_init() == SR_SUCCESS);
    error_code rc = http_sc_connect(&rmi, &sock, url, NULL, &response, "Security regression", NULL);
    socklib_close(&sock);
    socklib_free_tls(&sock);
    socklib_cleanup();
    printf("HTTP result: %d\n", rc);
    return rc == SR_SUCCESS ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc == 3 && !strcmp(argv[1], "--connect"))
        return connect_once(argv[2]);
    CHECK(argc == 1);
    for (int cycle = 0; cycle < 100; cycle++) {
        server_versions();
        oversized_headers();
        oversized_urls();
    }
    puts("PASS: malformed server versions, bounded headers/URLs and historical HTTP CVE regressions (100 cycles)");
    return 0;
}
