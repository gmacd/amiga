/* hardware.h -- chip register bases shared by the OS boundary and the demo. */
#ifndef HARDWARE_H
#define HARDWARE_H

#include <exec/types.h> /* the NDK hardware headers need it */
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
static constexpr uint16_t INTF_ALL_SOURCES = 0x7fff; /* INTENA, INTREQ   */
static constexpr uint16_t DMAF_ALL_BITS = 0x7fff;    /* DMACON           */

/* The DMACON bits a program can set, as read back from DMACONR: every
 * channel, the master enable and blitter priority. Bits 11-14 are status
 * (blitter busy/zero) and must not be written back. */
static constexpr uint16_t DMAF_WRITABLE = DMAF_ALL | DMAF_MASTER | DMAF_BLITHOG;
static_assert(DMAF_WRITABLE == 0x07ff, "DMACON writable bits");

/* Autovector level 3 (VERTB/COPER/BLIT) exception vector offset. */
static constexpr uintptr_t VECTOR_LEVEL3 = 0x6c;

#endif
