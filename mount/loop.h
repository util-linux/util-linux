#define LO_CRYPT_NONE	0
#define LO_CRYPT_XOR	1
#define LO_CRYPT_DES	2
#define LO_CRYPT_CRYPTOAPI 18

#define LOOP_SET_FD		0x4C00
#define LOOP_CLR_FD		0x4C01
#define LOOP_SET_STATUS		0x4C02
#define LOOP_GET_STATUS		0x4C03
#define LOOP_SET_STATUS64	0x4C04
#define LOOP_GET_STATUS64	0x4C05

#define LO_NAME_SIZE	64
#define LO_KEY_SIZE	32

#include "my_dev_t.h"

struct loop_info {
	int		lo_number;
	my_dev_t	lo_device;
	unsigned long	lo_inode;
	my_dev_t	lo_rdevice;
	int		lo_offset;
	int		lo_encrypt_type;
	int		lo_encrypt_key_size;
	int		lo_flags;
	char		lo_name[LO_NAME_SIZE];
	unsigned char	lo_encrypt_key[LO_KEY_SIZE];
	unsigned long	lo_init[2];
	char		reserved[4];
};

/*
 * Where to get __u8, __u32, __u64? Let us use unsigned char/int/long long
 * and get punished when someone comes with 128-bit long longs.
 */
struct loop_info64 {
	unsigned long long	lo_device;
	unsigned long long	lo_inode;
	unsigned long long	lo_rdevice;
	unsigned long long	lo_offset;
	unsigned long long	lo_sizelimit; /* bytes, 0 == max available */
	unsigned int		lo_number;
	unsigned int		lo_encrypt_type;
	unsigned int		lo_encrypt_key_size;
	unsigned int		lo_flags;
	unsigned char		lo_file_name[LO_NAME_SIZE];
	unsigned char		lo_crypt_name[LO_NAME_SIZE];
	unsigned char		lo_encrypt_key[LO_KEY_SIZE];
	unsigned long long	lo_init[2];
};
