BSD_LABELSECTOR=0
BSD_LABELOFFSET=64
BSD_EXTRA=
BYTE_ORDER=BE

Initialize empty image
00000000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00a00000
Create new DOS partition table
00000000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
000001b0  00 00 00 00 00 00 00 00  01 00 00 00 00 00 00 00  |................|
000001c0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
000001f0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 55 aa  |..............U.|
00000200  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00a00000

---layout----------
Disk <removed>: 10 MiB, 10485760 bytes, 20480 sectors
Units: sectors of 1 * 512 = 512 bytes
Sector size (logical/physical): 512 bytes / 512 bytes
I/O size (minimum/optimal): 512 bytes / <removed> bytes
Disklabel type: dos
Disk identifier: <removed>
-------------------

Create 1st primary partition
00000000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
000001b0  00 00 00 00 00 00 00 00  01 00 00 00 00 00 00 20  |............... |
000001c0  21 00 83 41 01 00 00 08  00 00 00 08 00 00 00 00  |!..A............|
000001d0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
000001f0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 55 aa  |..............U.|
00000200  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00a00000

---layout----------
Disk <removed>: 10 MiB, 10485760 bytes, 20480 sectors
Units: sectors of 1 * 512 = 512 bytes
Sector size (logical/physical): 512 bytes / 512 bytes
I/O size (minimum/optimal): 512 bytes / <removed> bytes
Disklabel type: dos
Disk identifier: <removed>

Device     Boot Start   End Sectors Size Id Type
<removed>1       2048  4095    2048   1M 83 Linux
-------------------

Create 2st primary partition
00000000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
000001b0  00 00 00 00 00 00 00 00  01 00 00 00 00 00 00 20  |............... |
000001c0  21 00 83 41 01 00 00 08  00 00 00 08 00 00 00 04  |!..A............|
000001d0  01 3e 83 13 41 36 00 10  00 00 00 40 00 00 00 00  |.>..A6.....@....|
000001e0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
000001f0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 55 aa  |..............U.|
00000200  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00a00000
Set 2nd partition type
00000000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
000001b0  00 00 00 00 00 00 00 00  01 00 00 00 00 00 00 20  |............... |
000001c0  21 00 83 41 01 00 00 08  00 00 00 08 00 00 00 04  |!..A............|
000001d0  01 3e a5 13 41 36 00 10  00 00 00 40 00 00 00 00  |.>..A6.....@....|
000001e0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
000001f0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 55 aa  |..............U.|
00000200  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00a00000

---layout----------
Disk <removed>: 10 MiB, 10485760 bytes, 20480 sectors
Units: sectors of 1 * 512 = 512 bytes
Sector size (logical/physical): 512 bytes / 512 bytes
I/O size (minimum/optimal): 512 bytes / <removed> bytes
Disklabel type: dos
Disk identifier: <removed>

Device     Boot Start   End Sectors Size Id Type
<removed>1       2048  4095    2048   1M 83 Linux
<removed>2       4096 20479   16384   8M a5 FreeBSD
-------------------

Create default BSD
00000000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
000001b0  00 00 00 00 00 00 00 00  01 00 00 00 00 00 00 20  |............... |
000001c0  21 00 83 41 01 00 00 08  00 00 00 08 00 00 00 04  |!..A............|
000001d0  01 3e a5 13 41 36 00 10  00 00 00 40 00 00 00 00  |.>..A6.....@....|
000001e0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
000001f0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 55 aa  |..............U.|
00000200  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00200040  82 56 45 57 00 06 00 00  00 00 00 00 00 00 00 00  |.VEW............|
00200050  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
00200060  00 00 00 00 00 00 00 00  00 00 02 00 00 00 00 3f  |...............?|
00200070  00 00 00 ff 00 00 00 01  00 00 3e c1 00 00 3e c1  |..........>...>.|
00200080  00 00 00 00 00 00 00 00  0e 10 00 01 00 00 00 00  |................|
00200090  00 00 00 00 00 00 00 00  00 00 00 20 00 00 00 00  |........... ....|
002000a0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
002000c0  00 00 00 00 82 56 45 57  62 33 00 04 00 00 20 00  |.....VEWb3.... .|
002000d0  00 00 20 00 00 00 00 00  00 00 00 00 00 00 00 00  |.. .............|
002000e0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
002000f0  00 00 00 00 00 00 40 00  00 00 10 00 00 00 00 00  |......@.........|
00200100  00 00 00 00 00 00 3e c1  00 00 00 00 00 00 00 00  |......>.........|
00200110  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00a00000

