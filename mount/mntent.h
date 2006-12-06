#include <mntent.h>		/* for struct mntent */

#define ERR_MAX 5

typedef struct mntFILEstruct {
	FILE *mntent_fp;
	char *mntent_file;
	int mntent_lineno;
	int mntent_errs;
	int mntent_softerrs;
} mntFILE;

mntFILE *my_setmntent (const char *file, char *mode);
void my_endmntent (mntFILE *mfp);
int my_addmntent (mntFILE *mfp, struct mntent *mnt);
struct mntent *my_getmntent (mntFILE *mfp);
