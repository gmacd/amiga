/* hardware.h -- chip register bases shared by the OS boundary and the demo. */
#ifndef HARDWARE_H
#define HARDWARE_H

#include <exec/types.h>
#include <hardware/cia.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>

#define CUSTOM ((volatile struct Custom*)0xdff000)
#define CIAA ((volatile struct CIA*)0xbfe001)

/* Autovector level 3 (VERTB/COPER/BLIT) exception vector offset. */
static constexpr ULONG VECTOR_LEVEL3 = 0x6c;

#endif
