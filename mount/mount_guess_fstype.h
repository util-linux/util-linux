struct mountargs {
	const char *spec;
	const char *node;
	const char *type;
	int flags;
	void *data;
};

extern int verbose;

char *guess_fstype(const char *device);
char *do_guess_fstype(const char *device);
int procfsloop(int (*mount_fn)(struct mountargs *), struct mountargs *args,
	       const char **type);
int is_in_procfs(const char *fstype);

