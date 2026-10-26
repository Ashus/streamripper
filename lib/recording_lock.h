/* Recording directory ownership, GPL-2.0-or-later. */
#ifndef RECORDING_LOCK_H
#define RECORDING_LOCK_H

#include <glib.h>

gboolean recording_lock_acquire(GList** locks, const char* incomplete_directory);
void recording_locks_release(GList** locks);

#endif
