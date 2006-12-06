struct mountargs {
	const char *spec;
	const char *node;
	const char *type;
	int flags;
	void *data;
};

extern int verbose;

char *guess_fstype_from_superblock(const char *device);
int procfsloop(int (*mount_fn)(struct mountargs *), struct mountargs *args,
	       char **type);
int is_in_procfs(const char *fstype);
int have_procfs(void);
