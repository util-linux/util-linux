# Hacking on util-linux

## Man Pages

Since v2.37 util-linux uses asciidoc format for man pages.
See `man-common/manpage-stub.adoc` for details.

## Usage Function

Refer to `Documentation/boilerplate.c` for a complete example.

### Format

The `usage()` output consists of: Usage section, one-line command description,
Options section, optional special sections (e.g., 'Available columns'), and
a final man page reference line. Each section is separated by one empty line.

Only the synopsis and option lines are indented (one space).
Option lines do not use line-ending punctuation.

### Synopsis

Diamond brackets `<arg>` mark required arguments, square brackets `[arg]`
mark optional ones. Optional option arguments require `=` with no whitespace:
`--optional[=<arg>]`. Three dots `...` indicate unlimited repetition.

Use multiple synopsis lines when a command does fundamentally different things
depending on options/arguments:

	ionice [options] -p <pid> ...
	ionice [options] <command> [<arg> ...]

### Option Descriptions

- Short option first, then long option, separated by comma and space.
- Description starts at the column of the longest option plus two spaces.
- Maximum width is 80 characters; use indented continuation lines if needed.
- `--help` and `--version` are always last.
- One gettext entry per option — be nice to translators.

Use the `USAGE_HELP_OPTIONS(<num>)` macro from `include/c.h` for the
help/version options, where `<num>` is the description start column.

### Usage Function Rules

- `usage()` never returns — it is only called by `-h`/`--help`.
- All other error cases use `errtryhelp(EXIT_FAILURE)`.
- Use string constants from `include/c.h` for section headers.

## Debugging

### Libtool Wrappers (autotools only)

Binaries built with autotools/libtool are wrapped by shell scripts. The
actual binary is in `.libs/` (e.g., `mount/.libs/mount`). To run it
directly with gdb or valgrind, set `LD_LIBRARY_PATH`:

	export LD_LIBRARY_PATH=$PWD/libblkid/src/.libs/:$LD_LIBRARY_PATH

Meson builds produce directly usable binaries without wrappers, so this
is not needed when building with meson.

### Library Debug Output

All libraries support debug output via environment variables:

	export LIBBLKID_DEBUG=all
	export LIBMOUNT_DEBUG=all
	export LIBFDISK_DEBUG=all
	export LIBSMARTCOLS_DEBUG=all

See `libblkid/src/blkidP.h` and `libmount/src/mountP.h` for the meaning
of individual debug flags.

### Libmount Path Overrides

These environment variables override default paths (ignored for non-root):

| Variable | Default |
|---|---|
| `LIBMOUNT_FSTAB` | `/etc/fstab` |
| `LIBMOUNT_MTAB` | `/etc/mtab` |
| `LIBMOUNT_UTAB` | `/run/mount/utab` |

### Libblkid Overrides

- `BLKID_CONF` — override `/etc/blkid.conf`
- `BLKID_FILE` — override cache file location (see blkid(8))
