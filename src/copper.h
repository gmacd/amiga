/* copper.h -- a tiny copper-list emitter.
 *
 * Build the list in code with cop_move()/cop_wait() instead of a static
 * table: adding a per-line colour change is then one cop_wait() and one
 * cop_move(), and the words that need patching at run time (bitplane
 * pointers) are just the pointer returned when they were emitted. */
#ifndef COPPER_H
#define COPPER_H

#include <stdint.h>

/* MOVE value to custom register `reg` (byte offset from $DFF000). */
static inline uint16_t* cop_move(uint16_t* p, uint16_t reg, uint16_t value) {
  *p++ = reg;
  *p++ = value;
  return p;
}

/* WAIT for beam position (x, y); x in low-res pixel pairs, y 0-255.
 * Lines past 255 need a wait at (0, 255) first, the usual copper trick. */
static inline uint16_t* cop_wait(uint16_t* p, uint16_t x, uint16_t y) {
  *p++ = (uint16_t)((y << 8) | (x & 0xfe) | 1);
  *p++ = 0xfffe;
  return p;
}

/* MOVE a 32-bit address into a pointer register pair: `reg` is the high
 * word's offset, e.g. REG(bplpt[0]). Returns the pair, for cop_set_ptr()
 * to repoint later; the next free word is the pair + 4. */
static inline uint16_t* cop_move_ptr(uint16_t* p, uint16_t reg,
                                     const void* address) {
  p[0] = reg;
  p[1] = (uint16_t)((uintptr_t)address >> 16);
  p[2] = (uint16_t)(reg + 2);
  p[3] = (uint16_t)((uintptr_t)address & 0xffff);
  return p;
}

/* Patch a 32-bit address into the two value words of a hi/lo pointer
 * pair emitted by cop_move_ptr(). */
static inline void cop_set_ptr(uint16_t* pair, const void* address) {
  pair[1] = (uint16_t)((uintptr_t)address >> 16);
  pair[3] = (uint16_t)((uintptr_t)address & 0xffff);
}

/* End of list: a WAIT that never matches. */
static inline uint16_t* cop_end(uint16_t* p) {
  *p++ = 0xffff;
  *p++ = 0xfffe;
  return p;
}

#endif
