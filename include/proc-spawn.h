/*
 * proc.h - Process spawning API, Linux only.
 *
 * Requires:
 *   - Linux >= 5.18 (pidfd_spawn)
 *   - glibc >= 2.36 (pidfd_spawn, P_PIDFD, pidfd_send_signal)
 *   - _GNU_SOURCE
 *
 * Falls back to posix_spawnp + waitid(P_PID) on ENOSYS.
 * Both variants use PATH lookup so cfg->path need not be an absolute path.
 */

#pragma once


#include <spawn.h>
#include <stdbool.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>

/* -------------------------------------------------------------------------
 * Stdio disposition
 * ------------------------------------------------------------------------- */

typedef enum {
    PROC_STDIO_INHERIT  = 0,
    PROC_STDIO_PIPE,
    PROC_STDIO_DEVNULL,
} proc_stdio_mode_t;

/* -------------------------------------------------------------------------
 * Spawn attributes callback
 *
 * Called after posix_spawnattr_setflags() has been applied, giving the
 * caller a chance to call additional setters (e.g. posix_spawnattr_setsigmask,
 * posix_spawnattr_setpgroup, posix_spawnattr_setschedparam, ...) on the
 * same posix_spawnattr_t that will be passed to spawn.
 *
 * proc.c manages the lifetime of the posix_spawnattr_t — do not init or
 * destroy it inside the callback.
 *
 * Return 0 on success, negative errno on error.
 * Pass NULL in proc_config_t if not needed.
 * ------------------------------------------------------------------------- */

typedef int (*proc_spawnattr_fn)(posix_spawnattr_t *attr, void *userdata);

/* -------------------------------------------------------------------------
 * File actions callback
 *
 * Called after the internal pipe/devnull actions have been added, giving
 * the caller a chance to append their own adddup2/addclose/addopen entries
 * to the same posix_spawn_file_actions_t that will be passed to spawn.
 *
 * Return 0 on success, negative errno on error.
 * Pass NULL in proc_config_t if not needed.
 * ------------------------------------------------------------------------- */

typedef int (*proc_file_actions_fn)(posix_spawn_file_actions_t *fa,
                                    void                       *userdata);

/* -------------------------------------------------------------------------
 * Spawn configuration
 * ------------------------------------------------------------------------- */

typedef struct {
    const char             *path;   /* filename or full path; looked up via PATH */
    char * const           *argv;
    char * const           *envp;   /* NULL = inherit */

    proc_stdio_mode_t       stdin_mode;
    proc_stdio_mode_t       stdout_mode;
    proc_stdio_mode_t       stderr_mode;

    /*
     * spawnattr_flags: any combination of POSIX_SPAWN_* flags accepted by
     * posix_spawnattr_setflags(3).  0 = no flags (default).
     *
     * spawnattr_cb: optional callback for additional attribute setters that
     * require their own calls (e.g. posix_spawnattr_setsigmask).  Called
     * after setflags().  proc.c manages the posix_spawnattr_t lifetime.
     *
     * If both are zero/NULL, no posix_spawnattr_t is initialised and NULL
     * is passed directly to the spawn call.
     */
    short                   spawnattr_flags;
    proc_spawnattr_fn       spawnattr_cb;
    void                   *spawnattr_userdata;

    /*
     * Optional callback to append extra file actions.
     * Called after internal stdio actions are registered.
     * fa is already initialised; do not init or destroy it.
     */
    proc_file_actions_fn    file_actions_cb;
    void                   *file_actions_userdata;
} proc_config_t;

/* -------------------------------------------------------------------------
 * Process handle
 * ------------------------------------------------------------------------- */

typedef struct {
    enum {
        PROC_HANDLE_PID   = 0,
        PROC_HANDLE_PIDFD = 1,
    } type;

    union {
        pid_t pid;
        int   pidfd;
    };

    int            stdin_write;    /* write end → child stdin,  -1 if not piped */
    int            stdout_read;    /* read  end ← child stdout, -1 if not piped */
    int            stderr_read;    /* read  end ← child stderr, -1 if not piped */

    /*
     * rusage is zeroed at spawn and filled by proc_wait() on successful
     * reap.  Not valid until proc_wait() returns 1.
     */
    struct rusage  rusage;
} proc_handle_t;

/* -------------------------------------------------------------------------
 * Wait result
 * ------------------------------------------------------------------------- */

typedef struct {
    siginfo_t info;
    bool      exited;
    bool      signaled;
    int       status;
} proc_wait_result_t;

