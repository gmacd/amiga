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

/* Autovector level 3 (VERTB/COPER/BLIT) exception vector offset. */
static constexpr uintptr_t VECTOR_LEVEL3 = 0x6c;

#endif
