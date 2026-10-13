/*
 * demo1.c -- Copperline / A1200 bare-metal demo skeleton.
 *
 * Yellow background, a red 16x16 box bouncing off the screen edges, drawn
 * by the blitter into a double-buffered single bitplane, paced by a VBlank
 * interrupt. Runs until the left mouse button is pressed, or for N frames
 * when N is the command-line argument (headless runs), then hands the
 * machine back and returns 0 to the CLI (20 if setup fails).
 *
 * This file touches hardware only. The OS is used through the boundary in
 * system.h: before sys_takeover() and after sys_restore() only.
 *
 * Build (bebbo m68k-amigaos-gcc, see Makefile):
 *   make
 * Run:
 *   copperline --model A1200 --run demo1/build/demo1
 */

#include <stddef.h>
#include <stdint.h>

#include "copper.h"
#include "debug.h"
#include "hardware.h"
#include "system.h"

// --- Screen geometry: PAL lo-res, one bitplane, two buffers ---

static constexpr int32_t WIDTH = 320;
static constexpr int32_t HEIGHT = 256;
static constexpr int32_t ROW_BYTES = WIDTH / 8;
static constexpr int32_t ROW_WORDS = WIDTH / 16;
static constexpr int32_t PLANE_SIZE = ROW_BYTES * HEIGHT;
static constexpr int32_t BUFFERS = 2;

// The bouncing box.
static constexpr int32_t BOX = 16;
static constexpr int32_t START_X = 40;
static constexpr int32_t START_Y = 30;
static constexpr int32_t SPEED_X = 3;
static constexpr int32_t SPEED_Y = 2;

static_assert(WIDTH % 16 == 0, "bitplane rows must be whole words");
static_assert(BOX == 16, "blit_box masks assume a 16-pixel-wide box");
static_assert(START_X >= 0 && START_X + BOX <= WIDTH, "box starts on screen");
static_assert(START_Y >= 0 && START_Y + BOX <= HEIGHT, "box starts on screen");
static_assert(HEIGHT < 1024 && ROW_WORDS < 64, "plane fits one BLTSIZE blit");

static constexpr uint16_t COLOR_YELLOW = 0x0ff0;
static constexpr uint16_t COLOR_RED = 0x0f00;

/* Copper list buffer, in words. build_copper() checks it fits, so this is
 * a capacity with headroom, not a count to keep in step. */
static constexpr size_t COPPER_WORDS = 64;

/* Blitter minterms for blit_box(), from hardware/blit.h's truth-table
 * terms (ABC = A and B and C, NABC = not-A and B and C, ...). B is unused,
 * so each term appears with B both set and clear. */
static constexpr uint16_t LF_SET = A_OR_C;          // D = A | C
static constexpr uint16_t LF_CLEAR = NABC | NANBC;  // D = ~A & C
static_assert(LF_SET == 0xfa && LF_CLEAR == 0x0a, "minterms");

// --- VBlank interrupt ---

// Bumped once per frame by the level 3 handler; the main loop waits on it.
volatile uint32_t frame_counter;

/* Level 3 autovector handler. Only VERTB is enabled at this level, but the
 * request bit is checked anyway. Acknowledged twice: a single INTREQ write
 * can be missed on fast 68040/060 machines (the A4000 quirk). Touches no
 * registers, so nothing needs saving. */
// clang-format off
__asm__(
    "    .text\n"
    "    .globl _vblank_handler\n"
    "_vblank_handler:\n"
    "    btst    #5,0xdff01f\n"  // INTREQR bit 5: VERTB
    "    beq.s   1f\n"
    "    move.w  #0x0020,0xdff09c\n"  // INTREQ: clear VERTB
    "    move.w  #0x0020,0xdff09c\n"
    "    addq.l  #1,_frame_counter\n"
    "1:  rte\n");
// clang-format on

extern void vblank_handler(void);

static void frame_wait(void) {
  uint32_t seen = frame_counter;
  while (frame_counter == seen) {
    // spin: the VBlank handler bumps the counter
  }
}

// --- Blitter ---

static void blit_wait(void) {
  (void)CUSTOM->dmaconr;  // A1000 Agnus read quirk
  while (CUSTOM->dmaconr & DMAF_BLTDONE) {
    // spin: BBUSY clears when the blit is done
  }
}