/* -------------------------------------------------------------------------
 * API
 *
 * All functions return 0 on success or a negative errno value on failure
 * (e.g. -EINVAL, -EIO, -ENOMEM).  Never -1 with errno set.
 *
 * proc_wait() - wait for child to change state.
 *   flags: extra waitid(2) flags ORed with WEXITED (e.g. WNOHANG, WSTOPPED,
 *          WCONTINUED, WNOWAIT).  Pass 0 for a plain blocking wait.
 *   Returns 1 if reaped, 0 if still running (WNOHANG), negative on error.
 * proc_close() returns 0 on success, negative errno if any fd failed to close.
 *   On close error the handle is still fully released; the error is advisory
 *   (e.g. -EIO on a pipe that lost its reader before all data was flushed).
 * ------------------------------------------------------------------------- */

int proc_spawn(const proc_config_t *cfg, proc_handle_t *out);
int proc_wait(proc_handle_t *h, int flags, proc_wait_result_t *res);
int proc_kill(const proc_handle_t *h, int sig);
int proc_close(proc_handle_t *h);

/* -------------------------------------------------------------------------
 * Automatic cleanup via __attribute__((cleanup))
 *
 * PROC_AUTO declares a proc_handle_t that is automatically closed when it
 * goes out of scope, whether by normal return, goto, or early return on error.
 *
 *   PROC_AUTO(h);
 *   if (proc_spawn(&cfg, &h) < 0) ...   // h closed on any exit path
 *
 * Close errors from the cleanup are silently discarded — if you need to
 * handle them, call proc_close() explicitly before leaving scope.
 *
 * PROC_AUTO_WAIT additionally blocks until the child exits on scope exit if
 * the handle is still live.  Useful for fire-and-forget scopes:
 *
 *   {
 *       PROC_AUTO_WAIT(h);
 *       proc_spawn(&cfg, &h);
 *       // ... drain pipes ...
 *   }  // child waited and handle closed here automatically
 *
 * If you need the exit status, use PROC_AUTO and call proc_wait() yourself.
 *
 * Note: PROC_AUTO_WAIT performs a blocking wait on scope exit.  Drain or
 * close any pipes before leaving the scope to avoid deadlock.
 * ------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * proc_handle_live() - true if the handle refers to a running child.
 *
 * Returns false on a zeroed/closed handle (pidfd == -1 or pid <= 0).
 * Safe to call at any time, including before proc_spawn() or after
 * proc_close().
 * ------------------------------------------------------------------------- */

static inline bool proc_handle_live(const proc_handle_t *h)
{
    return (h->type == PROC_HANDLE_PIDFD)
           ? (h->pidfd >= 0)
           : (h->pid   >  0);
}

/* -------------------------------------------------------------------------
 * Handle declaration macros
 *
 * PROC_HANDLE(name) - declare a proc_handle_t with no automatic cleanup.
 *   Use when lifetime is managed manually or stored in a struct.
 *
 * PROC_AUTO(name) - declare a proc_handle_t that is automatically closed
 *   when it goes out of scope (normal return, goto, or early return).
 *   Close errors are silently discarded — call proc_close() explicitly
 *   if you need them.
 *
 * PROC_AUTO_WAIT(name) - like PROC_AUTO but also blocks until the child
 *   exits on scope exit if the handle is still live.  Drain or close any
 *   pipes before leaving scope to avoid deadlock.  Use when you don't need
 *   the exit status; otherwise use PROC_AUTO + explicit proc_wait().
 * ------------------------------------------------------------------------- */

#define PROC_HANDLE_INIT                                                \
    (proc_handle_t){                                                    \
        .type        = PROC_HANDLE_PID,                                 \
        .pid         = -1,                                              \
        .stdin_write = -1,                                              \
        .stdout_read = -1,                                              \
        .stderr_read = -1,                                              \
    }

#define PROC_HANDLE(name)                                               \
    proc_handle_t name = PROC_HANDLE_INIT

static inline void _proc_auto_close(proc_handle_t *h)
{
    proc_close(h);
}

static inline void _proc_auto_wait_and_close(proc_handle_t *h)
{
    if (proc_handle_live(h)) {
        proc_wait_result_t _res;
        proc_wait(h, 0, &_res);
    }
    proc_close(h);
}

#define PROC_AUTO(name)                                                 \
    proc_handle_t name                                                  \
        __attribute__((cleanup(_proc_auto_close))) = PROC_HANDLE_INIT

#define PROC_AUTO_WAIT(name)                                            \
    proc_handle_t name                                                  \
        __attribute__((cleanup(_proc_auto_wait_and_close))) = PROC_HANDLE_INIT
