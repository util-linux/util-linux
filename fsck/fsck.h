/*
 * fsck.h
 */

#include <time.h>

#ifdef __STDC__
#define NOARGS void
#else
#define NOARGS
#endif

#ifdef __GNUC__
#define FSCK_ATTR(x) __attribute__(x)
#else
#define FSCK_ATTR(x)
#endif


#ifndef DEFAULT_FSTYPE
#define DEFAULT_FSTYPE	"ext2"
#endif

#define MAX_DEVICES 32
#define MAX_ARGS 32

/*
 * Internal structure for mount tabel entries.
 */

struct fsck_fs_data
{
	const char	*device;
	dev_t		disk;
	unsigned int	stacked:1,
			done:1,
			eval_device:1;
};

#define FLAG_DONE 1
#define FLAG_PROGRESS 2

/*
 * Structure to allow exit codes to be stored
 */
struct fsck_instance {
	int	pid;
	int	flags;
	int	lock;		/* flock()ed whole disk file descriptor or -1 */
	int	exit_status;
	time_t	start_time;
	char *	prog;
	char *	type;
	struct libmnt_fs *fs;
	struct fsck_instance *next;
};

extern char *base_device(const char *device);
extern const char *identify_fs(const char *fs_name, const char *fs_types);

/* ismounted.h */
extern int is_mounted(const char *file);
