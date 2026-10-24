#include "console_output.h"
#include <string.h>
#ifdef WIN32
#include <windows.h>
#include <io.h>
#include <stdint.h>
#endif

/* Keep UTF-8 in pipes and files; bypass CRT code-page conversion on Windows consoles. */
int
sr_console_vprintf(FILE *stream, const char *format, va_list args)
{
    char *text = g_strdup_vprintf(format, args);
    size_t length = strlen(text);
    int result = -1;
#ifdef WIN32
    HANDLE handle = (HANDLE)(intptr_t)_get_osfhandle(_fileno(stream));
    DWORD mode;
    if (handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode)) {
        char *valid = g_utf8_make_valid(text, -1);
        GString *lines = g_string_new(NULL);
        gunichar2 *wide;
        glong count;
        glong offset = 0;
        const char *cursor;
        /* Console writes do not perform the CRT's LF-to-CRLF translation. */
        for (cursor = valid; *cursor; cursor++) {
            if (*cursor == '\n' && (cursor == valid || cursor[-1] != '\r'))
                g_string_append_c(lines, '\r');
            g_string_append_c(lines, *cursor);
        }
        wide = g_utf8_to_utf16(lines->str, -1, NULL, &count, NULL);
        while (offset < count) {
            DWORD written;
            DWORD chunk = (DWORD)MIN(count - offset, 16384);
            if (offset + chunk < count && wide[offset + chunk - 1] >= 0xd800
                && wide[offset + chunk - 1] <= 0xdbff)
                chunk--;
            if (!WriteConsoleW(handle, wide + offset, chunk, &written, NULL) || !written)
                break;
            offset += written;
        }
        if (offset == count)
            result = (int)length;
        g_free(wide);
        g_string_free(lines, TRUE);
        g_free(valid);
    } else
#endif
    {
        if (fwrite(text, 1, length, stream) == length && fflush(stream) == 0)
            result = (int)length;
    }
    g_free(text);
    return result;
}

int
sr_console_printf(FILE *stream, const char *format, ...)
{
    int result;
    va_list args;
    va_start(args, format);
    result = sr_console_vprintf(stream, format, args);
    va_end(args);
    return result;
}
