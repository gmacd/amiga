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

#include <stddef.h>
#include <stdint.h>

struct GfxBase;
struct View;

/* CLI return codes (AmigaDOS RETURN_OK and RETURN_FAIL, checked against
 * dos/dos.h in system.c so the demo side needs no NDK header). */
/* An enum rather than constexpr objects: system.c builds with
 * -fno-toplevel-reorder, which would emit unused objects as data. */
enum : int32_t { SYS_RETURN_OK = 0, SYS_RETURN_FAIL = 20 };

struct sys {
  struct GfxBase* gfx;
  uintptr_t vbr;  // vector base: 0 on 68000, else from MOVEC
  struct View* old_view;
  uint16_t old_dmacon;
  uint16_t old_intena;
};

/* Open libraries and read the VBR. Returns SYS_RETURN_OK, or a CLI
 * return code to exit with. */
[[nodiscard]] int32_t sys_init(struct sys* s);

// Cleared chip memory, or nullptr.
[[nodiscard]] void* sys_alloc_chip(size_t bytes);
void sys_free_chip(void* p, size_t bytes);

/* Park the OS display and switch every interrupt and DMA channel off.
 * After this the demo owns the machine. */
void sys_takeover(struct sys* s);

/* Give the machine back: OS copper list, saved DMA and interrupt enables,
 * previous view. The demo must have switched its own interrupt sources
 * off and finished its blits first. */
void sys_restore(struct sys* s);

void sys_exit(struct sys* s);

// Loader side: called by the _start stub with the CLI argument string.
int32_t entry(const char* args, uint32_t length);

/* The demo's entry point, called by entry() with the CLI argument string
 * (empty when started from Workbench). Returns the CLI return code. */
int32_t demo_main(const char* args, uint32_t length);

#endif
