#ifndef SR_CONSOLE_OUTPUT_H
#define SR_CONSOLE_OUTPUT_H

#include <stdio.h>
#include <stdarg.h>
#include <glib.h>

int sr_console_vprintf(FILE *stream, const char *format, va_list args);
int sr_console_printf(FILE *stream, const char *format, ...) G_GNUC_PRINTF(2, 3);
void sr_console_status(FILE *stream, const char *status, const char *title, const char *detail);

#endif
