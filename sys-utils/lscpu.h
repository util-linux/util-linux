#ifndef LSCPU_H
#define LSCPU_H

#include "c.h"
#include "nls.h"
#include "cpuset.h"
#include "xalloc.h"
#include "strutils.h"
#include "bitops.h"
#include "path.h"
#include "pathnames.h"
#include "all-io.h"

/* virtualization types */
enum {
	VIRT_NONE	= 0,
	VIRT_PARA,
	VIRT_FULL,
	VIRT_CONT
};

/* hypervisor vendors */
enum {
	HYPER_NONE	= 0,
	HYPER_XEN,
	HYPER_KVM,
	HYPER_MSHV,
	HYPER_VMWARE,
	HYPER_IBM,		/* sys-z powervm */
	HYPER_VSERVER,
	HYPER_UML,
	HYPER_INNOTEK,		/* VBOX */
	HYPER_HITACHI,
	HYPER_PARALLELS,	/* OpenVZ/VIrtuozzo */
	HYPER_VBOX,
	HYPER_OS400,
	HYPER_PHYP,
	HYPER_SPAR,
	HYPER_WSL,
};

enum {
	OUTPUT_SUMMARY	= 0,	/* default */
	OUTPUT_PARSABLE,	/* -p */
	OUTPUT_READABLE,	/* -e */
	OUTPUT_CACHES           /* -C */
};

#endif /* LSCPU_H */
