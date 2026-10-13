// copper.h -- a tiny, bounds-checked copper-list emitter.
//
// Build the list in code instead of a static table: a per-line colour change
// is one cop_wait() and one cop_move(). The builder knows the end of its
// buffer, so emitting past it never writes out of bounds: it sets
// `overflow` and drops the instruction. Check cop_done() once at the end
// rather than counting words by hand.
#pragma once

#include <stddef.h>
#include <stdint.h>

struct copper {
  uint16_t* next;  // next free word
  uint16_t* end;   // one past the buffer's last word
  bool overflow;   // an instruction did not fit
};

static inline struct copper cop_begin(uint16_t* buffer, size_t words) {
  return (struct copper){
      .next = buffer, .end = buffer + words, .overflow = false};
}

// Reserve `words` words, or record the overflow and return nullptr.
static inline uint16_t* cop_reserve(struct copper* c, size_t words) {
  if (c->overflow || (size_t)(c->end - c->next) < words) {
    c->overflow = true;
    return nullptr;
  }
  uint16_t* p = c->next;
  c->next += words;
  return p;
}

// MOVE value to custom register `reg`, a REG() offset.
static inline void cop_move(struct copper* c, uint16_t reg, uint16_t value) {
  uint16_t* p = cop_reserve(c, 2);
  if (p) {
    p[0] = reg;
    p[1] = value;
  }
}

// WAIT for beam position (x, y); x in low-res pixel pairs, y 0-255.
// Lines past 255 need a wait at (0, 255) first, the usual copper trick.
static inline void cop_wait(struct copper* c, uint16_t x, uint16_t y) {
  uint16_t* p = cop_reserve(c, 2);
  if (p) {
    p[0] = (uint16_t)((y << 8) | (x & 0xfe) | 1);
    p[1] = 0xfffe;
  }
}

// Patch a 32-bit address into the two value words of a pointer pair
// emitted by cop_move_ptr(): two MOVEs, four words.
static inline void cop_set_ptr(uint16_t pair[static 4], const void* address) {
  pair[1] = (uint16_t)((uintptr_t)address >> 16);
  pair[3] = (uint16_t)((uintptr_t)address & 0xffff);
}

// MOVE a 32-bit address into a pointer register pair: `reg` is the high
// word's offset, e.g. REG(bplpt[0]). Returns the pair, for cop_set_ptr()
// to repoint later, or nullptr if it did not fit.
static inline uint16_t* cop_move_ptr(struct copper* c, uint16_t reg,
                                     const void* address) {
  uint16_t* p = cop_reserve(c, 4);
  if (p) {
    p[0] = reg;
    p[2] = (uint16_t)(reg + 2);
    cop_set_ptr(p, address);
  }
  return p;
}

// Terminate the list with a WAIT that never matches. Returns false if the
// list, terminator included, did not fit its buffer.
[[nodiscard]] static inline bool cop_done(struct copper* c) {
  uint16_t* p = cop_reserve(c, 2);
  if (p) {
    p[0] = 0xffff;
    p[1] = 0xfffe;
  }
  return !c->overflow;
}
