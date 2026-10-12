/* mem.h -- the two memory helpers a freestanding program needs, so that
 * nothing ever pulls libc in by accident. Byte loops: chip-memory bulk work
 * belongs to the blitter (see demo1.c). */
#ifndef MEM_H
#define MEM_H

#include <exec/types.h>

static inline void mem_set(void* dst, UBYTE value, ULONG bytes) {
  UBYTE* d = dst;
  while (bytes--) *d++ = value;
}

static inline void mem_copy(void* dst, const void* src, ULONG bytes) {
  UBYTE* d = dst;
  const UBYTE* s = src;
  while (bytes--) *d++ = *s++;
}

#endif
