/* system.h -- the OS boundary.
 *
 * Everything that talks to exec, dos or graphics.library lives in system.c.
 * The demo (demo1.c) only touches hardware and calls these in a fixed
 * order:
 *
 *   sys_init          OS calls allowed: libraries opened, VBR known
 *   sys_alloc_chip    OS calls allowed: chip memory for planes and copper
 *   sys_takeover      --- OS parked, interrupts and DMA off ---
 *        hardware only from here: no OS call of any kind
 *   sys_restore       --- OS back: display, DMA and interrupts restored ---
 *   sys_free_chip     OS calls allowed again
 *   sys_exit
 *
 * The CLI/Workbench entry stub is also in system.c; it is the only
 * loader-specific code, so a bootblock or trackmo build replaces that file
 * and keeps demo1.c as is. */
#ifndef SYSTEM_H
#define SYSTEM_H

#include <exec/types.h>

struct sys {
  struct GfxBase* gfx;
  ULONG vbr; /* vector base: 0 on 68000, else from MOVEC */
  struct View* old_view;
  UWORD old_dmacon;
  UWORD old_intena;
};

/* Open libraries and read the VBR. Returns 0, or a CLI return code. */
[[nodiscard]] LONG sys_init(struct sys* s);

/* Cleared chip memory, or nullptr. */
[[nodiscard]] void* sys_alloc_chip(ULONG bytes);
void sys_free_chip(void* p, ULONG bytes);

/* Park the OS display and switch every interrupt and DMA channel off.
 * After this the demo owns the machine. */
void sys_takeover(struct sys* s);

/* Give the machine back: OS copper list, saved DMA and interrupt enables,
 * previous view. The demo must have switched its own interrupt sources
 * off and finished its blits first. */
void sys_restore(struct sys* s);

void sys_exit(struct sys* s);

/* Loader side: called by the _start stub with the CLI argument string. */
LONG entry(const char* args, ULONG length);

/* The demo's entry point, called by entry() with the CLI argument string
 * (empty when started from Workbench). Returns the CLI return code. */
LONG demo_main(const char* args, ULONG length);

#endif
