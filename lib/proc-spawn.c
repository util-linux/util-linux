/*
 * proc.c - Linux-only process spawning implementation.
 *
 * All functions return 0 on success or a negative errno value on failure.
 *
 * close() error handling follows POSIX 2024:
 *   - EINTR and EINPROGRESS are ignored: on Linux the fd is always closed
 *     regardless, so retrying would operate on a recycled fd.
 *   - All other errors (e.g. EIO) are captured and returned to the caller.
 *   - The fd field is always set to -1 after the close attempt, even on error,
 *     since the fd is no longer valid regardless of what close() returned.
 *
 * cc -std=gnu11 -Wall -Wextra -D_GNU_SOURCE -c proc.c
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "pidfd-utils.h"
#include "proc-spawn.h"

/* -------------------------------------------------------------------------
 * close_fd()
 *
 * Closes *fd and sets it to -1 unconditionally.
 *
 * Returns 0 on success, negative errno on failure, with EINTR and
 * EINPROGRESS silently ignored per POSIX 2024 / Linux semantics.
 * ------------------------------------------------------------------------- */

static int close_fd(int *fd)
{
    if (*fd < 0)
        return 0;

    int ret = 0;

    if (close(*fd) < 0) {
        int e = errno;
        if (e != EINTR && e != EINPROGRESS)
            ret = -e;
        /* fd is closed on Linux regardless of EINTR/EINPROGRESS;
         * for other errors the fd is also invalid — do not retry. */
    }

    *fd = -1;
    return ret;
}

/* -------------------------------------------------------------------------
 * handle_cleanup()
 *
 * Closes all open fds in the handle.  All fds are attempted regardless of
 * intermediate failures; the last non-zero error is returned.
 * ------------------------------------------------------------------------- */

static int handle_cleanup(proc_handle_t *h)
{
    int err = 0;
    int r;

    r = close_fd(&h->stdin_write);
    if (r != 0) err = r;

    r = close_fd(&h->stdout_read);
    if (r != 0) err = r;

    r = close_fd(&h->stderr_read);
    if (r != 0) err = r;

    if (h->type == PROC_HANDLE_PIDFD) {
        r = close_fd(&h->pidfd);
        if (r != 0) err = r;
    }

    return err;
}

/* -------------------------------------------------------------------------
 * Pipe setup
 *
 * Returns 0 on success, negative errno on failure.
 *
 * pipe2(O_CLOEXEC) marks both ends close-on-exec atomically.
 * adddup2() onto the target fd (0/1/2) produces a new descriptor without
 * CLOEXEC in the child; the original child-side fd closes itself on exec.
 * We deliberately omit addclose() for the spare child-side ends: since they
 * already carry CLOEXEC, exec cleans them up, and it avoids the fd collision
 * where pipe2 returns fd N that the caller's callback dup2's something onto,
 * and our addclose(N) would then clobber it.
 *
 * posix_spawn_file_actions_* return the error code directly (not via errno),
 * so we negate their return value.  pipe2 uses errno normally.
 * ------------------------------------------------------------------------- */

typedef struct {
    int child_stdin_r;
    int child_stdout_w;
    int child_stderr_w;
} pipe_ctx_t;

/*
 * setup_pipes() builds pipe fds into locals, writing to *pctx and *tmp
 * only on full success.  On failure both are left untouched.
 */
