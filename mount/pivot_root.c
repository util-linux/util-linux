/* pivot_root.c - Change the root file system */

/* Written 2000 by Werner Almesberger */


#include <stdio.h>
#include <linux/unistd.h>


_syscall2(int,pivot_root,const char *,new_root,const char *,put_old)


int main(int argc,const char **argv)
{
    if (argc != 3) {
	fprintf(stderr,"usage: %s new_root put_old\n",argv[0]);
	return 1;
    }
    if (pivot_root(argv[1],argv[2]) < 0) {
	perror("pivot_root");
	return 1;
    }
    return 0;
}
