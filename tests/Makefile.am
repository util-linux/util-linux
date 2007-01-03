include $(top_srcdir)/config/include-Makefile.am

SUBDIRS = helpers

EXTRA_DIST = 	commands.sh.in \
		functions.sh \
		ts-mount-paths \
		ts-ipcs-headers \
		ts-ipcs-limits \
		ts-ipcs-limits2 \
		expected/ts-mount-paths \
		expected/ts-ipcs-headers \
		expected/ts-ipcs-limits \
		expected/ts-ipcs-limits2

distclean-local:
	rm -rf output diff

