/* versions to be used with > 16-bit dev_t - leave unused for now */

#ifndef major
#define major(dev)	((dev) >> 8)
#endif

#ifndef minor
#define minor(dev)	((dev) & 0xff)
#endif
