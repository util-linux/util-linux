include $(top_srcdir)/config/include-Makefile.am

EXTRA_DIST = README.fdisk README.cfdisk sfdisk.examples partitiontype.c

fdisk_common = disksize.c i386_sys_types.c common.h gpt.c gpt.h

if !M68K

sbin_PROGRAMS = fdisk
man_MANS = fdisk.8
fdisk_SOURCES = fdisk.c fdiskbsdlabel.c fdisksgilabel.c \
	fdisksunlabel.c fdiskaixlabel.c partname.c \
	fdisk.h fdisksunlabel.h fdisksgilabel.h fdiskaixlabel.h \
	fdiskbsdlabel.h $(fdisk_common)

if !SPARC

sbin_PROGRAMS += sfdisk
man_MANS += sfdisk.8
sfdisk_SOURCES = sfdisk.c partname.c $(fdisk_common)

if USE_SLANG
sbin_PROGRAMS += cfdisk
man_MANS += cfdisk.8
cfdisk_SOURCES = cfdisk.c $(fdisk_common)
cfdisk_LDADD = -lslang
else
if HAVE_NCURSES
sbin_PROGRAMS += cfdisk
man_MANS += cfdisk.8
cfdisk_SOURCES = cfdisk.c $(fdisk_common)
cfdisk_LDADD = -lncurses
endif
endif

endif # !SPARC
endif # !M68K