// Zero a whole bitplane: D channel only, minterm 0.
static void blit_clear(uint8_t* plane) {
  blit_wait();
  CUSTOM->bltcon0 = BC0F_DEST;  // D only, minterm 0: write zeros
  CUSTOM->bltcon1 = 0;
  CUSTOM->bltdpt = plane;
  CUSTOM->bltdmod = 0;
  CUSTOM->bltsize = (HEIGHT << 6) | ROW_WORDS;
}

/* Set (fill != 0) or clear the BOXxBOX box at (x, y).
 *
 * A is fed from BLTADAT (all ones) with the first/last word masks shaping
 * the box's horizontal extent, C reads the existing plane, D writes it:
 *   fill:  D = A | C
 *   clear: D = ~A & C
 * The box spans one word when 16-pixel aligned, two otherwise. */
static void blit_box(uint8_t* plane, int32_t x, int32_t y, bool fill) {
  int32_t shift = x & 15;
  uint16_t nwords = shift ? 2 : 1;
  uint16_t fwm = (uint16_t)(0xffff >> shift);
  uint16_t lwm = shift ? (uint16_t)(0xffff << (16 - shift)) : 0xffff;
  uint16_t mod = (uint16_t)(ROW_BYTES - nwords * 2);
  uint8_t* addr = plane + y * ROW_BYTES + ((x >> 4) << 1);

  blit_wait();
  CUSTOM->bltcon0 = BC0F_SRCC | BC0F_DEST | (fill ? LF_SET : LF_CLEAR);
  CUSTOM->bltcon1 = 0;
  CUSTOM->bltafwm = fwm;
  CUSTOM->bltalwm = lwm;
  CUSTOM->bltadat = 0xffff;
  CUSTOM->bltcpt = addr;
  CUSTOM->bltdpt = addr;
  CUSTOM->bltcmod = mod;
  CUSTOM->bltdmod = mod;
  CUSTOM->bltsize = (uint16_t)((BOX << 6) | nwords);
}

// --- Copper list ---

/* Returns the MOVE pair holding the bitplane pointer, for flipping, or
 * nullptr if the list does not fit the buffer. */
static uint16_t* build_copper(uint16_t* buffer, size_t words,
                              const uint8_t* plane) {
  struct copper c = cop_begin(buffer, words);

  cop_move(&c, REG(fmode), 0x0000);    // AGA: OCS-compatible fetch
  cop_move(&c, REG(bplcon0), 0x1200);  // 1 bitplane, colour burst
  cop_move(&c, REG(bplcon1), 0x0000);
  cop_move(&c, REG(bplcon2), 0x0000);
  cop_move(&c, REG(bplcon3), 0x0000);  // AGA: bank 0, 12-bit writes
  cop_move(&c, REG(bplcon4), 0x0011);
  cop_move(&c, REG(bpl1mod), 0x0000);
  cop_move(&c, REG(bpl2mod), 0x0000);
  cop_move(&c, REG(diwstrt), 0x2c81);  // standard PAL 320x256
  cop_move(&c, REG(diwstop), 0x2cc1);
  cop_move(&c, REG(ddfstrt), 0x0038);
  cop_move(&c, REG(ddfstop), 0x00d0);
  uint16_t* bpl1pt = cop_move_ptr(&c, REG(bplpt[0]), plane);
  cop_move(&c, REG(color[0]), COLOR_YELLOW);
  cop_move(&c, REG(color[1]), COLOR_RED);

  return cop_done(&c) ? bpl1pt : nullptr;
}

// --- Input and arguments ---

static bool left_mouse_pressed(void) {
  return !(CIAA->ciapra & CIAF_GAMEPORT0);
}

// Leading decimal number in the CLI arguments, or 0 if there is none.
static uint32_t parse_frames(const char* args, uint32_t length) {
  uint32_t n = 0;
  uint32_t i = 0;

  while (i < length && (args[i] == ' ' || args[i] == '\t')) {
    i++;
  }
  while (i < length && args[i] >= '0' && args[i] <= '9') {
    n = n * 10 + (uint32_t)(args[i++] - '0');
  }
  return n;
}

// --- The demo ---

// int32_t, the 68020's native width, for coordinates and counters; the
// hardware's own widths for registers (uint16_t) and addresses (uintptr_t).
struct box {
  int32_t x, y;
};

