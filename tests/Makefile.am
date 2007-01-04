include $(top_srcdir)/config/include-Makefile.am

SUBDIRS = helpers

EXTRA_DIST = 	commands.sh.in \
		functions.sh \
		ts-mount-paths \
		ts-ipcs-headers \
		expected/ts-mount-paths \
		expected/ts-ipcs-headers

distclean-local:
	rm -rf output diff

