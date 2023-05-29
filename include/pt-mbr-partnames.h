/*
 * No copyright is claimed.  This code is in the public domain; do with
 * it what you wish.
 */
	{.code = 0x00, N_("Empty")},
	{.code = 0x01, N_("FAT12")},
	{.code = 0x02, N_("XENIX root")},
	{.code = 0x03, N_("XENIX usr")},
	{.code = 0x04, N_("FAT16 <32M")},
	{.code = 0x05, N_("Extended")},		/* DOS 3.3+ extended partition */
	{.code = 0x06, N_("FAT16")},		/* DOS 16-bit >=32M */
	{.code = 0x07, N_("HPFS/NTFS/exFAT")},	/* OS/2 IFS, eg, HPFS or NTFS or QNX or exFAT */
	{.code = 0x08, N_("AIX")},		/* AIX boot (AIX -- PS/2 port) or SplitDrive */
	{.code = 0x09, N_("AIX bootable")},	/* AIX data or Coherent */
	{.code = 0x0a, N_("OS/2 Boot Manager")},/* OS/2 Boot Manager */
	{.code = 0x0b, N_("W95 FAT32")},
	{.code = 0x0c, N_("W95 FAT32 (LBA)")},/* LBA really is `Extended Int 13h' */
	{.code = 0x0e, N_("W95 FAT16 (LBA)")},
	{.code = 0x0f, N_("W95 Ext'd (LBA)")},
	{.code = 0x10, N_("OPUS")},
	{.code = 0x11, N_("Hidden FAT12")},
	{.code = 0x12, N_("Compaq diagnostics")},
	{.code = 0x14, N_("Hidden FAT16 <32M")},
	{.code = 0x16, N_("Hidden FAT16")},
	{.code = 0x17, N_("Hidden HPFS/NTFS")},
	{.code = 0x18, N_("AST SmartSleep")},
	{.code = 0x1b, N_("Hidden W95 FAT32")},
	{.code = 0x1c, N_("Hidden W95 FAT32 (LBA)")},
	{.code = 0x1e, N_("Hidden W95 FAT16 (LBA)")},
	{.code = 0x24, N_("NEC DOS")},
	{.code = 0x27, N_("Hidden NTFS WinRE")},
	{.code = 0x39, N_("Plan 9")},
	{.code = 0x3c, N_("PartitionMagic recovery")},
	{.code = 0x40, N_("Venix 80286")},
	{.code = 0x41, N_("PPC PReP Boot")},
	{.code = 0x42, N_("SFS")},
	{.code = 0x4d, N_("QNX4.x")},
	{.code = 0x4e, N_("QNX4.x 2nd part")},
	{.code = 0x4f, N_("QNX4.x 3rd part")},
	{.code = 0x50, N_("OnTrack DM")},
	{.code = 0x51, N_("OnTrack DM6 Aux1")},	/* (or Novell) */
	{.code = 0x52, N_("CP/M")},		/* CP/M or Microport SysV/AT */
	{.code = 0x53, N_("OnTrack DM6 Aux3")},
	{.code = 0x54, N_("OnTrackDM6")},
	{.code = 0x55, N_("EZ-Drive")},
	{.code = 0x56, N_("Golden Bow")},
	{.code = 0x5c, N_("Priam Edisk")},
	{.code = 0x61, N_("SpeedStor")},
	{.code = 0x63, N_("GNU HURD or SysV")},	/* GNU HURD or Mach or Sys V/386 (such as ISC UNIX) */
	{.code = 0x64, N_("Novell Netware 286")},
	{.code = 0x65, N_("Novell Netware 386")},
	{.code = 0x70, N_("DiskSecure Multi-Boot")},
	{.code = 0x75, N_("PC/IX")},
	{.code = 0x80, N_("Old Minix")},	/* Minix 1.4a and earlier */
	{.code = 0x81, N_("Minix / old Linux")},/* Minix 1.4b and later */
	{.code = 0x82, N_("Linux swap / Solaris")},
	{.code = 0x83, N_("Linux")},
	{.code = 0x84, N_("OS/2 hidden or Intel hibernation")},/* OS/2 hidden C: drive,
					   hibernation type Microsoft APM
					   or hibernation Intel Rapid Start */
	{.code = 0x85, N_("Linux extended")},
	{.code = 0x86, N_("NTFS volume set")},
	{.code = 0x87, N_("NTFS volume set")},
	{.code = 0x88, N_("Linux plaintext")},
	{.code = 0x8e, N_("Linux LVM")},
	{.code = 0x93, N_("Amoeba")},
	{.code = 0x94, N_("Amoeba BBT")},	/* (bad block table) */
	{.code = 0x9f, N_("BSD/OS")},		/* BSDI */
	{.code = 0xa0, N_("IBM Thinkpad hibernation")},
	{.code = 0xa5, N_("FreeBSD")},		/* various BSD flavours */
	{.code = 0xa6, N_("OpenBSD")},
	{.code = 0xa7, N_("NeXTSTEP")},
	{.code = 0xa8, N_("Darwin UFS")},
	{.code = 0xa9, N_("NetBSD")},
	{.code = 0xab, N_("Darwin boot")},
	{.code = 0xaf, N_("HFS / HFS+")},
	{.code = 0xb7, N_("BSDI fs")},
	{.code = 0xb8, N_("BSDI swap")},
	{.code = 0xbb, N_("Boot Wizard hidden")},
	{.code = 0xbc, N_("Acronis FAT32 LBA")},/* hidden (+0xb0) Acronis Secure Zone (backup software) */
	{.code = 0xbe, N_("Solaris boot")},
	{.code = 0xbf, N_("Solaris")},
	{.code = 0xc1, N_("DRDOS/sec (FAT-12)")},
	{.code = 0xc4, N_("DRDOS/sec (FAT-16 < 32M)")},
	{.code = 0xc6, N_("DRDOS/sec (FAT-16)")},
	{.code = 0xc7, N_("Syrinx")},
	{.code = 0xda, N_("Non-FS data")},
	{.code = 0xdb, N_("CP/M / CTOS / ...")},/* CP/M or Concurrent CP/M or
					   Concurrent DOS or CTOS */
	{.code = 0xde, N_("Dell Utility")},	/* Dell PowerEdge Server utilities */
	{.code = 0xdf, N_("BootIt")},		/* BootIt EMBRM */
	{.code = 0xe1, N_("DOS access")},	/* DOS access or SpeedStor 12-bit FAT
					   extended partition */
	{.code = 0xe3, N_("DOS R/O")},		/* DOS R/O or SpeedStor */
	{.code = 0xe4, N_("SpeedStor")},	/* SpeedStor 16-bit FAT extended
					   partition < 1024 cyl. */

	/* Linux https://www.freedesktop.org/wiki/Specifications/BootLoaderSpec/ */
	{.code = 0xea, N_("Linux extended boot")},

	{.code = 0xeb, N_("BeOS fs")},
	{.code = 0xee, N_("GPT")},		/* Intel EFI GUID Partition Table */
	{.code = 0xef, N_("EFI (FAT-12/16/32)")},/* Intel EFI System Partition */
	{.code = 0xf0, N_("Linux/PA-RISC boot")},/* Linux/PA-RISC boot loader */
	{.code = 0xf1, N_("SpeedStor")},
	{.code = 0xf4, N_("SpeedStor")},	/* SpeedStor large partition */
	{.code = 0xf2, N_("DOS secondary")},	/* DOS 3.3+ secondary */
	{.code = 0xf8, N_("EBBR protective")},	/* Arm EBBR firmware protective partition */
	{.code = 0xfb, N_("VMware VMFS")},
	{.code = 0xfc, N_("VMware VMKCORE")},	/* VMware kernel dump partition */
	{.code = 0xfd, N_("Linux raid autodetect")},/* Linux raid partition with
					       autodetect using persistent
					       superblock */
	{.code = 0xfe, N_("LANstep")},		/* SpeedStor >1024 cyl. or LANstep */
	{.code = 0xff, N_("BBT")},		/* Xenix Bad Block Table */

	{ 0 }