static void bounce_loop(uint8_t* planes[BUFFERS], uint16_t* bpl1pt,
                        uint32_t frames) {
  struct box pos = {.x = START_X, .y = START_Y};
  struct box drawn[BUFFERS];  // where each buffer has the box

  for (int32_t i = 0; i < BUFFERS; i++) {
    blit_clear(planes[i]);
    blit_box(planes[i], pos.x, pos.y, true);
    drawn[i] = pos;
  }
  blit_wait();

  int32_t dx = SPEED_X;
  int32_t dy = SPEED_Y;
  int32_t back = 0;  // buffer being drawn, not shown
  for (uint32_t frame = 0; frames == 0 || frame < frames; frame++) {
    frame_wait();
    if (left_mouse_pressed()) {
      DBG("demo1: mouse button at frame %lu\n", frame);
      break;
    }

    pos.x += dx;
    pos.y += dy;
    if (pos.x <= 0) {
      pos.x = 0;
      dx = SPEED_X;
    } else if (pos.x >= WIDTH - BOX) {
      pos.x = WIDTH - BOX;
      dx = -SPEED_X;
    }
    if (pos.y <= 0) {
      pos.y = 0;
      dy = SPEED_Y;
    } else if (pos.y >= HEIGHT - BOX) {
      pos.y = HEIGHT - BOX;
      dy = -SPEED_Y;
    }

    /* The copper already fetched this frame's pointer at VBlank, so the
     * back buffer is invisible until the flip below takes effect next
     * frame. Erase the box this buffer last showed, draw the new one. */
    blit_box(planes[back], drawn[back].x, drawn[back].y, false);
    blit_box(planes[back], pos.x, pos.y, true);
    drawn[back] = pos;
    cop_set_ptr(bpl1pt, planes[back]);
    back ^= 1;
  }
  blit_wait();
}

/* Everything between allocation and freeing: build the display, take the
 * machine, run, give it back. Early returns are safe here because
 * demo_main() owns the memory. */
static int32_t demo_run(struct sys* sys, uint8_t* planes[BUFFERS],
                        uint16_t* copper, uint32_t frames) {
  if (!planes[0] || !planes[1] || !copper) {
    return SYS_RETURN_FAIL;
  }
  uint16_t* bpl1pt = build_copper(copper, COPPER_WORDS, planes[0]);
  if (!bpl1pt) {
    DBG("demo1: copper list overflows its %lu-word buffer\n",
        (uint32_t)COPPER_WORDS);
    return SYS_RETURN_FAIL;
  }
  DBG("demo1: frames=%lu vbr=%p planes=%p,%p copper=%p\n", frames,
      (void*)sys->vbr, planes[0], planes[1], copper);

  // --- hardware phase: no OS calls between takeover and restore ---
  sys_takeover(sys);

  uintptr_t old_vector = *(volatile uintptr_t*)(sys->vbr + VECTOR_LEVEL3);
  *(volatile uintptr_t*)(sys->vbr + VECTOR_LEVEL3) = (uintptr_t)vblank_handler;
  CUSTOM->cop1lc = (uintptr_t)copper;
  CUSTOM->copjmp1 = 0;
  CUSTOM->dmacon =
      DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER | DMAF_BLITTER;
  CUSTOM->intena = INTF_SETCLR | INTF_INTEN | INTF_VERTB;

  bounce_loop(planes, bpl1pt, frames);

  CUSTOM->intena = INTF_INTEN | INTF_VERTB;
  CUSTOM->intreq = INTF_VERTB;
  *(volatile uintptr_t*)(sys->vbr + VECTOR_LEVEL3) = old_vector;

  sys_restore(sys);
  // --- OS phase again ---

  DBG("demo1: done after %lu frames\n", frame_counter);
  return SYS_RETURN_OK;
}

// OS phase: libraries and memory in, demo_run(), memory and libraries out.
int32_t demo_main(const char* args, uint32_t length) {
  struct sys sys;
  int32_t rc = sys_init(&sys);
  if (rc != SYS_RETURN_OK) {
    return rc;
  }

  uint8_t* planes[BUFFERS] = {};
  for (int32_t i = 0; i < BUFFERS; i++) {
    planes[i] = sys_alloc_chip(PLANE_SIZE);
  }
  uint16_t* copper = sys_alloc_chip(COPPER_WORDS * sizeof(uint16_t));

  rc = demo_run(&sys, planes, copper, parse_frames(args, length));

  sys_free_chip(copper, COPPER_WORDS * sizeof(uint16_t));
  for (int32_t i = 0; i < BUFFERS; i++) {
    sys_free_chip(planes[i], PLANE_SIZE);
  }
  sys_exit(&sys);
  return rc;
}
