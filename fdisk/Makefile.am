include $(top_srcdir)/config/include-Makefile.am

EXTRA_DIST = README.fdisk README.cfdisk sfdisk.examples partitiontype.c

fdisk_common = i386_sys_types.c common.h gpt.c gpt.h \
	../lib/blkdev.c ../lib/wholedisk.c

if LINUX
fdisk_common += ../lib/linux_version.c
endif

if !ARCH_M68K

sbin_PROGRAMS = fdisk
dist_man_MANS = fdisk.8
fdisk_SOURCES = fdisk.c fdiskbsdlabel.c fdisksgilabel.c \
	fdisksunlabel.c fdiskaixlabel.c fdiskmaclabel.c partname.c \
	fdisk.h fdisksunlabel.h fdisksgilabel.h fdiskaixlabel.h \
	fdiskbsdlabel.h fdiskmaclabel.h $(fdisk_common)

if HAVE_STATIC_FDISK
sbin_PROGRAMS += fdisk.static
fdisk_static_SOURCES = $(fdisk_SOURCES)
fdisk_static_LDFLAGS = $(LDFLAGS_STATIC)
endif

if !ARCH_SPARC

sbin_PROGRAMS += sfdisk
dist_man_MANS += sfdisk.8
sfdisk_SOURCES = sfdisk.c partname.c $(fdisk_common)

if HAVE_STATIC_SFDISK
sbin_PROGRAMS += sfdisk.static
sfdisk_static_SOURCES = $(sfdisk_SOURCES)
sfdisk_static_LDFLAGS = $(LDFLAGS_STATIC)
endif

if USE_SLANG
sbin_PROGRAMS += cfdisk
dist_man_MANS += cfdisk.8
cfdisk_SOURCES = cfdisk.c $(fdisk_common)
cfdisk_LDADD = -lslang
else
if HAVE_NCURSES
sbin_PROGRAMS += cfdisk
dist_man_MANS += cfdisk.8
cfdisk_SOURCES = cfdisk.c $(fdisk_common)
cfdisk_LDADD = @NCURSES_LIBS@
endif
endif

endif # !ARCH_SPARC
endif # !ARCH_M68K

