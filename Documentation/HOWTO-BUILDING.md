# Building util-linux

## Quick Start

Autotools:

	./autogen.sh && ./configure && make

If something fails, check the last lines — the typical reason is a missing
dependency such as libtool or gettext.

Meson:

	meson setup build
	meson compile -C build

## Autotools Basics

- `./autogen.sh` generates all files needed to compile (run after git checkout)
- `make distclean` removes generated files; code can still be recompiled
  with `./configure && make`
- `make dist-gzip` (or `-bzip2`) creates a tarball that works without `./autogen.sh`

## Selective Compilation

See `./configure --help` for `--disable-*` and `--enable-*` options.

Build only specific programs:

	./configure --disable-all-programs --enable-fallocate

The configure script tracks dependencies between libs and tools. Follow
warning/error messages. For example, mount(8) needs libmount, libblkid
and libuuid:

	./configure --disable-all-programs --enable-mount \
	            --enable-libmount --enable-libblkid --enable-libuuid

## Compiler Flags

Use `SUID_CFLAGS` and `SUID_LDFLAGS` for suid programs (chfn, chsh,
newgrp, su, write, mount, umount):

	./configure SUID_CFLAGS="-fpie" SUID_LDFLAGS="-pie"

Use `DAEMON_CFLAGS` and `DAEMON_LDFLAGS` for daemons (uuidd).

Use `SOLIB_CFLAGS` and `SOLIB_LDFLAGS` for shared libraries (libmount,
libblkid, libuuid).

## Static Linking

Use `--enable-static-programs[=LIST]`.

Note that mount(8) uses NSS functions (get{pw,gr}nam, getpwuid) which may
be dynamically loaded even in static builds. The UID/GID translation will
not work in environments where NSS modules are not installed.

## Build System Internals

The autotools build system is non-recursive — subdirectories use `Makemodule.am`
files that are merged by automake into one global Makefile.

- All build results (binaries, libtool scripts) go in the top-level directory
- `Makemodule.am` files must use full paths (e.g., `foo_SOURCES = subdir/foo.c`)
- Always use `+=` for global variables (e.g., `bin_PROGRAMS += foo`)
- Use `libcommon.la` (without path) for lib/ stuff
- For libblkid/libuuid/libmount use `lib<name>.la` in `_LDADD` and
  `-I$(ul_lib<name>_incdir)` in `_CFLAGS`
- Always use suffixes for hooks (e.g., `install-exec-hook-foo`)
- All autoconf macros use the `UL_` prefix
- Utils are enabled/disabled via `BUILD_<NAME>` conditions (`AM_CONDITIONAL`)
- `BUILD_<NAME>` blocks are never nested; dependencies are resolved in `configure.ac`
  (see `UL_REQUIRES_BUILD()`)

Predefined configure scenarios are in `tools/config-gen.d/`:

	./tools/config-gen all selinux

WARNING: `config-gen` is for development only, not for end-user or
downstream distribution builds.
