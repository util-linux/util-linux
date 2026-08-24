# Hacking lsfd

`lsfd` is a command for listing file descriptors.
See the `lsfd(1)` man page for basic usage.

## Extending lsfd

(Planned topics)

* How to add a new column
* How to support a new type of file descriptor

## Testing lsfd

### Introduction to the `test_mkfds` Helper

The `test_mkfds` helper program can be used to test `lsfd`.
When building with `make`, `test_mkfds` can be compiled via:

```shell
$ make test_mkfds
```

`test_mkfds` opens various types of resources and holds their file
descriptors according to the specified arguments. After creating the
requested file descriptors, it prints its own process ID (PID) to standard
output and waits for input on standard input. Upon receiving any input, it
cleans up and exits.

```shell
$ ./test_mkfds ro-regular-file 5 file=/etc/fstab
2427534
<newline>
$
```

In this example, the regular file `/etc/fstab` is opened, assigned to file
descriptor 5, and held open. Since `2427534` is the PID of `test_mkfds`,
you can test `lsfd` against this process:

```shell
$ ./lsfd -p 2427534 -Q 'FD == 5'
COMMAND        PID  USER ASSOC  XMODE TYPE SOURCE MNTID  INODE NAME
test_mkfds 2427534 agent     5 r-----  REG   dm-0    42 262624 /etc/fstab
```

An `lsfd` test case runs `test_mkfds` to create the target file
descriptor(s). It then runs `lsfd -p $PID` with the printed PID (`$PID`) to
verify whether `lsfd` correctly retrieves and reports the expected
metadata. Once inspection is complete, the test writes something to the
standard input of `test_mkfds` to terminate it.

What `test_mkfds` opens is determined by the first argument, called a
**factory**. Various factories are available. You can list all available
factories with the `-l` option:

```shell
$ ./test_mkfds -l
FACTORY              PRIV COUNT NRETURN NPARAM DESCRIPTION
bpf-link              yes     1       3      0 make bpf-link
bpf-map               yes     1       1      2 make bpf-map
bpf-prog              yes     1       3      2 make bpf-prog
cdev-tun              yes     1       2      0 open /dev/net/tun
directory              no     1       1      2 directory
eventfd                no     2       2      0 make an eventfd connecting two processes
eventpoll              no     3       1      0 make eventpoll (epoll) file
...
pty                    no     2       2      0 make a pair of ptmx and pts
raw                   yes     1       1      1 AF_INET+SOCK_RAW sockets
raw6                  yes     1       1      1 AF_INET6+SOCK_RAW sockets
ro-block-device       yes     1       1      1 block device with O_RDONLY flag
ro-regular-file        no     1       1      3 read-only regular file
rw-character-device    no     1       1      1 character device with O_RDWR flag
```

Different factories hold different numbers of file descriptors. For
instance, the `pty` factory opens a master and a slave pseudo-terminal,
holding 2 file descriptors. The `COUNT` column in the `-l` output indicates
the number of file descriptors held by that factory. Looking at the list
above, `ro-regular-file` has `1`, while `pty` has `2`.

The positional arguments following the factory specify which file descriptor
numbers should be used to hold the descriptors.

```shell
$ ./test_mkfds ro-regular-file 5 file=/etc/fstab
```

Here, `5` instructs `test_mkfds` to assign the file descriptor created by
`ro-regular-file` to FD number 5. Internally, `dup2` is used to adjust the
descriptor to the requested number.

Factories with `yes` in the `PRIV` column require root privileges to
execute.

Some factories take optional `key=value` parameters. The `NPARAM` column in
the `-l` output indicates the number of available parameter types. All
parameters have default values, so they are optional.

You can inspect the parameters for a specific factory using the `-I`
option:

```shell
$ ./test_mkfds -I ro-regular-file
PARAMETER     TYPE DEFAULT_VALUE DESCRIPTION
file        string   /etc/passwd file to be opened
offset     integer             0 seek bytes after open with SEEK_CUR
read-lease boolean         false taking out read lease for the file
```

Here, the `file` parameter takes a string argument and defaults to
`/etc/passwd`. Passing `file=/etc/fstab` overrides the file to be opened.

