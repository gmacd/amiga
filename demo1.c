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
 *   copperline --model A1200 --run demo1/demo1
 */

#include <exec/types.h>
#include <graphics/gfxbase.h>

#include "hardware.h"
#include "copper.h"
#include "debug.h"
#include "system.h"

/* ------------------------------------------------------------------ */
/* Screen geometry: PAL lo-res, one bitplane, two buffers.             */

#define WIDTH      320
#define HEIGHT     256
#define ROW_BYTES  (WIDTH / 8)
#define ROW_WORDS  (WIDTH / 16)
#define PLANE_SIZE (ROW_BYTES * HEIGHT)
#define BUFFERS    2

/* The bouncing box. */
#define BOX        16
#define START_X    40
#define START_Y    30
#define SPEED_X    3
#define SPEED_Y    2

/* Custom register offsets for the copper list. */
#define REG_BPLCON0 0x100
#define REG_BPLCON1 0x102
#define REG_BPLCON2 0x104
#define REG_BPLCON3 0x106
#define REG_BPL1MOD 0x108
#define REG_BPL2MOD 0x10a
#define REG_BPLCON4 0x10c
#define REG_DIWSTRT 0x08e
#define REG_DIWSTOP 0x090
#define REG_DDFSTRT 0x092
#define REG_DDFSTOP 0x094
#define REG_BPL1PTH 0x0e0
#define REG_BPL1PTL 0x0e2
#define REG_COLOR00 0x180
#define REG_COLOR01 0x182
#define REG_FMODE   0x1fc

#define COLOR_YELLOW 0x0ff0
#define COLOR_RED    0x0f00

#define COPPER_WORDS 64       /* room for the list below, with headroom */

/* ------------------------------------------------------------------ */
/* VBlank interrupt.                                                   */

/* Bumped once per frame by the level 3 handler; the main loop waits on it. */
volatile ULONG frame_counter;

/* Level 3 autovector handler. Only VERTB is enabled at this level, but the
 * request bit is checked anyway. Acknowledged twice: a single INTREQ write
 * can be missed on fast 68040/060 machines (the A4000 quirk). Touches no
 * registers, so nothing needs saving. */
__asm__(
    "    .text\n"
    "    .globl _vblank_handler\n"
    "_vblank_handler:\n"
    "    btst    #5,0xdff01f\n"              /* INTREQR bit 5: VERTB  */
    "    beq.s   1f\n"
    "    move.w  #0x0020,0xdff09c\n"         /* INTREQ: clear VERTB   */
    "    move.w  #0x0020,0xdff09c\n"
    "    addq.l  #1,_frame_counter\n"
    "1:  rte\n");

extern void vblank_handler(void);

static void wait_frame(void)
{
    ULONG seen = frame_counter;
    while (frame_counter == seen)
        ;
}

/* ------------------------------------------------------------------ */
/* Blitter.                                                            */

static void blit_wait(void)
{
    (void)CUSTOM->dmaconr;                    /* A1000 Agnus read quirk */
    while (CUSTOM->dmaconr & DMAF_BLTDONE)
        ;
}

/* Zero a whole bitplane: D channel only, minterm 0. */
static void blit_clear(UBYTE *plane)
{
    blit_wait();
    CUSTOM->bltcon0 = 0x0100;                 /* USED, LF = 0           */
    CUSTOM->bltcon1 = 0;
    CUSTOM->bltdpt = plane;
    CUSTOM->bltdmod = 0;
    CUSTOM->bltsize = (HEIGHT << 6) | ROW_WORDS;
}

/* Set (fill != 0) or clear the BOXxBOX box at (x, y).
 *
 * A is fed from BLTADAT (all ones) with the first/last word masks shaping
 * the box's horizontal extent, C reads the existing plane, D writes it:
 *   fill:  D = A | C      (LF 0xfa)
 *   clear: D = ~A & C     (LF 0x0a)
 * The box spans one word when 16-pixel aligned, two otherwise. */
static void blit_box(UBYTE *plane, int x, int y, int fill)
{
    int shift = x & 15;
    UWORD nwords = shift ? 2 : 1;
    UWORD fwm = (UWORD)(0xffff >> shift);
    UWORD lwm = shift ? (UWORD)(0xffff << (16 - shift)) : 0xffff;
    UWORD mod = (UWORD)(ROW_BYTES - nwords * 2);
    UBYTE *addr = plane + y * ROW_BYTES + ((x >> 4) << 1);

    blit_wait();
    CUSTOM->bltcon0 = 0x0300 | (fill ? 0xfa : 0x0a);   /* USEC | USED */
    CUSTOM->bltcon1 = 0;
    CUSTOM->bltafwm = fwm;
    CUSTOM->bltalwm = lwm;
    CUSTOM->bltadat = 0xffff;
    CUSTOM->bltcpt = addr;
    CUSTOM->bltdpt = addr;
    CUSTOM->bltcmod = mod;
    CUSTOM->bltdmod = mod;
    CUSTOM->bltsize = (UWORD)((BOX << 6) | nwords);
}

/* ------------------------------------------------------------------ */
/* Copper list.                                                        */

