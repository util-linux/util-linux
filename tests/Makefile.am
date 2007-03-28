include $(top_srcdir)/config/include-Makefile.am

SUBDIRS = helpers

EXTRA_DIST = 	expected/* \
		functions.sh \
		input/* \
		README \
		run.sh \
		ts-col-multibyte \
		ts-ipcs-headers \
		ts-ipcs-limits \
		ts-ipcs-limits2 \
		ts-ipcs.sh \
		ts-mkswap-doit \
		ts-mount-paths \
		ts-namei-logic \
		ts-cramfs-mkfs \
		ts-look-separator \
		ts-login-checktty \
		ts-hwclock-systohc \
		ts-mount-special

distclean-local:
	rm -rf output diff

clean: distclean-local


