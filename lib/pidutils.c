/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 *
 * Authors: Christian Goeschel Ndjomouo <cgoesc2@wgu.edu> [2025]
 */
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

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
 *         On failure, a negative errno number will be returned.
 */
int ul_parse_pid_str(char *pidstr, pid_t *pid_num, uint64_t *pfd_ino)
{
	int rc;
	char *end = NULL;
	int64_t num = 0;

	if (!pidstr || !*pidstr || !pid_num)
		return -EINVAL;

	num = strtoimax(pidstr, &end, 10);
	if (errno == 0 && ((num && num < 1) || (num && num > SINT_MAX(pid_t))))
		return -ERANGE;
	*pid_num = (pid_t) num;

	if (*end == ':' && pfd_ino) {
		rc = ul_strtou64(++end, pfd_ino, 10);
		if (rc != 0)
			return -ERANGE;
		*end = '\0';
	}
	if (errno != 0 || ((end && *end != '\0') || pidstr >= end))
		return -EINVAL;
	return 0;
}
