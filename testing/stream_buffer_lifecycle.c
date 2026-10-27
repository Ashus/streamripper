/* Detached recording chunks must remain owned on receive failure. */
#include "ripstream.h"
#include "ripstream_mp3.h"
#include "ripstream_ogg.h"
#include "cbuf3.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAILED at line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

int main(void) {
    char data[32] = {0};
    GList second = {data, NULL, NULL};
    GList first = {data, &second, NULL};
    Cbuf3 buffer = {0};
    buffer.chunk_size = sizeof(data);
    Cbuf3_pointer start = {&first, 7};
    Cbuf3_pointer end = {&second, 11};
    u_long distance = 0;
    CHECK(cbuf3_pointer_subtract(&buffer, &distance, &start, &end) == SR_SUCCESS);
    CHECK(distance == 36);
    end.node = &first;
    CHECK(cbuf3_pointer_subtract(&buffer, &distance, &start, &end) == SR_SUCCESS);
    CHECK(distance == 4);
    end.offset = 6;
    CHECK(cbuf3_pointer_subtract(&buffer, &distance, &start, &end) == SR_ERROR_BUFFER_TOO_SMALL);
    for (int cycle = 0; cycle < 100; cycle++) {
        for (int ogg = 0; ogg < 2; ogg++) {
            RIP_MANAGER_INFO rmi = {0};
            STREAM_PREFS prefs = {0};
            rmi.prefs = &prefs;
            rmi.meta_interval = NO_META_INTERVAL;
            rmi.http_info.content_type = ogg ? CONTENT_TYPE_OGG : CONTENT_TYPE_MP3;
            CHECK(ripstream_init(&rmi) == SR_SUCCESS);
            rmi.stream_sock.closed = TRUE;
            error_code rc = ogg ? ripstream_ogg_rip(&rmi) : ripstream_mp3_rip(&rmi);
            CHECK(rc == SR_ERROR_SOCKET_CLOSED);
            CHECK(rmi.cbuf3.free_list != NULL && rmi.cbuf3.buf != NULL);
            CHECK(g_queue_get_length(rmi.cbuf3.free_list) + g_queue_get_length(rmi.cbuf3.buf) == rmi.cbuf3.num_chunks);
            ripstream_destroy(&rmi);
            CHECK(rmi.getbuffer == NULL && rmi.cbuf3.free_list == NULL && rmi.cbuf3.buf == NULL);
        }
    }
    puts("PASS: 100 MP3/Ogg receive-failure lifecycles return detached chunks and release their buffers");
    return 0;
}
