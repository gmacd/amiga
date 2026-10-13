// demo1.c -- Copperline / A1200 bare-metal demo skeleton.
//
// Yellow background, a red 16x16 box bouncing off the screen edges, drawn
// by the blitter into a double-buffered single bitplane, paced by a VBlank
// interrupt. Runs until the left mouse button is pressed, or for N frames
// when N is the command-line argument (headless runs), then hands the
// machine back and returns 0 to the CLI (20 if setup fails).
//
// This file touches hardware only. The OS is used through the boundary in
// system.h: before sys_takeover() and after sys_restore() only.
//
// Build (bebbo m68k-amigaos-gcc, see Makefile):
//   make
// Run:
//   copperline --model A1200 --run demo1/build/demo1

#include <stddef.h>
#include <stdint.h>

#include "copper.h"
#include "debug.h"
#include "hardware.h"
#include "system.h"

// --- Screen geometry: PAL lo-res, one bitplane, two buffers ---

static constexpr int32_t kWidth = 320;
static constexpr int32_t kHeight = 256;
static constexpr int32_t kRowBytes = kWidth / 8;
static constexpr int32_t kRowWords = kWidth / 16;
static constexpr int32_t kPlaneSize = kRowBytes * kHeight;
static constexpr int32_t kBuffers = 2;  // drawn one frame, shown the next

// The bouncing box.
static constexpr int32_t kBox = 16;
static constexpr int32_t kStartX = 40;
static constexpr int32_t kStartY = 30;
static constexpr int32_t kSpeedX = 3;
static constexpr int32_t kSpeedY = 2;

static_assert(kWidth % 16 == 0, "bitplane rows must be whole words");
static_assert(kBox == 16, "blit_box masks assume a 16-pixel-wide box");
static_assert(kStartX >= 0 && kStartX + kBox <= kWidth, "box starts on screen");
static_assert(kStartY >= 0 && kStartY + kBox <= kHeight,
              "box starts on screen");
static_assert(kHeight < 1024 && kRowWords < 64, "plane fits one BLTSIZE blit");
static_assert(kBuffers >= 2, "drawing needs a buffer that is not on screen");

static constexpr uint16_t kColorYellow = 0x0ff0;
static constexpr uint16_t kColorRed = 0x0f00;

// Copper list buffer, in words. build_copper() checks it fits, so this is
// a capacity with headroom, not a count to keep in step.
static constexpr size_t kCopperWords = 64;

// What blit_box() does to the box, as its blitter minterm, so the
// operation drops straight into BLTCON0. Built from hardware/blit.h's
// truth-table terms (ABC = A and B and C, NABC = not-A and B and C, ...).
// B is unused, so each term appears with B both set and clear.
enum box_op : uint16_t {
  kBoxSet = A_OR_C,          // D = A | C
  kBoxClear = NABC | NANBC,  // D = ~A & C
};
static_assert(kBoxSet == 0xfa && kBoxClear == 0x0a, "minterms");

// --- VBlank interrupt ---

// Bumped once per frame by the level 3 handler; the main loop waits on it.
static volatile uint32_t frame_counter;

// Level 3 autovector handler. Only VERTB is enabled at this level, but the
// request bit is checked anyway. Acknowledged twice: a single INTREQ write
// can be missed on fast 68040/060 machines (the A4000 quirk).
INTERRUPT_HANDLER static void vblank_handler(void) {
  if (CUSTOM->intreqr & INTF_VERTB) {
    CUSTOM->intreq = INTF_VERTB;
    CUSTOM->intreq = INTF_VERTB;
    frame_counter++;
  }
}

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
static void blit_clear(uint8_t plane[static kPlaneSize]) {
  blit_wait();
  CUSTOM->bltcon0 = BC0F_DEST;  // D only, minterm 0: write zeros
  CUSTOM->bltcon1 = 0;
  CUSTOM->bltdpt = plane;
  CUSTOM->bltdmod = 0;
  CUSTOM->bltsize = (kHeight << 6) | kRowWords;
}

// Set or clear the kBox x kBox box at (x, y).
//
// A is fed from BLTADAT (all ones) with the first/last word masks shaping
// the box's horizontal extent, C reads the existing plane, D writes it:
// D = op(A, C), with A set where the box is.
// The box spans one word when 16-pixel aligned, two otherwise.
static void blit_box(uint8_t plane[static kPlaneSize], int32_t x, int32_t y,
                     enum box_op op) {
  int32_t shift = x & 15;
  uint16_t nwords = shift ? 2 : 1;
  uint16_t fwm = (uint16_t)(0xffff >> shift);
  uint16_t lwm = shift ? (uint16_t)(0xffff << (16 - shift)) : 0xffff;
  uint16_t mod = (uint16_t)(kRowBytes - nwords * 2);
  uint8_t* addr = plane + y * kRowBytes + ((x >> 4) << 1);

  blit_wait();
  CUSTOM->bltcon0 = BC0F_SRCC | BC0F_DEST | op;
  CUSTOM->bltcon1 = 0;
  CUSTOM->bltafwm = fwm;
  CUSTOM->bltalwm = lwm;
  CUSTOM->bltadat = 0xffff;
  CUSTOM->bltcpt = addr;
  CUSTOM->bltdpt = addr;
  CUSTOM->bltcmod = mod;
  CUSTOM->bltdmod = mod;
  CUSTOM->bltsize = (uint16_t)((kBox << 6) | nwords);
}

// --- Copper list ---

// Returns the MOVE pair holding the bitplane pointer, for flipping, or
// nullptr if the list does not fit the buffer.
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
  cop_move(&c, REG(color[0]), kColorYellow);
  cop_move(&c, REG(color[1]), kColorRed);

  return cop_done(&c) ? bpl1pt : nullptr;
}

