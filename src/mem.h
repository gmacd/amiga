/* mem.h -- the two memory helpers a freestanding program needs, so that
 * nothing ever pulls libc in by accident. Byte loops: chip-memory bulk work
 * belongs to the blitter (see demo1.c). */
#ifndef MEM_H
#define MEM_H

#include <stddef.h>
#include <stdint.h>

static inline void mem_set(void* dst, uint8_t value, size_t bytes) {
  uint8_t* d = dst;
  while (bytes--) {
    *d++ = value;
  }
}

static inline void mem_copy(void* dst, const void* src, size_t bytes) {
  uint8_t* d = dst;
  const uint8_t* s = src;
  while (bytes--) {
    *d++ = *s++;
  }
}

#endif
