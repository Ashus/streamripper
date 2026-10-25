/* AAC silence-search integration driver, GPL-2.0-or-later. */
#include "findsep.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    gchar *data = NULL;
    gsize size;
    u_long previous = 123, next = 456;
    if (argc != 7 || !g_file_get_contents(argv[1], &data, &size, NULL)) return 1;
    error_code rc = findsep_silence_aac(data, size, atol(argv[2]), atol(argv[3]),
        atol(argv[4]), atol(argv[5]), atol(argv[6]), &previous, &next);
    printf("%d %lu %lu\n", rc, previous, next);
    g_free(data);
    return 0;
}
