/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org/licenses/>.
 */

#ifndef CAPUTILS_H
#define CAPUTILS_H

#include <stdint.h>
#include <linux/capability.h>

#ifndef PR_CAP_AMBIENT
# define PR_CAP_AMBIENT		47
#  define PR_CAP_AMBIENT_IS_SET	1
#  define PR_CAP_AMBIENT_RAISE	2
#  define PR_CAP_AMBIENT_LOWER	3
#endif

extern int capset(cap_user_header_t header, cap_user_data_t data);
extern int capget(cap_user_header_t header, const cap_user_data_t data);

extern int cap_last_cap(void);

extern void cap_permitted_to_ambient(void);

extern int cap_lower_effective(struct __user_cap_data_struct saved[_LINUX_CAPABILITY_U32S_3],
			       uint64_t retain);
extern int cap_restore(struct __user_cap_data_struct saved[_LINUX_CAPABILITY_U32S_3]);

#endif /* CAPUTILS_H */
