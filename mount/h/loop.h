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

#define LO_CRYPT_NONE	0
#define LO_CRYPT_XOR	1
#define LO_CRYPT_DES	2
#define LO_CRYPT_IDEA	3
#define MAX_LO_CRYPT	4

#define LOOP_SET_FD	0x4C00
#define LOOP_CLR_FD	0x4C01
#define LOOP_SET_STATUS	0x4C02
#define LOOP_GET_STATUS	0x4C03
