# AGENTS.md

This file provides guidance to AI coding agents working on this repository.

util-linux is a collection of essential Linux system utilities and libraries
(libmount, libblkid, libsmartcols, libfdisk, libuuid). The project follows
Linux kernel coding conventions and workflows.

## Legal

Only human beings can be credited in commit messages. Do not include
Co-Developed-By, Co-Authored-By, or similar attribution for AI models.
Commits should include a Signed-off-by line (`git commit -s`).

## Key Documentation

Always consult these files as needed:

- `Documentation/HOWTO-BUILDING.md` — how to compile the project, build system internals
- `Documentation/HOWTO-CONTRIBUTING.md` — contribution guidelines, patch and PR workflow
- `Documentation/HOWTO-HACKING.md` — coding details: usage functions, man pages, debugging
- `Documentation/HOWTO-TESTING.md` — regression test framework, environment variables

## Code Style

The coding style is based on the Linux kernel coding style.

- Do NOT use `{ }` braces for single-line if/else blocks.
- Do not use `else` after non-returning functions (err(), errx(), exit(), ...).
- Avoid `if ((rc = func()) != 0)` — split into separate assignment and comparison:
  ```c
  rc = func();
  if (rc != 0)
  ```
- Use `printf()` consistently, not `fprintf(stdout, ...)`.
- Follow existing naming and coding conventions in each file.
- Use EditorConfig (`.editorconfig` in the project root) for whitespace settings.

## Function Return Conventions

- Boolean functions return true/false — name with `_is_`, `_has_`, `_can_`.
- Iterator `_next_` functions: 0 = success, <0 = error, 1 = end of list.
- Error-returning functions: negative errno or NULL.
- Check existing similar functions in libsmartcols, libmount for API consistency.

## Memory Management

- For non-library code, use `include/xalloc.h` wrappers (`xmalloc()`, `xcalloc()`,
  `xstrdup()`, etc.) — these always succeed or terminate with `err()`.
- In libraries, check ownership semantics: does the function take ownership or copy?
- Trace every `malloc`/`calloc`/`strdup`/`asprintf` to its corresponding `free()`.
- `list_del()` must also free the element memory, not just unlink.
- Watch for missing `free()` on error/early-return paths.

## Commit Messages

- Use `Fixes:` when the commit resolves an issue.
- Use `Addresses:` when the commit only partly implements requested changes.
- Always use complete GitHub URLs, for example:
  `Fixes: https://github.com/util-linux/util-linux/issues/NNN`
- Do not commit generated files (po/, po-man/, ./configure, ...).

## Build and Test

The project supports two build systems: autotools and meson.

Autotools:

- Build: `./autogen.sh && ./configure && make`
- Run tests: `make check` or `cd tests && ./run.sh`
- Selective build: `./configure --disable-all-programs --enable-<name>`

Meson:

- Build: `meson setup build && meson compile -C build`
- Run tests: `meson test -C build`

Features should include corresponding tests.