static int setup_pipes(const proc_config_t        *cfg,
                       posix_spawn_file_actions_t  *fa,
                       proc_handle_t               *tmp,
                       pipe_ctx_t                  *pctx)
{
    pipe_ctx_t  lp  = { -1, -1, -1 };
    proc_handle_t lh = *tmp;
    int r;

    if (cfg->stdin_mode == PROC_STDIO_PIPE) {
        int fds[2];
        if (pipe2(fds, O_CLOEXEC) < 0)
            goto fail_errno;
        lp.child_stdin_r = fds[0];
        lh.stdin_write   = fds[1];
        r = posix_spawn_file_actions_adddup2(fa, fds[0], STDIN_FILENO);
        if (r != 0) goto fail_r;
    } else if (cfg->stdin_mode == PROC_STDIO_DEVNULL) {
        r = posix_spawn_file_actions_addopen(fa, STDIN_FILENO,
                "/dev/null", O_RDONLY, 0);
        if (r != 0) goto fail_r;
    }

    if (cfg->stdout_mode == PROC_STDIO_PIPE) {
        int fds[2];
        if (pipe2(fds, O_CLOEXEC) < 0)
            goto fail_errno;
        lh.stdout_read    = fds[0];
        lp.child_stdout_w = fds[1];
        r = posix_spawn_file_actions_adddup2(fa, fds[1], STDOUT_FILENO);
        if (r != 0) goto fail_r;
    } else if (cfg->stdout_mode == PROC_STDIO_DEVNULL) {
        r = posix_spawn_file_actions_addopen(fa, STDOUT_FILENO,
                "/dev/null", O_WRONLY, 0);
        if (r != 0) goto fail_r;
    }

    if (cfg->stderr_mode == PROC_STDIO_PIPE) {
        int fds[2];
        if (pipe2(fds, O_CLOEXEC) < 0)
            goto fail_errno;
        lh.stderr_read    = fds[0];
        lp.child_stderr_w = fds[1];
        r = posix_spawn_file_actions_adddup2(fa, fds[1], STDERR_FILENO);
        if (r != 0) goto fail_r;
    } else if (cfg->stderr_mode == PROC_STDIO_DEVNULL) {
        r = posix_spawn_file_actions_addopen(fa, STDERR_FILENO,
                "/dev/null", O_WRONLY, 0);
        if (r != 0) goto fail_r;
    }

    /* Commit — only reached on full success */
    *pctx = lp;
    *tmp  = lh;
    return 0;

fail_errno:
    r = -errno;
fail_r:
    /* Clean up any pipe fds opened in locals before returning */
    close_fd(&lp.child_stdin_r);
    close_fd(&lp.child_stdout_w);
    close_fd(&lp.child_stderr_w);
    close_fd(&lh.stdin_write);
    close_fd(&lh.stdout_read);
    close_fd(&lh.stderr_read);
    /* *pctx and *tmp are untouched */
    return r;
}

static int close_child_ends(pipe_ctx_t *pctx)
{
    int err = 0;
    int r;

    r = close_fd(&pctx->child_stdin_r);
    if (r != 0) err = r;

    r = close_fd(&pctx->child_stdout_w);
    if (r != 0) err = r;

    r = close_fd(&pctx->child_stderr_w);
    if (r != 0) err = r;

    return err;
}

/* -------------------------------------------------------------------------
 * destroy_fa()
 *
 * Wraps posix_spawn_file_actions_destroy(), which returns an error code
 * directly (not via errno).  Returns negative errno on failure.
 * ------------------------------------------------------------------------- */

static int destroy_fa(posix_spawn_file_actions_t *fa)
{
    int r = posix_spawn_file_actions_destroy(fa);
    return r != 0 ? -r : 0;
}

/* -------------------------------------------------------------------------
 * destroy_attr()
 *
 * Wraps posix_spawnattr_destroy(), which returns an error code directly.
 * Returns negative errno on failure.
 * ------------------------------------------------------------------------- */

static int destroy_attr(posix_spawnattr_t *attr)
{
    int r = posix_spawnattr_destroy(attr);
    return r != 0 ? -r : 0;
}

/* -------------------------------------------------------------------------
 * setup_attr()
 *
 * Initialises *attr, applies cfg->spawnattr_flags, then calls
 * cfg->spawnattr_cb if set.  Writes to *attr only on full success.
 * On any failure, destroys the partially-initialised attr internally
 * so the caller never needs to clean up after an error.
 *
 * Returns 0 on success, negative errno on failure.
 *
 * Called only when cfg->spawnattr_flags != 0 or cfg->spawnattr_cb != NULL.
 * posix_spawnattr_* functions return errors directly, not via errno.
 * ------------------------------------------------------------------------- */

