/*
 * service_internal.h — Internal daemon service helpers and test hooks.
 *
 * Production callers outside the daemon implementation must use
 * daemon/service.h.  Runtime identity verification hashes an already-open,
 * kernel-bound file handle through this internal boundary; the test hook lets
 * concurrency tests establish an exact interleaving without scheduler luck.
 */
#ifndef CBM_DAEMON_SERVICE_INTERNAL_H
#define CBM_DAEMON_SERVICE_INTERNAL_H

#include "daemon/service.h"

#include <stdbool.h>
#include <stdint.h>

/* Hash an already-open regular file without closing it. native_file is an int
 * file descriptor on POSIX and a HANDLE cast through uintptr_t on Windows.
 * Callers use this after binding the handle to kernel process-image metadata,
 * avoiding a second lookup through a replaceable pathname. */
bool cbm_daemon_build_fingerprint_native_file(uintptr_t native_file,
                                              char out[CBM_DAEMON_BUILD_FINGERPRINT_SIZE]);

/* cbm_daemon_build_fingerprint_memo_configure with an explicit settle window:
 * an entry is recorded only when the file's ctime is at least settle_seconds
 * older than the wall clock. Production uses
 * CBM_DAEMON_BUILD_FINGERPRINT_MEMO_SETTLE_SECONDS. */
#define CBM_DAEMON_BUILD_FINGERPRINT_MEMO_SETTLE_SECONDS 2
bool cbm_daemon_build_fingerprint_memo_configure_settle(const char *cache_dir,
                                                        int64_t settle_seconds);

/* Whether the memo may answer for a file on a volume of filesystem type
 * fs_type (the platform's name: "apfs", "ext4", "xfs", "btrfs", "tmpfs"; NULL
 * when unknown), mounted read_only or not, whose ctime has nanosecond part
 * ctime_nsec. True only where every change to the file's bytes is guaranteed
 * to change its key: a writable mount of a filesystem that stamps ctime from
 * the kernel clock and lets no caller set it. A read-only mount can only
 * change by being replaced with another volume, which may present the same
 * device, inode and stored times; a ctime with no sub-second part was not
 * stamped by such a clock (coarse filesystem or normalised image). */
bool cbm_daemon_build_fingerprint_memo_volume_trusted(const char *fs_type, bool read_only,
                                                      long ctime_nsec);

/* The same decision for the file at path as it stands now. */
bool cbm_daemon_build_fingerprint_memo_file_trusted(const char *path);

typedef enum {
    CBM_DAEMON_CONFLICT_LOG_BEFORE_SERIALIZATION_LOCK = 1,
    CBM_DAEMON_CONFLICT_LOG_AFTER_SERIALIZATION_LOCK,
} cbm_daemon_conflict_log_test_stage_t;

typedef void (*cbm_daemon_conflict_log_test_hook_fn)(void *context,
                                                     cbm_daemon_conflict_log_test_stage_t stage);

void cbm_daemon_conflict_log_set_test_hook(cbm_daemon_conflict_log_test_hook_fn hook,
                                           void *context);

#endif /* CBM_DAEMON_SERVICE_INTERNAL_H */
