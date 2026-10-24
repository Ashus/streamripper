/* Recording directory ownership, GPL-2.0-or-later. */
#include "recording_lock.h"
#include "sr_compat.h"
#include <glib/gstdio.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifndef WIN32
#include <sys/file.h>
#endif

typedef struct {
    char *path;
    FHANDLE file;
} RecordingLock;

static void close_lock(RecordingLock *lock)
{
#ifdef WIN32
    CloseHandle(lock->file);
#else
    close(lock->file);
#endif
    g_free(lock->path);
    g_free(lock);
}

gboolean recording_lock_acquire(GList **locks, const char *incomplete_directory)
{
    char *filename = g_build_filename(incomplete_directory, ".lock", NULL);
    char *path = g_canonicalize_filename(filename, NULL);
    RecordingLock *lock;
    char pid[32] = {0};
    gboolean acquired = FALSE;
    g_free(filename);
    for (GList *item = *locks; item; item = item->next) {
        RecordingLock *owned = item->data;
#ifdef WIN32
        if (!g_ascii_strcasecmp(owned->path, path)) {
#else
        if (!strcmp(owned->path, path)) {
#endif
            g_free(path);
            return TRUE;
        }
    }
    if (g_mkdir_with_parents(incomplete_directory, 0777)) {
        fprintf(stderr, "Unable to create recording lock directory: %s (%s)\n",
                incomplete_directory, g_strerror(errno));
        g_free(path);
        return FALSE;
    }
    lock = g_new0(RecordingLock, 1);
    lock->path = path;
#ifdef WIN32
    gunichar2 *wide = g_utf8_to_utf16(path, -1, NULL, NULL, NULL);
    lock->file = wide ? CreateFileW((LPCWSTR)wide, GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE;
    g_free(wide);
#else
    lock->file = g_open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0666);
#endif
    if (lock->file == INVALID_FHANDLE) {
        fprintf(stderr, "Unable to open recording lock: %s\n", path);
        g_free(lock->path);
        g_free(lock);
        return FALSE;
    }
#ifdef WIN32
    /* Lock beyond EOF so other processes can still read the diagnostic PID. */
    OVERLAPPED region = {0};
    DWORD count = 0;
    region.Offset = 4096;
    acquired = LockFileEx(lock->file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                          0, 1, 0, &region);
    if (!acquired && GetLastError() != ERROR_LOCK_VIOLATION) {
        fprintf(stderr, "Unable to lock recording directory: %s (Windows error %lu)\n",
                path, (unsigned long)GetLastError());
        close_lock(lock);
        return FALSE;
    }
    if (!acquired) ReadFile(lock->file, pid, sizeof(pid) - 1, &count, NULL);
#else
    int result;
    do { result = flock(lock->file, LOCK_EX | LOCK_NB); } while (result && errno == EINTR);
    acquired = result == 0;
    if (!acquired && errno != EWOULDBLOCK && errno != EAGAIN) {
        fprintf(stderr, "Unable to lock recording directory: %s (%s)\n", path, g_strerror(errno));
        close_lock(lock);
        return FALSE;
    }
    if (!acquired) {
        ssize_t count = read(lock->file, pid, sizeof(pid) - 1);
        if (count < 0) pid[0] = 0;
    }
#endif
    if (!acquired) {
        g_strstrip(pid);
        for (const char *p = pid; *p; p++) {
            if (!g_ascii_isdigit(*p)) { pid[0] = 0; break; }
        }
        fprintf(stderr, "Output directory is already in use (PID %s): %s\n",
                pid[0] ? pid : "unknown", path);
        close_lock(lock);
        return FALSE;
    }
#ifdef WIN32
    g_snprintf(pid, sizeof(pid), "%lu\n", (unsigned long)GetCurrentProcessId());
    acquired = WriteFile(lock->file, pid, (DWORD)strlen(pid), &count, NULL) && count == strlen(pid);
    if (acquired) acquired = SetEndOfFile(lock->file);
#else
    g_snprintf(pid, sizeof(pid), "%ld\n", (long)getpid());
    acquired = ftruncate(lock->file, 0) == 0;
    if (acquired) acquired = write(lock->file, pid, strlen(pid)) == (ssize_t)strlen(pid);
#endif
    if (!acquired) {
        fprintf(stderr, "Unable to write recording lock PID: %s\n", path);
        close_lock(lock);
        return FALSE;
    }
    *locks = g_list_prepend(*locks, lock);
    return TRUE;
}

void recording_locks_release(GList **locks)
{
    g_list_free_full(*locks, (GDestroyNotify)close_lock);
    *locks = NULL;
}
