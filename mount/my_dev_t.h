/* silliness to get dev_t defined as the kernel defines it */
/* glibc uses a different dev_t */
/* maybe we need __kernel_old_dev_t -- later */
/* for ancient systems use "unsigned short" */

#include <linux/posix_types.h>
#define my_dev_t __kernel_dev_t
