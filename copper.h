/* copper.h -- a tiny copper-list emitter.
 *
 * Build the list in code with cop_move()/cop_wait() instead of a static
 * table: adding a per-line colour change is then one cop_wait() and one
 * cop_move(), and the words that need patching at run time (bitplane
 * pointers) are just the pointer returned when they were emitted. */
#ifndef COPPER_H
#define COPPER_H

#include <exec/types.h>

/* MOVE value to custom register `reg` (byte offset from $DFF000). */
static inline UWORD* cop_move(UWORD* p, UWORD reg, UWORD value) {
  *p++ = reg;
  *p++ = value;
  return p;
}

/* WAIT for beam position (x, y); x in low-res pixel pairs, y 0-255.
 * Lines past 255 need a wait at (0, 255) first, the usual copper trick. */
static inline UWORD* cop_wait(UWORD* p, UWORD x, UWORD y) {
  *p++ = (UWORD)((y << 8) | (x & 0xfe) | 1);
  *p++ = 0xfffe;
  return p;
}

/* Patch a 32-bit address into the two value words of a hi/lo pointer
 * pair emitted as cop_move(REG_xxxPTH) followed by cop_move(REG_xxxPTL). */
static inline void cop_set_ptr(UWORD* pair, const void* address) {
  pair[1] = (UWORD)((ULONG)address >> 16);
  pair[3] = (UWORD)((ULONG)address & 0xffff);
}

/* End of list: a WAIT that never matches. */
static inline UWORD* cop_end(UWORD* p) {
  *p++ = 0xffff;
  *p++ = 0xfffe;
  return p;
}

#endif
