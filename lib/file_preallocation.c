/* Best-effort audio file preallocation, GPL-2.0-or-later. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#include "file_preallocation.h"
#include <errno.h>
#include <stdio.h>
#if defined(__linux__)
#include <linux/falloc.h>
#endif

void audio_file_reserve(FHANDLE file, AudioFileAllocation* allocation, gsize size) {
#if defined(SR_MODERN_WINDOWS) || defined(__linux__)
    guint64 end, rounded;
    gint64 position, length;
    if (!size || allocation->disabled)
        return;
#ifdef SR_MODERN_WINDOWS
    LARGE_INTEGER zero = {0}, current, eof;
    if (!SetFilePointerEx(file, zero, &current, FILE_CURRENT))
        goto disable;
    position = current.QuadPart;
#else
    position = lseek(file, 0, SEEK_CUR);
    if (position < 0)
        goto disable;
#endif
    if (size > G_MAXINT64 || (guint64)position > G_MAXINT64 - (guint64)size)
        goto disable;
    end = (guint64)position + size;
    if (end <= allocation->reserved)
        return;

    /* Never shrink an existing file when increasing its allocation. */
#ifdef SR_MODERN_WINDOWS
    if (!GetFileSizeEx(file, &eof))
        goto disable;
    length = eof.QuadPart;
#else
    struct stat info;
    if (fstat(file, &info))
        goto disable;
    length = info.st_size;
#endif
    end = MAX(end, (guint64)length);
    if (end > G_MAXINT64 - (AUDIO_ALLOCATION_STEP - 1))
        goto disable;
    rounded = (end + AUDIO_ALLOCATION_STEP - 1) / AUDIO_ALLOCATION_STEP * AUDIO_ALLOCATION_STEP;
#ifdef SR_MODERN_WINDOWS
    FILE_ALLOCATION_INFO info;
    info.AllocationSize.QuadPart = rounded;
    if (!SetFileInformationByHandle(file, FileAllocationInfo, &info, sizeof(info)))
        goto disable;
#else
    int result;
    do {
        result = fallocate(file, FALLOC_FL_KEEP_SIZE, allocation->reserved,
                           rounded - allocation->reserved);
    }
    while (result && errno == EINTR);
    if (result)
        goto disable;
#endif
    allocation->reserved = rounded;
    return;
disable:
    /* Unsupported filesystems and low free space must not stop recording. */
    allocation->disabled = TRUE;
#else
    (void)file;
    (void)allocation;
    (void)size;
#endif
}

void audio_file_release(FHANDLE file, AudioFileAllocation* allocation) {
    if (!allocation->reserved)
        return;
#ifdef SR_MODERN_WINDOWS
    LARGE_INTEGER length;
    FILE_ALLOCATION_INFO info;
    if (GetFileSizeEx(file, &length)) {
        info.AllocationSize = length;
        if (!SetFileInformationByHandle(file, FileAllocationInfo, &info, sizeof(info)))
            fprintf(stderr, "Unable to release unused audio file allocation.\n");
    }
#elif defined(__linux__)
    struct stat info;
    int result = -1;
    if (!fstat(file, &info)) {
        /* Truncating to the current EOF releases reservations beyond EOF. */
        do { result = ftruncate(file, info.st_size); }
        while (result && errno == EINTR);
    }
    if (result)
        fprintf(stderr, "Unable to release unused audio file allocation.\n");
#else
    (void)file;
#endif
    allocation->reserved = 0;
}
