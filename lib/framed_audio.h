/* Native MP3/ADTS file framing and finalization, GPL-2.0-or-later. */
#ifndef FRAMED_AUDIO_H
#define FRAMED_AUDIO_H
#include "srtypes.h"
#include "errors.h"

typedef struct framed_audio FramedAudio;
void framed_audio_counter_init(FramedAudio** counter, int type);
void framed_audio_counter_free(FramedAudio** counter);
gboolean framed_audio_duration(const FramedAudio* counter, guint64* duration_ms);
error_code framed_audio_init(FramedAudio** output, FHANDLE file, int type,
                             gint64 tlen_offset);
error_code framed_audio_write(FramedAudio* output, const char* data, size_t size);
error_code framed_audio_finish(FramedAudio** output);
error_code framed_audio_finish_duration(FramedAudio** output, guint64* duration_ms);
gboolean framed_audio_adts_boundary(const unsigned char* data, size_t size,
                                    size_t target, size_t* boundary);
#endif