static int setup_attr(const proc_config_t *cfg, posix_spawnattr_t *attr)
{
    int r = posix_spawnattr_init(attr);
    if (r != 0)
        return -r;

    if (cfg->spawnattr_flags != 0) {
        r = posix_spawnattr_setflags(attr, cfg->spawnattr_flags);
        if (r != 0)
            goto fail;
    }

    if (cfg->spawnattr_cb != NULL) {
        r = cfg->spawnattr_cb(attr, cfg->spawnattr_userdata);
        if (r != 0)
            goto fail;
    }

    return 0;

fail:
    {
        int err = r < 0 ? r : -r;
        posix_spawnattr_destroy(attr);   /* ignore destroy error on fail path */
        return err;
    }
}

/* -------------------------------------------------------------------------
 * proc_spawn() - returns 0 on success, negative errno on failure.
 * ------------------------------------------------------------------------- */

int proc_spawn(const proc_config_t *cfg, proc_handle_t *out)
{
    if (!cfg || !cfg->path || !cfg->argv || !out)
        return -EINVAL;

    char * const *envp = cfg->envp ? cfg->envp : environ;

    bool              attr_ready = false;
    posix_spawnattr_t attr;
    posix_spawnattr_t *attrp     = NULL;

    posix_spawn_file_actions_t fa;
    int r = posix_spawn_file_actions_init(&fa);
    if (r != 0)
        return -r;

    /* Build the handle into a local; *out is untouched until success. */
    proc_handle_t tmp = {
        .type        = PROC_HANDLE_PID,
        .pid         = -1,
        .stdin_write = -1,
        .stdout_read = -1,
        .stderr_read = -1,
    };

    pipe_ctx_t pctx = { -1, -1, -1 };

    /* 1. Internal stdio pipes/devnulls */
    r = setup_pipes(cfg, &fa, &tmp, &pctx);
    if (r != 0)
        goto fail;

    /* 2. Caller's extra file actions, appended after ours */
    if (cfg->file_actions_cb != NULL) {
        r = cfg->file_actions_cb(&fa, cfg->file_actions_userdata);
        if (r != 0)
            goto fail;
    }

    /*
     * 3. Spawn attributes — only initialised when needed.
     *    If neither flags nor cb are set, attrp stays NULL and the spawn
     *    calls use POSIX-defined default behaviour.
     */
    if (cfg->spawnattr_flags != 0 || cfg->spawnattr_cb != NULL) {
        r = setup_attr(cfg, &attr);
        if (r != 0)
            goto fail;
        attr_ready = true;
        attrp      = &attr;
    }

    /* --- try pidfd_spawnp first ---------------------------------------- */

    int pidfd = -1;
    r = pidfd_spawnp(&pidfd, cfg->path, &fa, attrp, cfg->argv, envp);
    if (r == 0) {
        tmp.type  = PROC_HANDLE_PIDFD;
        tmp.pidfd = pidfd;
        goto done;
    }

    r = -errno;
    if (r != -ENOSYS)
        goto fail;

    /* --- ENOSYS: fall back to posix_spawnp + P_PID --------------------- */

    pid_t pid = -1;
    r = posix_spawnp(&pid, cfg->path, &fa, attrp, cfg->argv, envp);
    if (r != 0) {
        r = -r;
        goto fail;
    }

    tmp.type = PROC_HANDLE_PID;
    tmp.pid  = pid;

done:
    {
        int dr = destroy_fa(&fa);
        int ar = attr_ready ? destroy_attr(&attr) : 0;
        close_child_ends(&pctx);
        /* Commit: *out is written only here, on the success path */
        *out = tmp;
        return dr != 0 ? dr : ar;
    }

fail:
    {
        int err = r;
        int dr  = destroy_fa(&fa);
        int ar  = attr_ready ? destroy_attr(&attr) : 0;
        close_child_ends(&pctx);
        handle_cleanup(&tmp);
        /* *out is never touched */
        return err != 0 ? err : (dr != 0 ? dr : ar);
    }
}