/* Returns the MOVE pair holding the bitplane pointer, for flipping. */
static UWORD *build_copper(UWORD *cop, const UBYTE *plane)
{
    UWORD *p = cop;
    UWORD *bpl1pt;

    p = cop_move(p, REG_FMODE,   0x0000);     /* AGA: OCS-compatible fetch */
    p = cop_move(p, REG_BPLCON0, 0x1200);     /* 1 bitplane, colour burst  */
    p = cop_move(p, REG_BPLCON1, 0x0000);
    p = cop_move(p, REG_BPLCON2, 0x0000);
    p = cop_move(p, REG_BPLCON3, 0x0000);     /* AGA: bank 0, 12-bit writes */
    p = cop_move(p, REG_BPLCON4, 0x0011);
    p = cop_move(p, REG_BPL1MOD, 0x0000);
    p = cop_move(p, REG_BPL2MOD, 0x0000);
    p = cop_move(p, REG_DIWSTRT, 0x2c81);     /* standard PAL 320x256      */
    p = cop_move(p, REG_DIWSTOP, 0x2cc1);
    p = cop_move(p, REG_DDFSTRT, 0x0038);
    p = cop_move(p, REG_DDFSTOP, 0x00d0);
    bpl1pt = p;
    p = cop_move(p, REG_BPL1PTH, 0x0000);
    p = cop_move(p, REG_BPL1PTL, 0x0000);
    cop_set_ptr(bpl1pt, plane);
    p = cop_move(p, REG_COLOR00, COLOR_YELLOW);
    p = cop_move(p, REG_COLOR01, COLOR_RED);
    p = cop_end(p);

    return bpl1pt;
}

/* ------------------------------------------------------------------ */
/* Input and arguments.                                                */

static int left_mouse_pressed(void)
{
    return !(CIAA->ciapra & CIAF_GAMEPORT0);
}

/* Leading decimal number in the CLI arguments, or 0 if there is none. */
static ULONG parse_frames(const char *args, ULONG length)
{
    ULONG n = 0;
    ULONG i = 0;

    while (i < length && (args[i] == ' ' || args[i] == '\t'))
        i++;
    while (i < length && args[i] >= '0' && args[i] <= '9')
        n = n * 10 + (ULONG)(args[i++] - '0');
    return n;
}

/* ------------------------------------------------------------------ */
/* The demo.                                                           */

struct box {
    int x, y;
};

static void run(UBYTE *planes[BUFFERS], UWORD *bpl1pt, ULONG frames)
{
    struct box pos = { START_X, START_Y };
    struct box drawn[BUFFERS];            /* where each buffer has the box */
    int dx = SPEED_X, dy = SPEED_Y;
    int back = 0;                         /* buffer being drawn, not shown */
    ULONG frame;
    int i;

    for (i = 0; i < BUFFERS; i++) {
        blit_clear(planes[i]);
        blit_box(planes[i], pos.x, pos.y, 1);
        drawn[i] = pos;
    }
    blit_wait();

    for (frame = 0; frames == 0 || frame < frames; frame++) {
        wait_frame();
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
        blit_box(planes[back], drawn[back].x, drawn[back].y, 0);
        blit_box(planes[back], pos.x, pos.y, 1);
        drawn[back] = pos;
        cop_set_ptr(bpl1pt, planes[back]);
        back ^= 1;
    }
    blit_wait();
}

LONG demo_main(const char *args, ULONG length)
{
    struct sys sys;
    UBYTE *planes[BUFFERS] = { NULL, NULL };
    UWORD *copper = NULL;
    UWORD *bpl1pt;
    ULONG frames = parse_frames(args, length);
    ULONG old_vector;
    LONG rc;
    int i;

    /* --- OS phase: libraries, memory, loading ----------------------- */
    rc = sys_init(&sys);
    if (rc)
        return rc;
    for (i = 0; i < BUFFERS; i++)
        planes[i] = sys_alloc_chip(PLANE_SIZE);
    copper = sys_alloc_chip(COPPER_WORDS * sizeof(UWORD));
    if (!planes[0] || !planes[1] || !copper) {
        rc = 20;
        goto out;
    }
    bpl1pt = build_copper(copper, planes[0]);
    DBG("demo1: frames=%lu vbr=%p planes=%p,%p copper=%p\n",
        frames, (void *)sys.vbr, planes[0], planes[1], copper);

    /* --- hardware phase: no OS calls between these two lines -------- */
    sys_takeover(&sys);

    old_vector = *(volatile ULONG *)(sys.vbr + VECTOR_LEVEL3);
    *(volatile ULONG *)(sys.vbr + VECTOR_LEVEL3) = (ULONG)vblank_handler;
    CUSTOM->cop1lc = (ULONG)copper;
    CUSTOM->copjmp1 = 0;
    CUSTOM->dmacon = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER
                   | DMAF_BLITTER;
    CUSTOM->intena = INTF_SETCLR | INTF_INTEN | INTF_VERTB;

    run(planes, bpl1pt, frames);

    CUSTOM->intena = INTF_INTEN | INTF_VERTB;
    CUSTOM->intreq = INTF_VERTB;
    *(volatile ULONG *)(sys.vbr + VECTOR_LEVEL3) = old_vector;

    sys_restore(&sys);
    /* --- OS phase again ----------------------------------------------- */

    DBG("demo1: done after %lu frames\n", frame_counter);
    rc = 0;
out:
    sys_free_chip(copper, COPPER_WORDS * sizeof(UWORD));
    for (i = 0; i < BUFFERS; i++)
        sys_free_chip(planes[i], PLANE_SIZE);
    sys_exit(&sys);
    return rc;
}
