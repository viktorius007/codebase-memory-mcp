/*
 * service.h — Stable daemon rendezvous and build-compatibility policy.
 *
 * The rendezvous key deliberately excludes executable path, release version,
 * build fingerprint, cache directory, and ABI values. Every stateful CBM
 * frontend for one OS account must meet at one endpoint; the HELLO comparison
 * then either admits the exact build or returns an explicit conflict.
 */
#ifndef CBM_DAEMON_SERVICE_H
#define CBM_DAEMON_SERVICE_H

#include "daemon/daemon.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CBM_DAEMON_VERSION_TEXT_SIZE 64U
#define CBM_DAEMON_BUILD_FINGERPRINT_SIZE 65U
#define CBM_DAEMON_CONFLICT_MESSAGE_SIZE 512U

typedef struct {
    const char *semantic_version;
    const char *build_fingerprint;
    /* SHA-256 of the canonical cache-root path. It is intentionally excluded
     * from the stable HELLO envelope, but the account-wide lifetime cohort
     * compares it before any daemon/CLI work can begin. NULL means an internal
     * test/legacy identity with no cache namespace. */
    const char *cache_fingerprint;
    uint32_t protocol_abi;
    uint32_t store_abi;
    uint32_t feature_abi;
} cbm_daemon_build_identity_t;

typedef enum {
    CBM_DAEMON_HELLO_INVALID = 0,
    CBM_DAEMON_HELLO_COMPATIBLE,
    CBM_DAEMON_HELLO_VERSION_CONFLICT,
    CBM_DAEMON_HELLO_BUILD_CONFLICT,
    CBM_DAEMON_HELLO_PROTOCOL_ABI_CONFLICT,
    CBM_DAEMON_HELLO_STORE_ABI_CONFLICT,
    CBM_DAEMON_HELLO_FEATURE_ABI_CONFLICT,
    CBM_DAEMON_HELLO_CACHE_CONFLICT,
} cbm_daemon_hello_status_t;

typedef struct {
    cbm_daemon_hello_status_t status;
    char active_version[CBM_DAEMON_VERSION_TEXT_SIZE];
    char active_build_fingerprint[CBM_DAEMON_BUILD_FINGERPRINT_SIZE];
    char requested_version[CBM_DAEMON_VERSION_TEXT_SIZE];
    char requested_build_fingerprint[CBM_DAEMON_BUILD_FINGERPRINT_SIZE];
    char active_cache_fingerprint[CBM_DAEMON_BUILD_FINGERPRINT_SIZE];
    char requested_cache_fingerprint[CBM_DAEMON_BUILD_FINGERPRINT_SIZE];
} cbm_daemon_conflict_t;

/* Stable product key. OS-account isolation is supplied by the IPC runtime
 * directory / current-user ACL, never by caller-provided identity text. */
bool cbm_daemon_rendezvous_key(char out[CBM_DAEMON_KEY_SIZE]);

/* SHA-256 of the exact executable bytes, encoded as 64 lowercase hex
 * characters plus NUL. This is captured once at process startup. */
bool cbm_daemon_build_fingerprint_file(const char *path,
                                       char out[CBM_DAEMON_BUILD_FINGERPRINT_SIZE]);

/* Remember computed fingerprints under <cache_dir>/build-fingerprints so the
 * next process of the same executable skips the whole-image hash. An entry is
 * keyed by the hashed file's kernel identity: device, inode, size, mtime and
 * ctime (nanoseconds). The memo answers only for files on a writable mount of
 * a filesystem whose kernel stamps ctime on every write, truncate, rename-over
 * or metadata change and lets no caller set it (APFS; ext4, xfs, btrfs,
 * tmpfs). There a replaced binary (also in place at the same path and inode)
 * misses the memo and is hashed again. Files anywhere else — read-only and
 * image filesystems, FAT/exFAT, network and FUSE mounts — are always hashed,
 * since a different file there can present the same key. An entry is
 * recorded only once its ctime is a few seconds old, so a write in the same
 * clock tick as the recorded state cannot follow it unnoticed. The memo
 * directory is read and written only when owned by the caller with mode
 * 0700. Call once during startup, before any thread hashes; NULL disables.
 * POSIX only: Windows always hashes. */
bool cbm_daemon_build_fingerprint_memo_configure(const char *cache_dir);

cbm_daemon_hello_status_t cbm_daemon_hello_compare(const cbm_daemon_build_identity_t *active,
                                                   const cbm_daemon_build_identity_t *requested,
                                                   cbm_daemon_conflict_t *conflict_out);

bool cbm_daemon_conflict_format(const cbm_daemon_conflict_t *conflict, char *out, size_t out_size);

/* Append one secret-free NDJSON conflict event. A persistent owner-only
 * <log_path>.lock serializes validation, rotation, and append across daemon
 * processes; cap_bytes rotates one complete prior generation to <log_path>.1
 * before appending a record that would cross the cap. */
bool cbm_daemon_conflict_log_append(const char *log_path, const cbm_daemon_conflict_t *conflict,
                                    size_t cap_bytes);

#endif /* CBM_DAEMON_SERVICE_H */
