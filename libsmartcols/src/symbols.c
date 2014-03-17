#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "smartcolsP.h"

struct libscols_symbols *scols_new_symbols(void)
{
	return calloc(1, sizeof(struct libscols_symbols));
}

int scols_symbols_set_branch(struct libscols_symbols *sb, const char *str)
{
	char *p = NULL;

	assert(sb);

	if (!sb)
		return -EINVAL;
	if (str) {
		p = strdup(str);
		if (!p)
			return -ENOMEM;
	}
	free(sb->branch);
	sb->branch = p;
	return 0;
}

int scols_symbols_set_vertical(struct libscols_symbols *sb, const char *str)
{
	char *p = NULL;

	assert(sb);

	if (!sb)
		return -EINVAL;
	if (str) {
		p = strdup(str);
		if (!p)
			return -ENOMEM;
	}
	free(sb->vert);
	sb->vert = p;
	return 0;
}

int scols_symbols_set_right(struct libscols_symbols *sb, const char *str)
{
	char *p = NULL;

	assert(sb);

	if (!sb)
		return -EINVAL;
	if (str) {
		p = strdup(str);
		if (!p)
			return -ENOMEM;
	}
	free(sb->right);
	sb->right = p;
	return 0;
}


struct libscols_symbols *scols_copy_symbols(const struct libscols_symbols *sb)
{
	struct libscols_symbols *ret;
	int rc;

	assert(sb);
	if (!sb)
		return NULL;

	ret = scols_new_symbols();
	if (!ret)
		return NULL;

	rc = scols_symbols_set_branch(ret, sb->branch);
	if (!rc)
		rc = scols_symbols_set_vertical(ret, sb->vert);
	if (!rc)
		rc = scols_symbols_set_right(ret, sb->right);
	if (!rc)
		return ret;

	scols_free_symbols(ret);
	return NULL;

}

void scols_free_symbols(struct libscols_symbols *sb)
{
	if (!sb)
		return;

	free(sb->branch);
	free(sb->vert);
	free(sb->right);
	free(sb);
}
