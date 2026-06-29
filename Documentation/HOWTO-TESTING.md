# Testing util-linux

## Quick Start

Compile and run basic tests:

	make check

Or with meson:

	meson test -C build

Run all tests including those requiring root:

	# cd tests
	# ./run.sh [options, see --help]

Or using sudo with make:

	$ make check-programs
	$ sudo -E make check TS_OPTS="--parallel=1"

Note: as root you must manually remove output and diff directories
(`rm -rf output diff`) or run `make clean` as root.

Note: the configure option `--disable-static` disables many libmount
and libblkid unit tests.

## Running Specific Tests

Run a test group:

	$ cd tests
	$ ./run.sh blkid
	$ ./run.sh libmount

Run an individual test:

	$ ./ts/cal/year

Exclude tests:

	$ ./run.sh --exclude="mount/move"

## Compile Test Programs Only

	$ make check-programs

## Fuzz Targets

Build and run fuzz targets (requires clang):

	$ ./tools/config-gen fuzz
	$ make check

## Environment Variables

`TS_COMMAND` — override the default command for `make check`:

	$ make check TS_COMMAND="true"   # build deps only, skip tests

`TS_OPTS` — pass options to `run.sh`:

	$ make check TS_OPTS="--parallel=1 utmp"

`TS_OPT_testdir_[testscript_]fake="yes|no"` — skip tests:

	$ make check TS_OPT_fdisk_fake="yes"           # skip all fdisk tests
	$ make check TS_OPT_fdisk_bsd_fake="yes"       # skip only fdisk/bsd
	$ make check TS_OPT_fdisk_fake="yes" TS_OPT_fdisk_bsd_fake="no"  # skip all fdisk except bsd

`TS_OPT_testdir_[testscript_]known_fail="yes|no"` — mark tests as known
failures (test runs but negative results are ignored).

`TS_OPT_testdir_[testscript_]verbose="yes|no"` — set verbosity.

`TS_OPT_testdir_[testscript_]memcheck="yes|no"` — run with valgrind.

## External Services

- Coveralls: https://coveralls.io/github/util-linux/util-linux
- Coverity Scan: https://scan.coverity.com/projects/karelzak-util-linux
- Fossies codespell: https://fossies.org/linux/test/util-linux-master.tar.gz/codespell.html
- OSS-Fuzz: https://oss-fuzz.com/coverage-report/job/libfuzzer_asan_util-linux/latest
- CIFuzz: https://github.com/util-linux/util-linux/actions?query=workflow%3ACIFuzz
