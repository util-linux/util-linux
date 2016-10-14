#include <stdlib.h>

#include "c.h"
#include "pwdutils.h"
#include "xalloc.h"

/* Returns allocated passwd and allocated pwdbuf to store passwd strings
 * fields. In case of error returns NULL and set errno, for unknown user set
 * errno to EINVAL
 */
struct passwd *xgetpwnam(const char *username, char **pwdbuf)
{
	struct passwd *pwd = NULL, *res = NULL;
	int rc;

	if (!pwdbuf || !username)
		return NULL;

	*pwdbuf = xmalloc(UL_GETPW_BUFSIZ);
	pwd = xcalloc(1, sizeof(struct passwd));

	errno = 0;
	rc = getpwnam_r(username, pwd, *pwdbuf, UL_GETPW_BUFSIZ, &res);
	if (rc != 0) {
		errno = rc;
		goto failed;
	}
	if (!res) {
		errno = EINVAL;
		goto failed;
	}
	return pwd;
failed:
	free(pwd);
	free(*pwdbuf);
	return NULL;
}


#ifdef TEST_PROGRAM
int main(int argc, char *argv[])
{
	char *pwdbuf = NULL;
	struct passwd *pwd = NULL;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <username>\n", argv[0]);
		return EXIT_FAILURE;
	}

	pwd = xgetpwnam(argv[1], &pwdbuf);
	if (!pwd)
		err(EXIT_FAILURE, "failed to get %s pwd entry", argv[1]);

	printf("Username: %s\n", pwd->pw_name);
	printf("UID:      %d\n", pwd->pw_uid);
	printf("HOME:     %s\n", pwd->pw_dir);
	printf("GECO:     %s\n", pwd->pw_gecos);

	free(pwd);
	free(pwdbuf);
	return EXIT_SUCCESS;
}
#endif /* TEST_PROGRAM */