Once the target file descriptors are ready, every factory prints its own
process ID (PID). Some factories print additional **output values** on
subsequent lines. The total number of output values (including the PID) is
shown in the `NRETURN` column of the `-l` output. Since every factory prints
at least its PID, `NRETURN` is always 1 or greater.

The description of each N-th output value (`NTH`) can be listed with the
`-O` option.

Example of inspecting the output values of the `bpf-prog` factory:

```shell
$ ./test_mkfds -O bpf-prog
NTH DESCRIPTION
  0 the pid owning the file descriptor(s)
  1 the id of bpf prog object
  2 the tag of bpf prog object
```

Output values represent internal information known to the `test_mkfds`
process itself. Even if such information could be extracted via other
tools, having `test_mkfds` report it directly simplifies test cases. There
are two typical use cases:

* **Incorporation into filter expressions**:
  Used as components in the filter expression passed to the `-Q` option of
  `lsfd`. Depending on the system state, running `lsfd` across all
  descriptors can be slow. Using `-Q` to narrow down the output
  significantly reduces test execution time and simplifies comparing
  `output` with `expected`.

* **Validation against internal values**:
  Used to verify whether `lsfd` accurately retrieves information that is
  difficult to obtain from outside the target process. For example, the
  memory address returned when `mmap`-ing a file descriptor can be reported
  by `test_mkfds` so that the test script can assert that `lsfd` extracted
  the correct address.

---

### Writing Test Cases with `test_mkfds`

Many test cases under `tests/ts/lsfd/` utilize `test_mkfds`. Tests are
written as `bash` scripts. `test_mkfds` is specifically designed to work in
conjunction with the `bash` `coproc` construct. While `coproc` is not widely
used elsewhere, it is essential for `lsfd` tests. Please consult the `bash`
manual to familiarize yourself with `coproc`.

Here is an excerpt from `tests/ts/lsfd/mkfds-eventpoll`:

```shell
 1: ts_init "$*"
 2:
 3: ts_check_test_command "$TS_CMD_LSFD"
 4: ts_check_test_command "$TS_HELPER_MKFDS"
 5:
 6: ts_cd "$TS_OUTDIR"
 7:
 8: PID=
 9: FD0=3
10: FD1=5
11: FD2=7
12:
13: {
14:     coproc MKFDS { "$TS_HELPER_MKFDS" eventpoll $FD0 $FD1 $FD2 ; }
15:     if read -u ${MKFDS[0]} PID; then
16:		    EXPR='(FD == '"$FD0"')'
17:		    ${TS_CMD_LSFD} -p "${PID}" -r -n -o ASSOC,TYPE,NAME,EVENTPOLL.TFDS -Q "${EXPR}"
18:		    echo 'ASSOC,TYPE,NAME,EVENTPOLL.TFDS': $?
19:		    ${TS_CMD_LSFD} -J -p "${PID}" -r -n -o ASSOC,TYPE,NAME,EVENTPOLL.TFDS -Q "${EXPR}"
20:		    echo 'ASSOC,TYPE,NAME,EVENTPOLL.TFDS (JSON)': $?
21:		    echo DONE >&"${MKFDS[1]}"
22:      fi
23:      wait ${MKFDS_PID}
24: } > "$TS_OUTPUT" 2>&1
25:
26: ts_finalize
```

- **Line 4**: `ts_check_test_command "$TS_HELPER_MKFDS"` declares that the
  test case requires the `test_mkfds` helper.
- **Line 14**: `coproc MKFDS { "$TS_HELPER_MKFDS" eventpoll $FD0 $FD1 $FD2 ; }`
  executes `test_mkfds` with the `eventpoll` factory. Since `eventpoll`
  holds 3 file descriptors, `$FD0`, `$FD1`, and `$FD2` are passed as FD
  numbers. The background coprocess is managed via the `MKFDS` file
  descriptor array.
- **Line 15**: Reads the first line of output from `test_mkfds` (the PID)
  into the variable `PID`.
- **Lines 16–20**: Runs `lsfd` in raw mode (`-r -n`) and JSON mode (`-J`)
  with column filters and records the exit status into the test output.
- **Line 21**: Writes `DONE` to the stdin of `test_mkfds` (`&"${MKFDS[1]}"`),
  signaling it to terminate.