/* -------------------------------------------------------------------------
 * do_waitid() - returns 1 if reaped, 0 if still running (WNOHANG),
 *               negative errno on error.
 *
 * Uses the raw waitid(2) syscall (5-argument form) to capture rusage.
 * The glibc wrapper omits the 5th argument; syscall() gives us access to it.
 *
 * On successful reap (return 1), *res and *ru are written.
 * On WNOHANG with no child ready (return 0), neither is written.
 * On error (return < 0), neither is written.
 * ------------------------------------------------------------------------- */

static int do_waitid(const proc_handle_t *h, int flags,
                     proc_wait_result_t *res, struct rusage *ru)
{
    proc_wait_result_t tmp_res = {0};
    struct rusage      tmp_ru  = {0};

    idtype_t type = (h->type == PROC_HANDLE_PIDFD) ? P_PIDFD : P_PID;
    id_t     id   = (h->type == PROC_HANDLE_PIDFD)
                    ? (id_t)h->pidfd
                    : (id_t)h->pid;

    long ret = syscall(SYS_waitid, type, id, &tmp_res.info,
                       flags | WEXITED, &tmp_ru);
    if (ret < 0)
        return -errno;

    if (tmp_res.info.si_pid == 0)
        return 0;   /* WNOHANG, child still running */

    switch (tmp_res.info.si_code) {
    case CLD_EXITED:
        tmp_res.exited = true;
        tmp_res.status = tmp_res.info.si_status;
        break;
    case CLD_KILLED:
    case CLD_DUMPED:
        tmp_res.signaled = true;
        tmp_res.status   = tmp_res.info.si_status;
        break;
    default:
        break;
    }

    /* Commit — only reached on successful reap */
    *res = tmp_res;
    *ru  = tmp_ru;
    return 1;
}

/* -------------------------------------------------------------------------
 * proc_wait() - returns 1 if reaped, 0 if still running (WNOHANG),
 *               negative errno on error.
 *
 * flags: extra waitid(2) flags ORed with WEXITED.
 *        Pass 0 for a plain blocking wait.
 *
 * On successful reap (return 1), *res is filled and h->rusage is updated.
 * ------------------------------------------------------------------------- */

int proc_wait(proc_handle_t *h, int flags, proc_wait_result_t *res)
{
    if (!h || !res) return -EINVAL;

    struct rusage ru;
    int r = do_waitid(h, flags, res, &ru);
    if (r == 1)
        h->rusage = ru;
    return r;
}

/* -------------------------------------------------------------------------
 * proc_kill() - returns 0 on success, negative errno on failure.
 * ------------------------------------------------------------------------- */

int proc_kill(const proc_handle_t *h, int sig)
{
    if (!h) return -EINVAL;

    int r;
    if (h->type == PROC_HANDLE_PIDFD)
        r = pidfd_send_signal(h->pidfd, sig, NULL, 0);
    else
        r = kill(h->pid, sig);

    return r < 0 ? -errno : 0;
}

/* -------------------------------------------------------------------------
 * proc_close() - returns 0 on success, negative errno if any close failed.
 *
 * The handle is always fully released even on error.  A close error is
 * advisory — typically -EIO meaning buffered pipe data was lost.
 * ------------------------------------------------------------------------- */

int proc_close(proc_handle_t *h)
{
    if (!h) return -EINVAL;

    int err = handle_cleanup(h);

    *h = (proc_handle_t){
        .type        = PROC_HANDLE_PID,
        .pid         = -1,
        .stdin_write = -1,
        .stdout_read = -1,
        .stderr_read = -1,
    };

    return err;
}
