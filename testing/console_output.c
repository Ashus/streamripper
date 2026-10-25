#include "console_output.h"
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <stdint.h>
#include <locale.h>
#include <string.h>

int main(void)
{
    HANDLE buffer;
    FILE *stream;
    WCHAR actual[128];
    DWORD count;
    COORD start = {0, 0};
    unsigned int pages[] = {1250, 65001};
    size_t i;
    const char *title = "Mirar - N\xc3\xa4r Solen G\xc3\xa5r Ned";
    const WCHAR expected[] = L"Mirar - N\u00e4r Solen G\u00e5r Ned";
    {
        FILE *capture = tmpfile();
        long first;
        if (!capture) return 13;
        sr_console_status(capture, "ripping...", "Track", "00:01");
        first = ftell(capture);
        sr_console_status(capture, "ripping...", "Track", "00:01");
        if (ftell(capture) != first) return 14;
        sr_console_status(capture, "ripping...", "Track", "00:02");
        if (ftell(capture) <= first) return 15;
        first = ftell(capture);
        sr_console_status(capture, "filtered...", "Track", "00:02");
        if (ftell(capture) <= first) return 16;
        first = ftell(capture);
        sr_console_status(capture, "filtered...", "Next track", "00:02");
        if (ftell(capture) <= first) return 17;
        sr_console_printf(capture, "\n");
        first = ftell(capture);
        sr_console_status(capture, "filtered...", "Next track", "00:02");
        if (ftell(capture) <= first) return 18;
        fclose(capture);
    }
    setlocale(LC_ALL, ".1250");
    for (i = 0; i < G_N_ELEMENTS(pages); i++) {
        SetConsoleOutputCP(pages[i]);
        buffer = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
        if (buffer == INVALID_HANDLE_VALUE) return 1;
        stream = _fdopen(_open_osfhandle((intptr_t)buffer, _O_TEXT), "w");
        if (!stream) return 2;
        if (sr_console_printf(stream, "%s\n", title) < 0) return 3;
        if (!ReadConsoleOutputCharacterW(buffer, actual, G_N_ELEMENTS(expected) - 1,
            start, &count) || count != G_N_ELEMENTS(expected) - 1
            || memcmp(actual, expected, sizeof(expected) - sizeof(WCHAR))) return 4;
        {
            CONSOLE_SCREEN_BUFFER_INFO info;
            if (!GetConsoleScreenBufferInfo(buffer, &info)
                || info.dwCursorPosition.X != 0 || info.dwCursorPosition.Y != 1) return 5;
        }
        {
            COORD size = {240, 300};
            SMALL_RECT window = {0, 0, 119, 9};
            const char *long_title = "0123456789012345678901234567890123456789012345678901234567890123456789-ENDMARK";
            if (!SetConsoleScreenBufferSize(buffer, size) ||
                !SetConsoleWindowInfo(buffer, TRUE, &window) ||
                !SetConsoleCursorPosition(buffer, start)) return 6;
            sr_console_status(stream, "ripping...    ", long_title, "01:23");
            if (!ReadConsoleOutputCharacterW(buffer, actual, 120, start, &count)) return 7;
            actual[119] = 0;
            if (!wcsstr(actual, L"ENDMARK")) return 8;
            window.Right = 39;
            if (!SetConsoleWindowInfo(buffer, TRUE, &window)) return 9;
            sr_console_status(stream, "ripping...    ", "Short", "01:23");
            if (!ReadConsoleOutputCharacterW(buffer, actual, 40, start, &count)) return 10;
            for (size_t cell = 33; cell < 40; cell++) if (actual[cell] != L' ') return 11;
            {
                CONSOLE_SCREEN_BUFFER_INFO info;
                if (!GetConsoleScreenBufferInfo(buffer, &info) || info.dwCursorPosition.Y != 0) return 12;
            }
        }
        fclose(stream);
    }
    sr_console_printf(stdout, "%s\n", title);
    sr_console_printf(stderr, "%s\n", title);
    return 0;
}
