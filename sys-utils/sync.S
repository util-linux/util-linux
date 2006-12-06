/* File:
 *	sync.S
 * Compile:
 *	/lib/cpp sync.S > sync.s
 *	as -o sync.o sync.s
 *	ld -s -N -e _main sync.o -o sync
 * Author:
 *	Nick Holloway, with thanks to James Bonfield
 * Last Changed:
 *	September 1993.
 */
# include <sys/syscall.h>

        .text
        .globl  _main
_main:
        movl    $(SYS_sync),%eax        /* sync () */
        int     $0x80
        movl    $(SYS_exit),%eax        /* exit ( 0 ) */
        movl    $0,%ebx
        int     $0x80