---layout----------

Welcome to fdisk <removed>.
Changes will remain in memory only, until you decide to write them.
Be careful before using the write command.


Command (m for help): Entering nested BSD disklabel.

Command (m for help): Disk <removed>2: 10 MiB, 10485760 bytes, 20480 sectors
Geometry: 255 heads, 63 sectors/track, 1 cylinders
Units: sectors of 1 * 512 = 512 bytes
Sector size (logical/physical): 512 bytes / 512 bytes
I/O size (minimum/optimal): 512 bytes / <removed> bytes
Disklabel type: bsd

Slice Start   End Sectors  Size Type     Fsize Bsize Cpg
c      4096 20479   16384    8M unused       0     0   0
d         0 16064   16065  7.9M unused       0     0   0

Partition table entries are not in disk order.

Command (m for help): Leaving nested disklabel.

Command (m for help): 
-------------------

00000000  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
000001b0  00 00 00 00 00 00 00 00  01 00 00 00 00 00 00 20  |............... |
000001c0  21 00 83 41 01 00 00 08  00 00 00 08 00 00 00 04  |!..A............|
000001d0  01 3e a5 13 41 36 00 10  00 00 00 40 00 00 00 00  |.>..A6.....@....|
000001e0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
000001f0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 55 aa  |..............U.|
00000200  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00200040  82 56 45 57 00 06 00 00  00 00 00 00 00 00 00 00  |.VEW............|
00200050  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
00200060  00 00 00 00 00 00 00 00  00 00 02 00 00 00 00 3f  |...............?|
00200070  00 00 00 ff 00 00 00 01  00 00 3e c1 00 00 3e c1  |..........>...>.|
00200080  00 00 00 00 00 00 00 00  0e 10 00 01 00 00 00 00  |................|
00200090  00 00 00 00 00 00 00 00  00 00 00 20 00 00 00 00  |........... ....|
002000a0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
002000c0  00 00 00 00 82 56 45 57  7d 32 00 04 00 00 20 00  |.....VEW}2.... .|
002000d0  00 00 20 00 00 00 08 01  00 00 10 00 00 00 00 00  |.. .............|
002000e0  07 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
002000f0  00 00 00 00 00 00 40 00  00 00 10 00 00 00 00 00  |......@.........|
00200100  00 00 00 00 00 00 3e c1  00 00 00 00 00 00 00 00  |......>.........|
00200110  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
*
00a00000

---layout----------

Welcome to fdisk <removed>.
Changes will remain in memory only, until you decide to write them.
Be careful before using the write command.


Command (m for help): Entering nested BSD disklabel.

Command (m for help): Disk <removed>2: 10 MiB, 10485760 bytes, 20480 sectors
Geometry: 255 heads, 63 sectors/track, 1 cylinders
Units: sectors of 1 * 512 = 512 bytes
Sector size (logical/physical): 512 bytes / 512 bytes
I/O size (minimum/optimal): 512 bytes / <removed> bytes
Disklabel type: bsd

Slice Start   End Sectors  Size Type     Fsize Bsize Cpg
a      4096  6144    2049    1M 4.2BSD       0     0   0
c      4096 20479   16384    8M unused       0     0   0
d         0 16064   16065  7.9M unused       0     0   0

Partition table entries are not in disk order.

Command (m for help): Leaving nested disklabel.

Command (m for help): 
-------------------



Changes will remain in memory only, until you decide to write them.
Be careful before using the write command.


Command (m for help): Entering nested BSD disklabel.

Command (m for help): 
 0  unused           5  4.1BSD           9  4.4LFS           d  boot           
 1  swap             6  Eighth Edition   a  unknown          e  ADOS           
 2  Version 6        7  4.2BSD           b  HPFS             f  HFS            
 3  Version 7        8  MS-DOS           c  ISO-9660        10  AdvFS          
 4  System V       

Command (m for help): 
