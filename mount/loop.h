#ifndef _LINUX_LOOP_H
#define _LINUX_LOOP_H

/*
 * include/linux/loop.h
 *
 * Written by Theodore Ts'o, 3/29/93.
 *
 * Copyright 1993 by Theodore Ts'o.  Redistribution of this file is
 * permitted under the GNU Public License.
 */

#define LO_NAME_SIZE	64
#define LO_KEY_SIZE	32
       
struct loop_device {
	int		lo_number;
	struct inode	*lo_inode;
	int		lo_refcnt;
	dev_t		lo_device;
	int		lo_offset;
	int		lo_encrypt_type;
	int		lo_encrypt_key_size;
	int		lo_flags;
	int		(*transfer)(struct loop_device *, int cmd,
				    char *raw_buf, char *loop_buf, int size);
	char		lo_name[LO_NAME_SIZE];
	char		lo_encrypt_key[LO_KEY_SIZE];
#ifdef DES_AVAILABLE
	des_key_schedule lo_des_key;
	unsigned long	lo_des_init[2];
#endif
};

typedef	int (* transfer_proc_t)(struct loop_device *, int cmd,
				char *raw_buf, char *loop_buf, int size);

/*
 * Loop flags
 */
#define LO_FLAGS_DO_BMAP	0x00000001

struct loop_info {
	int		lo_number;	/* ioctl r/o */
	dev_t		lo_device; 	/* ioctl r/o */
	unsigned long	lo_inode; 	/* ioctl r/o */
	dev_t		lo_rdevice; 	/* ioctl r/o */
	int		lo_offset;
	int		lo_encrypt_type;
	int		lo_encrypt_key_size; 	/* ioctl w/o */
	int		lo_flags;	/* ioctl r/o */
	char		lo_name[LO_NAME_SIZE];
	unsigned char	lo_encrypt_key[LO_KEY_SIZE]; /* ioctl w/o */
	unsigned long	lo_init[2];
	char		reserved[4];
};

/*
 * Loop encryption types --- LO_CRYPT_IDEA isn't supported yet
 */

#define LO_CRYPT_NONE	0
#define LO_CRYPT_XOR	1
#define LO_CRYPT_DES	2
#define LO_CRYPT_IDEA	4
#define MAX_LO_CRYPT	3

/*
 * IOCTL commands --- we will commandeer 0x4C ('L')
 */

#define LOOP_SET_FD	0x4C00
#define LOOP_CLR_FD	0x4C01
#define LOOP_SET_STATUS	0x4C02
#define LOOP_GET_STATUS	0x4C03

#endif
