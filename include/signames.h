#ifndef SIGNAMES_H
#define SIGNAMES_H

int signame_to_signum(const char *sig);
const char *signum_to_signame(int signum);
size_t get_numof_signames(void);
int get_signame_by_idx(size_t idx, const char **signame, int *signum);

#endif /* SIGNAMES_H */
