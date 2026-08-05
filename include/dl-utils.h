/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
#ifndef UTIL_LINUX_DL_UTILS_H
#define UTIL_LINUX_DL_UTILS_H

#include <stddef.h>
#include <stdint.h>

/*
 * Generic dlopen/dlsym helpers for optional runtime dependencies.
 *
 * Each optional library gets its own struct of function pointers and a
 * symbol table mapping names to offsets in that struct.  The generic
 * ul_dlopen_symbols() resolves all entries in one call.
 *
 * Library-specific wrappers live in lib/dl-<name>.c and expose a load
 * function (e.g. ul_load_libcryptsetup()) and a call macro.
 */

/* Symbol descriptor for the dlsym resolution loop */
struct ul_dlsym {
	const char *name;
	size_t offset;
};

/* Populate a symbol table entry; _type is the function-pointer struct */
#define UL_DLSYM(_type, _name) \
	{ \
		.name = # _name, \
		.offset = offsetof(_type, _name), \
	}

extern int ul_dlopen_symbols(const char *libname, int flags,
			     const struct ul_dlsym *syms, size_t nsyms,
			     void *opers, void **dl_handle);

/*
 * ELF .note.dlopen metadata — declares an optional dlopen() dependency so
 * that packaging tools (rpm, dpkg) can automatically derive it.
 *
 * See https://uapi-group.org/specifications/specs/elf_dlopen_metadata/
 *
 * Adapted from systemd's sd-dlopen.h (MIT-0, no copyright claimed).
 */
#define UL_ELF_NOTE_DLOPEN_VENDOR "FDO"
#define UL_ELF_NOTE_DLOPEN_TYPE   UINT32_C(0x407c0c0a)

#define UL_ELF_NOTE_DLOPEN_PRIORITY_REQUIRED    "required"
#define UL_ELF_NOTE_DLOPEN_PRIORITY_RECOMMENDED "recommended"
#define UL_ELF_NOTE_DLOPEN_PRIORITY_SUGGESTED   "suggested"

#ifndef _UL_ELF_NOTE_DLOPEN_SECTION_FLAGS
# define _UL_ELF_NOTE_DLOPEN_SECTION_FLAGS "aGR"
#endif

#if defined(__hppa__) || defined(__hppa64__)
# define _UL_ELF_NOTE_DLOPEN_GUARD ".set"
#else
# define _UL_ELF_NOTE_DLOPEN_GUARD ".equ"
#endif

#define _UL_ELF_NOTE_DLOPEN(json)                                                                                            \
        __asm__ (                                                                                                             \
                ".ifndef \"ul_dlopen:" json "\"\n"                                                                            \
                _UL_ELF_NOTE_DLOPEN_GUARD " \"ul_dlopen:" json "\", 1\n"                                                      \
                ".pushsection .note.dlopen, \"" _UL_ELF_NOTE_DLOPEN_SECTION_FLAGS "\", %note, \"ul_dlopen:" json "\", comdat\n" \
                ".balign 4\n"                                                                                                 \
                ".long 884f - 883f\n"                                                                                         \
                ".long 882f - 881f\n"                                                                                         \
                ".long 0x407c0c0a\n"                                                                                          \
                "883:\n"                                                                                                      \
                ".asciz \"" UL_ELF_NOTE_DLOPEN_VENDOR "\"\n"                                                                  \
                "884:\n"                                                                                                      \
                ".balign 4\n"                                                                                                 \
                "881:\n"                                                                                                      \
                ".asciz \"" json "\"\n"                                                                                       \
                "882:\n"                                                                                                      \
                ".balign 4\n"                                                                                                 \
                ".popsection\n"                                                                                               \
                ".endif\n")

#define _UL_SONAME_ARRAY1(a)          "[\\\"" a "\\\"]"
#define _UL_SONAME_ARRAY2(a, b)       "[\\\"" a "\\\",\\\"" b "\\\"]"
#define _UL_SONAME_ARRAY3(a, b, c)    "[\\\"" a "\\\",\\\"" b "\\\",\\\"" c "\\\"]"
#define _UL_SONAME_ARRAY4(a, b, c, d) "[\\\"" a "\\\",\\\"" b "\\\",\\\"" c "\\\",\\\"" d "\\\"]"
#define _UL_SONAME_ARRAY_GET(_1,_2,_3,_4,NAME,...) NAME
#define _UL_SONAME_ARRAY(...) _UL_SONAME_ARRAY_GET(__VA_ARGS__, _UL_SONAME_ARRAY4, _UL_SONAME_ARRAY3, _UL_SONAME_ARRAY2, _UL_SONAME_ARRAY1)(__VA_ARGS__)

#define UL_ELF_NOTE_DLOPEN(feature, description, priority, ...) \
        _UL_ELF_NOTE_DLOPEN("[{\\\"feature\\\":\\\"" feature "\\\",\\\"description\\\":\\\"" description "\\\",\\\"priority\\\":\\\"" priority "\\\",\\\"soname\\\":" _UL_SONAME_ARRAY(__VA_ARGS__) "}]")

#endif /* UTIL_LINUX_DL_UTILS_H */
