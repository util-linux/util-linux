/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Authors: Christian Goeschel Ndjomouo <cgoesc2@wgu.edu> [2025]
 */
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

#include "nls.h"
#include "strutils.h"
#include "pidutils.h"

/*
 * ul_parse_pid_str() - Parse a string and store the found pid and/or pidfd inode.
 *
 * @pidstr:  string in format `pid:pidfd_inode` that is to be parsed
 * @pid_num: stores pid number
 * @pfd_ino: stores pidfd inode number
 *
 * If @pfd_ino is not destined to be set, pass it as NULL.
 *
 * Return: On success, 0 is returned.
 *         On failure, a negative errno number is returned
 *         and errno is set to indicate the issue.
 */
int ul_parse_pid_str(char *pidstr, pid_t *pid_num, uint64_t *pfd_ino)
{
	int rc;
	char *end = NULL;
	int64_t num = 0;

	if (!pidstr || !*pidstr || !pid_num)
		return -(errno = EINVAL);

	num = strtoimax(pidstr, &end, 10);
	if (num == 0 && end == pidstr)
		return -(errno = EINVAL);

	if (errno == ERANGE || (num <= 0 || num > SINT_MAX(pid_t)))
		return -(errno = ERANGE);

	*pid_num = (pid_t) num;

	if (*end == ':' && pfd_ino) {
		rc = ul_strtou64(++end, pfd_ino, 10);
		if (rc < 0)
			return rc;

		if (*pfd_ino == 0)
			return -(errno = ERANGE);
		*end = '\0';
	}

	if (end && *end != '\0')
		return -(errno = EINVAL);
	return 0;
}

/*
 * ul_parse_pid_str_or_err() - Parse a string and store the found pid
 *                             and pidfd inode, or exit on error.
 *
 * @pidstr:  string in format `pid[:pidfd_inode]` that is to be parsed
 * @pid_num: stores pid number
 * @pfd_ino: stores pidfd inode number
 *
 * If @pfd_ino is not destined to be set, pass it as NULL.
 *
 * On failure, err() is called with an error message to indicate the issue.
 */
void ul_parse_pid_str_or_err(char *pidstr, pid_t *pid_num, uint64_t *pfd_ino)
{
	if (ul_parse_pid_str(pidstr, pid_num, pfd_ino) < 0) {
		err(EXIT_FAILURE, N_("failed to parse PID argument '%s'"), pidstr);
	}
}
