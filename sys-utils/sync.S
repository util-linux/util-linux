/* File:
 *	sync.S
 * Compile:
 *     gcc -nostdlib sync.S -o sync
 * Author:
 *	Nick Holloway, with thanks to James Bonfield
 *      Modified for ELF by Michael Shields
 *      Rik Faith added __ASSEMBLY__ for gcc 2.5.8
 * Last Changed:
 *     1995-07-04
 */
#include <sys/syscall.h>
#ifndef __ASSEMBLY__
#define __ASSEMBLY__
#endif
#include <linux/linkage.h>

        .text
ENTRY(_start)
        movl    $(SYS_sync),%eax        /* sync () */
        int     $0x80
        movl    $(SYS_exit),%eax        /* exit ( 0 ) */
        movl    $0,%ebx
        int     $0x80
