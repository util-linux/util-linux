/*
 * test_tiocsti - test security of TIOCSTI
 *
 * Written by Federico Bento <up201407890@alunos.dcc.fc.up.pt>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdlib.h>
#include <sys/ioctl.h>

int main(void)
{
	int rc = 0;
	char *cmd = "id -u -n\n";

	while(*cmd)
		rc += ioctl(0, TIOCSTI, cmd++);

	exit(rc ? EXIT_FAILURE : EXIT_SUCCESS);
}
