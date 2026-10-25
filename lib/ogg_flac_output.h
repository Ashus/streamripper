/* Ogg FLAC recording finalization, GPL-2.0-or-later. */
#ifndef OGG_FLAC_OUTPUT_H
#define OGG_FLAC_OUTPUT_H
#include "errors.h"
typedef struct ogg_flac_output OggFlacOutput;
typedef error_code (*OggFlacEmit)(void *, const unsigned char *, gsize);
typedef error_code (*OggFlacRewrite)(void *, guint64, const unsigned char *, gsize);
OggFlacOutput *ogg_flac_output_new(OggFlacEmit emit, OggFlacRewrite rewrite, void *context);
error_code ogg_flac_output_write(OggFlacOutput *output, const unsigned char *data, gsize size);
error_code ogg_flac_output_finish(OggFlacOutput *output);
BOOL ogg_flac_output_duration(OggFlacOutput *output, guint64 *milliseconds);
void ogg_flac_output_free(OggFlacOutput *output);
#endif
