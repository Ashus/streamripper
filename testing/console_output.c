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
        fclose(stream);
    }
    sr_console_printf(stdout, "%s\n", title);
    sr_console_printf(stderr, "%s\n", title);
    return 0;
}
