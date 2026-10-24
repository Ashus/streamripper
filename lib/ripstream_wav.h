/* Streaming MP3 to 16-bit PCM WAV output, GPL-2.0-or-later. */
#ifndef RIPSTREAM_WAV_H
#define RIPSTREAM_WAV_H
#include "errors.h"

typedef struct wav_output WavOutput;
error_code wav_output_init(WavOutput **output, FHANDLE file);
error_code wav_output_write(WavOutput *output, const char *data, size_t size);
error_code wav_output_finish(WavOutput **output);
#endif