- **Line 23**: `wait ${MKFDS_PID}` waits for `test_mkfds` to exit cleanly,
  ensuring no orphaned processes are left behind after the test finishes.

---

### How to Add a New Factory

When extending `tests/helpers/test_mkfds.c` with a new factory, you design
and implement the following four elements:

1. **Basic factory attributes**: Name (`name`), description (`desc`), root
   requirement (`priv`), and number of mandatory file descriptors (`N`).
2. **Parameter definitions (`params`)**: Accepted `key=value` argument
   names, types, default values, and descriptions.
3. **Creation function (`make`)**: The function that creates the target
   resource via system calls, duplicates it to the requested FD numbers via
   `dup2`, and registers cleanup handlers.
4. **Extra output values (`report` / `o_descs`) and cleanup (`free`)**:
   Optional functions to report internal information beyond PID and release
   allocated resources.

---

#### 1. Defining Parameters (`struct parameter[]`)

Parameters accepted by a factory are defined as an array of `struct
parameter`, terminated with `PARAM_END`.

Available types (`enum ptype`):
- `PTYPE_STRING`: String type (`defv.string = "..."`)
- `PTYPE_INTEGER`: Signed integer type (`defv.integer = 0`)
- `PTYPE_UINTEGER`: Unsigned integer type (`defv.uinteger = 0`)
- `PTYPE_BOOLEAN`: Boolean type (`defv.boolean = true/false`)

```c
/* Example: parameter definition for ro-regular-file */
static const struct parameter ro_regular_file_params[] = {
	{
		.name = "file",
		.type = PTYPE_STRING,
		.desc = "file to be opened",
		.defv.string = "/etc/passwd",
	},
	{
		.name = "offset",
		.type = PTYPE_INTEGER,
		.desc = "seek bytes after open with SEEK_CUR",
		.defv.integer = 0,
	},
	{
		.name = "read-lease",
		.type = PTYPE_BOOLEAN,
		.desc = "taking out read lease for the file",
		.defv.boolean = false,
	},
	PARAM_END
};
```

---

#### 2. Implementing the Creation Function (`make`)

The signature of the creation function is:

```c
void *make_foo(const struct factory *factory, struct fdesc fdescs[],
               int argc, char **argv);
```

Inside the `make` function:
1. Parse arguments with `decode_arg()`. **Always free each parsed argument
   with `free_arg()` after use.**
2. Open or create the target resource to obtain a temporary file descriptor
   (`fd`).
3. Duplicate the descriptor to the requested number via `dup2(fd,
   fdescs[i].fd)` and `close(fd)` the temporary descriptor.
4. Set the cleanup callback in `fdescs[i]` (usually `close_fdesc`).
5. If extra data needs to be passed to `report` or `free`, return a pointer
   to that custom context structure; otherwise, return `NULL`.

```c
/* Example: make function for ro-regular-file */
static void *open_ro_regular_file(const struct factory *factory,
				  struct fdesc fdescs[],
				  int argc, char **argv)
{
	struct arg file = decode_arg("file", factory->params, argc, argv);
	struct arg offset = decode_arg("offset", factory->params, argc, argv);
	struct arg lease_r = decode_arg("read-lease", factory->params, argc, argv);

	int fd = open(ARG_STRING(file), O_RDONLY);
	if (fd < 0)
		err(EXIT_FAILURE, "failed to open: %s", ARG_STRING(file));
	free_arg(&file);

	if (ARG_INTEGER(offset) != 0) {
		if (lseek(fd, (off_t)ARG_INTEGER(offset), SEEK_CUR) < 0)
			err(EXIT_FAILURE, "failed to seek 0 -> %ld", ARG_INTEGER(offset));
	}
	free_arg(&offset);

	if (ARG_BOOLEAN(lease_r)) {
		if (fcntl(fd, F_SETLEASE, F_RDLCK) < 0)
			err(EXIT_FAILURE, "failed to take out a read lease");
	}
	free_arg(&lease_r);

	/* Adjust to the requested FD number */
	if (fd != fdescs[0].fd) {
		if (dup2(fd, fdescs[0].fd) < 0)
			err(EXIT_FAILURE, "failed to dup %d -> %d", fd, fdescs[0].fd);
		close(fd);
	}

	fdescs[0] = (struct fdesc){
		.fd    = fdescs[0].fd,
		.close = close_fdesc,
		.data  = NULL
	};

	return NULL;
}
```

