/* Best-effort audio file preallocation, GPL-2.0-or-later. */
#ifndef FILE_PREALLOCATION_H
#define FILE_PREALLOCATION_H

#include <glib.h>
#include "sr_compat.h"

#define AUDIO_ALLOCATION_STEP (2 * 1024 * 1024)

/* One zero-initialized state per open audio file; never share between handles. */
typedef struct {
    guint64 reserved;
    gboolean disabled;
} AudioFileAllocation;

void audio_file_reserve(FHANDLE file, AudioFileAllocation *allocation, gsize size);
void audio_file_release(FHANDLE file, AudioFileAllocation *allocation);

#endif
