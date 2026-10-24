/* Cross-platform allocation/EOF regression checks, GPL-2.0-or-later. */
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#include "file_preallocation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef WIN32
#include <io.h>
#endif

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAILED at line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

static FHANDLE handle(FILE *stream)
{
#ifdef WIN32
    return (HANDLE)_get_osfhandle(_fileno(stream));
#else
    return fileno(stream);
#endif
}

static gint64 file_size(FHANDLE file, gboolean allocated)
{
#ifdef WIN32
    FILE_STANDARD_INFO info;
    CHECK(GetFileInformationByHandleEx(file, FileStandardInfo, &info, sizeof(info)));
    return allocated ? info.AllocationSize.QuadPart : info.EndOfFile.QuadPart;
#else
    struct stat info;
    CHECK(fstat(file, &info) == 0);
    return allocated ? (gint64)info.st_blocks * 512 : info.st_size;
#endif
}

static void seek_to(FHANDLE file, gint64 offset)
{
#ifdef WIN32
    LARGE_INTEGER position;
    position.QuadPart = offset;
    CHECK(SetFilePointerEx(file, position, NULL, FILE_BEGIN));
#else
    CHECK(lseek(file, offset, SEEK_SET) == offset);
#endif
}

static void write_bytes(FHANDLE file, const void *data, size_t size)
{
#ifdef WIN32
    DWORD count;
    CHECK(WriteFile(file, data, (DWORD)size, &count, NULL) && count == size);
#else
    CHECK(write(file, data, size) == (ssize_t)size);
#endif
}

int main(void)
{
    AudioFileAllocation first = {0}, second = {0}, failed = {0};
    char first_name[128], second_name[128], buffer[100];
    FILE *a, *b;
    FHANDLE one, two;
#ifdef WIN32
    unsigned long pid = GetCurrentProcessId();
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    snprintf(first_name, sizeof(first_name), "preallocation-%lu-a.tmp", pid);
    snprintf(second_name, sizeof(second_name), "preallocation-%lu-b.tmp", pid);
    a = fopen(first_name, "w+b");
    b = fopen(second_name, "w+b");
    CHECK(a && b);
    one = handle(a); two = handle(b);

    audio_file_reserve(one, &first, 1);
    CHECK(!first.disabled && first.reserved == AUDIO_ALLOCATION_STEP);
    CHECK(file_size(one, FALSE) == 0);
    CHECK(file_size(one, TRUE) >= AUDIO_ALLOCATION_STEP);
    audio_file_reserve(two, &second, 3);
    write_bytes(two, "end", 3);
    memset(buffer, 'A', sizeof(buffer));
    write_bytes(one, buffer, sizeof(buffer));
    audio_file_reserve(one, &first, AUDIO_ALLOCATION_STEP - sizeof(buffer));
    CHECK(first.reserved == AUDIO_ALLOCATION_STEP);

    /* Cross a boundary while another output retains its independent reserve. */
    seek_to(one, AUDIO_ALLOCATION_STEP - 50);
    audio_file_reserve(one, &first, sizeof(buffer));
    CHECK(first.reserved == 2 * AUDIO_ALLOCATION_STEP);
    CHECK(second.reserved == AUDIO_ALLOCATION_STEP);
    CHECK(file_size(one, FALSE) == sizeof(buffer));
    write_bytes(one, buffer, sizeof(buffer));
    CHECK(file_size(one, FALSE) == AUDIO_ALLOCATION_STEP + 50);

    /* Header rewrites must never truncate the recorded tail. */
    seek_to(one, 0);
    audio_file_reserve(one, &first, 4);
    write_bytes(one, "HEAD", 4);
    CHECK(first.reserved == 2 * AUDIO_ALLOCATION_STEP);
    audio_file_release(one, &first);
    CHECK(file_size(one, FALSE) == AUDIO_ALLOCATION_STEP + 50);
    CHECK(file_size(one, TRUE) < 2 * AUDIO_ALLOCATION_STEP);
    audio_file_release(one, &first); /* Idempotent. */
    audio_file_release(two, &second);
    CHECK(file_size(two, FALSE) == 3 && file_size(two, TRUE) < AUDIO_ALLOCATION_STEP);
    CHECK(fclose(a) == 0 && fclose(b) == 0);

    a = fopen(first_name, "rb");
    CHECK(a && fread(buffer, 1, 4, a) == 4 && !memcmp(buffer, "HEAD", 4));
    CHECK(fseek(a, AUDIO_ALLOCATION_STEP - 50, SEEK_SET) == 0);
    CHECK(fread(buffer, 1, sizeof(buffer), a) == sizeof(buffer));
    for (size_t i = 0; i < sizeof(buffer); i++) CHECK(buffer[i] == 'A');
    CHECK(fgetc(a) == EOF);
    CHECK(fclose(a) == 0);

    /* Reservation failure is sticky, but ordinary writes remain possible. */
    audio_file_reserve(INVALID_FHANDLE, &failed, 1);
    CHECK(failed.disabled && !failed.reserved);
    b = fopen(second_name, "ab");
    CHECK(b);
    audio_file_reserve(handle(b), &failed, 1);
    CHECK(fwrite("!", 1, 1, b) == 1);
    CHECK(fclose(b) == 0);
    b = fopen(second_name, "rb");
    CHECK(b && fread(buffer, 1, 4, b) == 4 && !memcmp(buffer, "end!", 4));
    CHECK(fclose(b) == 0);
    CHECK(remove(first_name) == 0 && remove(second_name) == 0);
    puts("PASS: 2 MiB allocation, unchanged EOF, independent outputs, header rewrites, release, and fallback");
    return 0;
}