---

#### 3. Implementing Extra Output Values (`report` / `o_descs`) and Resource Cleanup (`free`) (Optional)

If internal information (such as BPF object IDs or mapped memory addresses)
should be communicated to the test script, implement `EX_O`, `o_descs`,
`report`, and `free`.

```c
/* Example: report and free implementation for bpf-prog */
enum ritem_bpf_prog {
	RITEM_BPF_PROG_ID,
	RITEM_BPF_PROG_TAG,
};

static void report_bpf_prog(const struct factory *factory _U_,
			    int nth, void *data, FILE *fp)
{
	struct bpf_prog_info *info = data;

	switch (nth) {
	case RITEM_BPF_PROG_ID:
		fprintf(fp, "%u", info->id);
		break;
	case RITEM_BPF_PROG_TAG:
		for (size_t i = 0; i < BPF_TAG_SIZE; i++)
			fprintf(fp, "%02x", info->tag[i]);
		break;
	}
}

static void free_bpf_prog(const struct factory *factory _U_, void *data)
{
	free(data);
}
```

---

#### 4. Registering into the `factories[]` Table

Add a new entry to the `factories[]` array in `tests/helpers/test_mkfds.c`.

```c
	{
		.name = "ro-regular-file",
		.desc = "read-only regular file",
		.priv = false,
		.N    = 1,
		.EX_N = 0,
		.make = open_ro_regular_file,
		.params = (struct parameter []) {
			{
				.name = "file",
				.type = PTYPE_STRING,
				.desc = "file to be opened",
				.defv.string = "/etc/passwd",
			},
			{
				.name = "offset",
				.type = PTYPE_INTEGER,
				.desc = "seek bytes after open with SEEK_CUR",
				.defv.integer = 0,
			},
			{
				.name = "read-lease",
				.type = PTYPE_BOOLEAN,
				.desc = "taking out read lease for the file",
				.defv.boolean = false,
			},
			PARAM_END
		},
	},
```

When extra output values are provided, configure `EX_O`, `report`, `free`,
and `o_descs`:

```c
	{
		.name = "bpf-prog",
		.desc = "make bpf-prog",
		.priv = true,
		.N    = 1,
		.EX_N = 0,
		.EX_O = 2,
		.make = make_bpf_prog,
		.report = report_bpf_prog,
		.free = free_bpf_prog,
		.params = (struct parameter []) {
			{
				.name = "prog-type-id",
				.type = PTYPE_INTEGER,
				.desc = "program type by id",
				.defv.integer = 1,
			},
			PARAM_END
		},
		.o_descs = (const char *[]){
			"the id of bpf prog object",
			"the tag of bpf prog object",
		},
	},
```

---

#### 5. Verification and Test Suite Integration

After adding a factory, verify its behavior with the following steps:

1. **Build**:
   ```shell
   $ make test_mkfds
   ```
2. **Inspect factory details**:
   ```shell
   $ ./test_mkfds -l | grep <factory-name>
   $ ./test_mkfds -I <factory-name>
   $ ./test_mkfds -O <factory-name>
   ```
3. **Standalone test and memory leak validation with Valgrind**:
   Run manually to verify that the specified FD is created and waits on
   stdin:

   ```shell
   $ ./test_mkfds <factory-name> 3 ...
   ```

   It is strongly recommended to check for memory leaks (e.g. in argument
   parsing or custom allocations) using **Valgrind**. Passing the `-c`
   (`--dont-pause`) option skips pausing and immediately cleans up, enabling
   fast automated leak checks:

   ```shell
   $ valgrind --leak-check=full --error-exitcode=1 ./test_mkfds -c <factory-name> 3 ...
   ```

   Alternatively, pipe a newline to standard input:

   ```shell
   $ echo "" | valgrind --leak-check=full ./test_mkfds <factory-name> 3 ...
   ```

4. **Create a test case script under `tests/ts/lsfd/`**:
   Add `tests/ts/lsfd/mkfds-<factory-name>` and run the regression tests:

   ```shell
   $ cd tests && ./run.sh lsfd
   ```
