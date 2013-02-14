#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "nls.h"
#include "c.h"
#include "xalloc.h"

#include "exec_shell.h"

#define DEFAULT_SHELL "/bin/sh"

void __attribute__((__noreturn__)) exec_shell(void) {
	const char *shell = getenv("SHELL"), *shell_basename;
	char *arg0;
	if (!shell)
		shell = DEFAULT_SHELL;

	shell_basename = basename(shell);
	arg0 = xmalloc(strlen(shell_basename) + 2);
	arg0[0] = '-';
	strcpy(arg0 + 1, shell_basename);

	execl(shell, arg0, NULL);
	err(EXIT_FAILURE, _("failed to execute %s"), shell);
}