// --- Input and arguments ---

static bool left_mouse_pressed(void) {
  return !(CIAA->ciapra & CIAF_GAMEPORT0);
}

// Leading decimal number in the CLI arguments. No number, or 0, means run
// until the mouse button: UINT32_MAX frames, over two years at 50 Hz.
static uint32_t parse_frames(const char* args, uint32_t length) {
  uint32_t n = 0;
  uint32_t i = 0;

  while (i < length && (args[i] == ' ' || args[i] == '\t')) {
    i++;
  }
  while (i < length && args[i] >= '0' && args[i] <= '9') {
    n = n * 10 + (uint32_t)(args[i++] - '0');
  }
  return n ? n : UINT32_MAX;
}

// --- The demo ---

// int32_t, the 68020's native width, for coordinates and counters; the
// hardware's own widths for registers (uint16_t) and addresses (uintptr_t).
struct box {
  int32_t x, y;
};

static void bounce_loop(uint8_t* const planes[static kBuffers],
                        uint16_t bpl1pt[static 4], uint32_t frames) {
  struct box pos = {.x = kStartX, .y = kStartY};
  struct box drawn[kBuffers];  // where each buffer has the box

  for (int32_t i = 0; i < kBuffers; i++) {
    blit_clear(planes[i]);
    blit_box(planes[i], pos.x, pos.y, kBoxSet);
    drawn[i] = pos;
  }
  blit_wait();

  int32_t dx = kSpeedX;
  int32_t dy = kSpeedY;
  int32_t back = 0;  // buffer being drawn, not shown
  for (uint32_t frame = 0; frame < frames; frame++) {
    frame_wait();
    if (left_mouse_pressed()) {
      DBG("demo1: mouse button at frame %lu\n", frame);
      break;
    }

    pos.x += dx;
    pos.y += dy;
    if (pos.x <= 0) {
      pos.x = 0;
      dx = kSpeedX;
    } else if (pos.x >= kWidth - kBox) {
      pos.x = kWidth - kBox;
      dx = -kSpeedX;
    }
    if (pos.y <= 0) {
      pos.y = 0;
      dy = kSpeedY;
    } else if (pos.y >= kHeight - kBox) {
      pos.y = kHeight - kBox;
      dy = -kSpeedY;
    }

    // The copper already fetched this frame's pointer at VBlank, so the
    // back buffer is invisible until the flip below takes effect next
    // frame. Erase the box this buffer last showed, draw the new one.
    blit_box(planes[back], drawn[back].x, drawn[back].y, kBoxClear);
    blit_box(planes[back], pos.x, pos.y, kBoxSet);
    drawn[back] = pos;
    cop_set_ptr(bpl1pt, planes[back]);
    back = (back + 1) % kBuffers;
  }
  blit_wait();
}

// Everything between allocation and freeing: build the display, take the
// machine, run, give it back. Early returns are safe here because
// demo_main() owns the memory.
static int32_t demo_run(struct sys* sys, uint8_t* const planes[static kBuffers],
                        uint16_t* copper, uint32_t frames) {
  for (int32_t i = 0; i < kBuffers; i++) {
    if (!planes[i]) {
      return kSysReturnFail;
    }
  }
  if (!copper) {
    return kSysReturnFail;
  }
  uint16_t* bpl1pt = build_copper(copper, kCopperWords, planes[0]);
  if (!bpl1pt) {
    DBG("demo1: copper list overflows its %lu-word buffer\n",
        (uint32_t)kCopperWords);
    return kSysReturnFail;
  }
  DBG("demo1: frames=%lu vbr=%p copper=%p\n", frames, (void*)sys->vbr,
      (void*)copper);
  for (int32_t i = 0; i < kBuffers; i++) {
    DBG("demo1: plane %ld at %p\n", i, (void*)planes[i]);
  }

  // --- hardware phase: no OS calls between takeover and restore ---
  sys_takeover(sys);

  exception_handler old_handler = *vector_slot(sys->vbr, kVectorLevel3);
  *vector_slot(sys->vbr, kVectorLevel3) = vblank_handler;
  CUSTOM->cop1lc = (uintptr_t)copper;
  CUSTOM->copjmp1 = 0;
  CUSTOM->dmacon =
      DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER | DMAF_BLITTER;
  CUSTOM->intena = INTF_SETCLR | INTF_INTEN | INTF_VERTB;

  bounce_loop(planes, bpl1pt, frames);

  CUSTOM->intena = INTF_INTEN | INTF_VERTB;
  CUSTOM->intreq = INTF_VERTB;
  *vector_slot(sys->vbr, kVectorLevel3) = old_handler;

  sys_restore(sys);
  // --- OS phase again ---

  DBG("demo1: done after %lu frames\n", frame_counter);
  return kSysReturnOk;
}

// OS phase: libraries and memory in, demo_run(), memory and libraries out.
int32_t demo_main(const char* args, uint32_t length) {
  struct sys sys;
  int32_t rc = sys_init(&sys);
  if (rc != kSysReturnOk) {
    return rc;
  }

  uint8_t* planes[kBuffers] = {};
  for (int32_t i = 0; i < kBuffers; i++) {
    planes[i] = sys_alloc_chip(kPlaneSize);
  }
  uint16_t* copper = sys_alloc_chip(kCopperWords * sizeof(uint16_t));

  rc = demo_run(&sys, planes, copper, parse_frames(args, length));

  sys_free_chip(copper, kCopperWords * sizeof(uint16_t));
  for (int32_t i = 0; i < kBuffers; i++) {
    sys_free_chip(planes[i], kPlaneSize);
  }
  sys_exit(&sys);
  return rc;
}
