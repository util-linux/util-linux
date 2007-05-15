include $(top_srcdir)/config/include-Makefile.am

EXTRA_DIST = README.fdisk README.cfdisk sfdisk.examples partitiontype.c

if !M68K

sbin_PROGRAMS = fdisk
man_MANS = fdisk.8
fdisk_SOURCES = fdisk.c disksize.c fdiskbsdlabel.c fdisksgilabel.c \
	fdisksunlabel.c fdiskaixlabel.c i386_sys_types.c partname.c \
	common.h fdisk.h fdisksunlabel.h fdisksgilabel.h fdiskaixlabel.h \
	 fdiskbsdlabel.h

if !SPARC

sbin_PROGRAMS += sfdisk
man_MANS += sfdisk.8
sfdisk_SOURCES = sfdisk.c disksize.c i386_sys_types.c partname.c common.h

if USE_SLANG
sbin_PROGRAMS += cfdisk
man_MANS += cfdisk.8
cfdisk_SOURCES = cfdisk.c disksize.c i386_sys_types.c common.h
cfdisk_LDADD = -lslang
else
if HAVE_NCURSES
sbin_PROGRAMS += cfdisk
man_MANS += cfdisk.8
cfdisk_SOURCES = cfdisk.c disksize.c i386_sys_types.c common.h
cfdisk_LDADD = -lncurses
endif
endif

endif

endif

