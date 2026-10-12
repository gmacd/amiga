/*
 * system.c -- loader entry and the OS boundary for demo1.
 *
 * This is the only file that calls the operating system. See system.h for
 * the call order the demo follows. The `_start` stub must be the first code
 * in the text hunk: this file is linked first and the stub precedes every
 * include, with -fno-toplevel-reorder keeping it in place.
 */

/*
 * CLI entry stub. AmigaDOS enters at the top of the first hunk with a0 =
 * argument bytes and d0 = their length. Preserve the shell's callee-saved
 * registers and call entry(args, length); its d0 result is the CLI return
 * code. A Workbench start enters the same way with meaningless a0/d0, which
 * entry() detects and ignores.
 */
__asm__(
    "    .text\n"
    "    .globl _start\n"
    "_start:\n"
    "    movem.l d2-d7/a2-a6,-(sp)\n"
    "    move.l  d0,-(sp)\n"
    "    move.l  a0,-(sp)\n"
    "    jsr     _entry\n"
    "    addq.l  #8,sp\n"
    "    movem.l (sp)+,d2-d7/a2-a6\n"
    "    rts\n"
    "\n"
    "| ULONG read_vbr(struct ExecBase *sysbase): the vector base register.\n"
    "| MOVEC is privileged, so the read runs under exec's Supervisor()\n"
    "| (LVO -30, function in a5, ends with RTE). The NDK's inline wrapper\n"
    "| for Supervisor() is not prototype-clean, hence the direct call.\n"
    "| 68010+ only; the caller checks AttnFlags.\n"
    "    .globl _read_vbr\n"
    "_read_vbr:\n"
    "    movem.l a5/a6,-(sp)\n"
    "    move.l  12(sp),a6\n"
    "    lea     read_vbr_super(pc),a5\n"
    "    jsr     -30(a6)\n"
    "    movem.l (sp)+,a5/a6\n"
    "    rts\n"
    "read_vbr_super:\n"
    "    movec   vbr,d0\n"
    "    rte\n");

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <dos/dosextens.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <proto/exec.h>
#include <proto/graphics.h>

#include "hardware.h"
#include "mem.h"
#include "system.h"

struct ExecBase *SysBase;
struct GfxBase *GfxBase;

extern ULONG read_vbr(struct ExecBase *sysbase);

/*
 * Loader-side wrapper around demo_main(). Started from Workbench (no CLI
 * structure) the process must collect its startup message and reply to it
 * on exit, after Forbid(), so Workbench does not unload the code too early.
 */
LONG entry(const char *args, ULONG length)
{
    struct Process *self;
    struct Message *wb_msg = nullptr;
    LONG rc;

    SysBase = *(struct ExecBase **)4;
    self = (struct Process *)FindTask(nullptr);
    if (self->pr_CLI == 0) {
        WaitPort(&self->pr_MsgPort);
        wb_msg = GetMsg(&self->pr_MsgPort);
        args = "";
        length = 0;
    }

    rc = demo_main(args, length);

    if (wb_msg) {
        Forbid();
        ReplyMsg(wb_msg);
    }
    return rc;
}

LONG sys_init(struct sys *s)
{
    mem_set(s, 0, sizeof(*s));

    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 33);
    if (!GfxBase)
        return 20;
    s->gfx = GfxBase;

    /* 68010 and later have a relocatable vector table. */
    if (SysBase->AttnFlags & AFF_68010)
        s->vbr = read_vbr(SysBase);
    return 0;
}

void *sys_alloc_chip(ULONG bytes)
{
    return AllocMem(bytes, MEMF_CHIP | MEMF_CLEAR);
}

void sys_free_chip(void *p, ULONG bytes)
{
    if (p)
        FreeMem(p, bytes);
}

void sys_takeover(struct sys *s)
{
    s->old_view = s->gfx->ActiView;
    LoadView(nullptr);
    WaitTOF();
    WaitTOF();

    s->old_dmacon = CUSTOM->dmaconr;
    s->old_intena = CUSTOM->intenar;
    CUSTOM->intena = 0x7fff;          /* every interrupt source off */
    CUSTOM->intreq = 0x7fff;          /* and nothing left pending   */
    CUSTOM->dmacon = 0x7fff;          /* every DMA channel off      */
}

void sys_restore(struct sys *s)
{
    CUSTOM->dmacon = 0x7fff;
    CUSTOM->cop1lc = (ULONG)s->gfx->copinit;
    CUSTOM->copjmp1 = 0;
    CUSTOM->dmacon = DMAF_SETCLR | (s->old_dmacon & 0x07ff);
    CUSTOM->intena = INTF_SETCLR | (s->old_intena & 0x7fff);
    LoadView(s->old_view);
    WaitTOF();
    WaitTOF();
}

void sys_exit(struct sys *s)
{
    if (s->gfx)
        CloseLibrary((struct Library *)s->gfx);
    s->gfx = nullptr;
}
