// hardware.h -- chip register bases shared by the OS boundary and the demo.
#ifndef HARDWARE_H
#define HARDWARE_H

#include <exec/types.h>  // the NDK hardware headers need it
#include <hardware/blit.h>
#include <hardware/cia.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include <stddef.h>
#include <stdint.h>

#define CUSTOM ((volatile struct Custom*)0xdff000)
#define CIAA ((volatile struct CIA*)0xbfe001)

/* Custom-register offset for a copper MOVE or a hand-written table, taken
 * from the NDK's struct Custom so it cannot disagree with CUSTOM->field.
 * Array fields index naturally: REG(bplpt[1]) is BPL2PT, REG(color[17]) is
 * COLOR17. 32-bit pointer registers are two words: REG(x) high, +2 low. */
#define REG(field) ((uint16_t)offsetof(struct Custom, field))

static_assert(REG(bplcon0) == 0x100, "struct Custom matches the hardware");

/* Whole-register masks for the set/clear registers. Bit 15 is SET/CLR, so
 * writing these without it clears everything. */
static constexpr uint16_t INTF_ALL_SOURCES = 0x7fff;  // INTENA, INTREQ
static constexpr uint16_t DMAF_ALL_BITS = 0x7fff;     // DMACON

/* The DMACON bits a program can set, as read back from DMACONR: every
 * channel, the master enable and blitter priority. Bits 11-14 are status
 * (blitter busy/zero) and must not be written back. */
static constexpr uint16_t DMAF_WRITABLE = DMAF_ALL | DMAF_MASTER | DMAF_BLITHOG;
static_assert(DMAF_WRITABLE == 0x07ff, "DMACON writable bits");

/* Marks an exception handler: GCC's m68k backend saves every register the
 * body uses and returns with RTE. Host tools (clangd, clang-tidy) parse the
 * code as i386, where gnu::interrupt means the x86 attribute with different
 * rules, so it expands to nothing there. */
#ifdef __m68k__
#define INTERRUPT_HANDLER [[gnu::interrupt]]
#else
#define INTERRUPT_HANDLER
#endif

// An exception handler, entered by the CPU through the vector table.
typedef void (*exception_handler)(void);

// Autovector level 3 (VERTB/COPER/BLIT) exception vector offset.
static constexpr uintptr_t VECTOR_LEVEL3 = 0x6c;

/* The vector table slot at byte `offset` from the vector base `vbr`.
 * Base plus offset rather than indexing a table pointer: on a 68000 the
 * base is 0, and indexing a null pointer is undefined behaviour that GCC
 * may optimise on. A slot address is never null. */
static inline exception_handler volatile* vector_slot(uintptr_t vbr,
                                                      uintptr_t offset) {
  return (exception_handler volatile*)(vbr + offset);
}

#endif
