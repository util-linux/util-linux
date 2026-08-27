/*
 * test_mkfds - make various file descriptors
 *
 * Written by Masatake YAMATO <yamato@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org/licenses/>.
 */

#ifndef TEST_MKFDS_H
#define TEST_MKFDS_H

#include <asm/unistd.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef _U_
#define _U_ __attribute__((__unused__))
#endif

/* Exit code for test_mkfds execution control (distinct from errno-based
 * exit codes returned when factories fail to construct file descriptors). */
#define EXIT_EXPIRED 2

/* Update the constants in
 * tests/ts/lsfd/lsfd-functions.bash when changing
 * the following error definitions. */
#define EXIT_EPERM  18
#define EXIT_ENOPROTOOPT 19
#define EXIT_EPROTONOSUPPORT 20
#define EXIT_EACCES 21
#define EXIT_ENOENT 22
#define EXIT_ENOSYS 23
#define EXIT_EADDRNOTAVAIL 24
#define EXIT_ENODEV 25

enum multiplexing_mode {
   MX_READ   = 1 << 0,
   MX_WRITE  = 1 << 1,
   MX_EXCEPT = 1 << 2,
};

struct fdesc {
	int fd;
	void (*close)(int, void *);
	unsigned int mx_modes;
	void *data;
};

#define DEFUN_WAIT_EVENT_POLL(NAME,SYSCALL,XDECLS,SETUP_SIG_HANDLER,SYSCALL_INVOCATION) \
	bool wait_event_##NAME(bool add_stdin, int tfd, struct fdesc *fdescs, size_t n_fdescs) \
	{								\
		int n = (add_stdin ? 1 : 0) + (tfd >= 0 ? 1 : 0);	\
		int n0 = 0;						\
		int tfd_idx = -1;					\
		bool expired = false;					\
		struct pollfd *pfds = NULL;				\
									\
		XDECLS							\
									\
		for (size_t i = 0; i < n_fdescs; i++)			\
			if (fdescs[i].mx_modes)				\
				n++;					\
									\
		pfds = xcalloc(n, sizeof(pfds[0]));			\
									\
		for (size_t i = 0; i < n_fdescs; i++) {			\
			if (!fdescs[i].mx_modes)			\
				continue;				\
			pfds[n0].fd = fdescs[i].fd;			\
			if (fdescs[i].mx_modes & MX_READ)		\
				pfds[n0].events |= POLLIN;		\
			if (fdescs[i].mx_modes & MX_WRITE)		\
				pfds[n0].events |= POLLOUT;		\
			if (fdescs[i].mx_modes & MX_EXCEPT)		\
				pfds[n0].events |= POLLHUP;		\
			n0++;						\
		}							\
									\
		if (add_stdin) {					\
			pfds[n0].fd = 0;				\
			pfds[n0].events |= POLLIN;			\
			n0++;						\
		}							\
									\
		if (tfd >= 0) {						\
			tfd_idx = n0;					\
			pfds[n0].fd = tfd;				\
			pfds[n0].events |= POLLIN;			\
			n0++;						\
		}							\
									\
		SETUP_SIG_HANDLER					\
									\
		if (SYSCALL_INVOCATION < 0				\
		    && errno != EINTR) {				\
			if (errno == ENOSYS)				\
				errx(EXIT_ENOSYS, "no syscall: " SYSCALL); \
			err(EXIT_FAILURE, "failed in " SYSCALL);	\
		}							\
		if (tfd_idx >= 0 && (pfds[tfd_idx].revents & (POLLIN | POLLERR | POLLHUP))) \
			expired = true;					\
		free(pfds);						\
		return expired;						\
	}

#ifdef __NR_ppoll
bool wait_event_ppoll(bool add_stdin, int tfd, struct fdesc *fdescs, size_t n_fdescs);
#endif

#ifdef HAVE_LIBFUSE
#define HUNGFS_UNHUNG 'G'
#define HUNGFS_READY 'R'

struct hungfs_args {
	int ctlfd;
	const char *mountpoint;
	const char *file;
	bool hung;
};

void run_hungfs(struct hungfs_args *args);
#endif

#endif	/* TEST_MKFDS_H */
