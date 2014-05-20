#ifndef LSCPU_H
#define LSCPU_H

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
};

extern int read_hypervisor_dmi(void);

#endif /* LSCPU_H */
