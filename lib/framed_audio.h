/* Native MP3/ADTS file framing and finalization, GPL-2.0-or-later. */
#ifndef FRAMED_AUDIO_H
#define FRAMED_AUDIO_H
#include "srtypes.h"
#include "errors.h"

typedef struct framed_audio FramedAudio;
error_code framed_audio_init(FramedAudio **output, FHANDLE file, int type,
                             gint64 tlen_offset);
error_code framed_audio_write(FramedAudio *output, const char *data, size_t size);
error_code framed_audio_finish(FramedAudio **output);
gboolean framed_audio_adts_boundary(const unsigned char *data, size_t size,
                                    size_t target, size_t *boundary);
#endif
