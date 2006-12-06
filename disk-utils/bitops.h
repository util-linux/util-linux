#ifdef __i386__

#define bitop(name,op) \
static inline int name(char * addr,unsigned int nr) \
{ \
int __res; \
__asm__ __volatile__("bt" op " %1,%2; adcl $0,%0" \
:"=g" (__res) \
:"r" (nr),"m" (*(addr)),"0" (0)); \
return __res; \
}

bitop(bit,"")
bitop(setbit,"s")
bitop(clrbit,"r")

#elif defined(__mc68000__)

#define bitop(name,op) \
static inline int name (char *addr, unsigned int nr) \
{ \
       char __res; \
       __asm__ __volatile__("bf" op " %2@{%1:#1}; sne %0" \
                            : "=d" (__res) \
                            : "d" (nr ^ 15), "a" (addr)); \
       return __res != 0; \
}

bitop (bit, "tst")
bitop (setbit, "set")
bitop (clrbit, "clr")

#else
static inline int bit(char * addr,unsigned int nr) 
{
  return (addr[nr >> 3] & (1<<(nr & 7))) != 0;
}

static inline int setbit(char * addr,unsigned int nr)
{
  int __res = bit(addr, nr);
  addr[nr >> 3] |= (1<<(nr & 7));
  return __res != 0; \
}

static inline int clrbit(char * addr,unsigned int nr)
{
  int __res = bit(addr, nr);
  addr[nr >> 3] &= ~(1<<(nr & 7));
  return __res != 0;
}

#endif

