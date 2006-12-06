/*
 * Some people submit a utility flushb.c.
 * However, it is useless, BLKFLSBUF is already part of blockdev,
 * so that "flushb device" is the same as "blockdev --flushbufs device".
 */
