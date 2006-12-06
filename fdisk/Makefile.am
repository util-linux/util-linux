include $(top_srcdir)/config/include-Makefile.am

if !M68K

sbin_PROGRAMS = fdisk
man_MANS = fdisk.8
fdisk_SOURCES = fdisk.c disksize.c fdiskbsdlabel.c fdisksgilabel.c \
	fdisksunlabel.c fdiskaixlabel.c i386_sys_types.c partname.c

if !SPARC

sbin_PROGRAMS += sfdisk
man_MANS += sfdisk.8
sfdisk_SOURCES = sfdisk.c disksize.c i386_sys_types.c partname.c

if USE_SLANG
sbin_PROGRAMS += cfdisk
man_MANS += cfdisk.8
cfdisk_SOURCES = cfdisk.c disksize.c i386_sys_types.c
cfdisk_LDADD = -lslang
else
if HAVE_NCURSES
sbin_PROGRAMS += cfdisk
man_MANS += cfdisk.8
cfdisk_SOURCES = cfdisk.c disksize.c i386_sys_types.c
cfdisk_LDADD = -lncurses
endif
endif

endif

endif
