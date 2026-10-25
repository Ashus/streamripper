#include "console_output.h"
#include <string.h>
#ifdef WIN32
#include <windows.h>
#include <io.h>
#include <stdint.h>
#else
#include <unistd.h>
#include <sys/ioctl.h>
#endif

static guint64 console_write_generation;

static int console_columns(FILE *stream)
{
#ifdef WIN32
    HANDLE handle = (HANDLE)(intptr_t)_get_osfhandle(_fileno(stream));
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(handle, &info))
        return info.srWindow.Right - info.srWindow.Left + 1;
#else
    struct winsize size;
    if (isatty(fileno(stream)) && ioctl(fileno(stream), TIOCGWINSZ, &size) == 0 && size.ws_col)
        return size.ws_col;
#endif
    return 0;
}

/* Count display columns rather than bytes; combining marks occupy no column. */
static char *fit_columns(const char *text, int budget, int *columns)
{
    char *valid = g_utf8_make_valid(text, -1);
    GString *result = g_string_new(NULL);
    const char *p;
    *columns = 0;
    for (p = valid; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        int width;
        if (g_unichar_iscntrl(c)) c = ' ';
        width = g_unichar_iszerowidth(c) ? 0 : g_unichar_iswide(c) ? 2 : 1;
        if (*columns + width > budget) break;
        g_string_append_unichar(result, c);
        *columns += width;
    }
    g_free(valid);
    return g_string_free(result, FALSE);
}

void sr_console_status(FILE *stream, const char *status, const char *title, const char *detail)
{
    static char *previous_line;
    static FILE *previous_stream;
    static int previous_columns;
    static guint64 previous_generation;
    int columns = console_columns(stream), used;
    char *suffix = detail ? g_strdup_printf(" [%8s]", detail) : g_strdup("");
    int budget = columns ? MAX(0, columns - 1 - 17 - (int)strlen(suffix)) : 50;
    char *name = fit_columns(title, budget, &used);
    char *line = g_strdup_printf("[%14s] %s%s", status, name, suffix);
    /* Data callbacks can repeat within the same displayed second. */
    if (previous_stream == stream && previous_columns == columns &&
        previous_generation == console_write_generation &&
        g_strcmp0(previous_line, line) == 0) {
        g_free(line);
        g_free(name);
        g_free(suffix);
        return;
    }
    if (columns) {
        char *fitted = fit_columns(line, columns - 1, &used);
        /* Erase through the last column without triggering automatic wrapping. */
#ifdef WIN32
        HANDLE handle = (HANDLE)(intptr_t)_get_osfhandle(_fileno(stream));
        CONSOLE_SCREEN_BUFFER_INFO info;
        if (GetConsoleScreenBufferInfo(handle, &info)) {
            DWORD cleared;
            COORD start = {info.srWindow.Left, info.dwCursorPosition.Y};
            FillConsoleOutputCharacterW(handle, L' ', columns, start, &cleared);
            SetConsoleCursorPosition(handle, start);
            sr_console_printf(stream, "%s", fitted);
            SetConsoleCursorPosition(handle, start);
        }
#else
        sr_console_printf(stream, "\r%s\033[K\r", fitted);
#endif
        g_free(fitted);
    } else {
        sr_console_printf(stream, "%s\r", line);
    }
    g_free(previous_line);
    previous_line = line;
    previous_stream = stream;
    previous_columns = columns;
    previous_generation = console_write_generation;
    g_free(name);
    g_free(suffix);
}

/* Keep UTF-8 in pipes and files; bypass CRT code-page conversion on Windows consoles. */
int
sr_console_vprintf(FILE *stream, const char *format, va_list args)
{
    /* A message or track separator requires the next status to be redrawn. */
    console_write_generation++;
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
