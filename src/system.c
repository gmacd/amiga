// system.c -- loader entry and the OS boundary for demo1.
//
// This is the only file that calls the operating system. See system.h for
// the call order the demo follows. start() must be the first code in the
// text hunk: it is emitted into its own .text.start section, which link.ld
// places first.
//
// The Makefile builds this file for the plain 68000, so start() can reject
// an older CPU cleanly before any 68020 code (demo1.c) runs. read_vbr's
// MOVEC is the one 68010 instruction, emitted as raw words since the
// assembler is in 68000 mode; it only runs after AttnFlags says the CPU
// has it.

// clang-format off
__asm__(
    "    .text\n"
    "| uintptr_t read_vbr(struct ExecBase *sysbase): the vector base register.\n"
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
    "    .word   0x4e7a,0x0801       | movec vbr,d0 (68010+; file is 68000)\n"
    "    rte\n");
// clang-format on

#include "system.h"

#include <devices/trackdisk.h>
#include <dos/dosextens.h>
#include <exec/execbase.h>
#include <exec/io.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <exec/types.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>

#include "hardware.h"

static_assert(kSysReturnOk == RETURN_OK && kSysReturnFail == RETURN_FAIL,
              "return codes match dos/dos.h");

struct ExecBase* SysBase;
struct DosLibrary* DOSBase;
struct GfxBase* GfxBase;

extern uintptr_t read_vbr(struct ExecBase* sysbase);

// Print to the CLI window. A Workbench start has no output stream, and the
// message is dropped.
static void cli_print(const char* text, size_t length) {
  DOSBase = (struct DosLibrary*)OpenLibrary("dos.library", 33);
  if (DOSBase == nullptr) {
    return;
  }
  BPTR out = Output();
  if (out != 0) {
    Write(out, text, (LONG)length);
  }
  CloseLibrary((struct Library*)DOSBase);
  DOSBase = nullptr;
}

// AmigaDOS enters at the top of the first hunk with a0 = argument bytes and
// d0 = their length, and takes the CLI return code from d0. The C calling
// convention already preserves the shell's d2-d7/a2-a6, so no asm stub is
// needed: the parameters are bound to the loader's registers instead.
// Started from Workbench (no CLI structure), a0/d0 are meaningless; the
// process must collect its startup message and reply to it on exit, after
// Forbid(), so Workbench does not unload the code too early. The demo
// itself is 68020 code, so a 68000 or 68010 gets a message and RETURN_FAIL.
[[gnu::section(".text.start")]]
int32_t start(const char* args IN_REG("a0"), uint32_t length IN_REG("d0")) {
  SysBase = *(struct ExecBase**)4;

  struct Process* self = (struct Process*)FindTask(nullptr);
  struct Message* wb_msg = nullptr;
  if (self->pr_CLI == 0) {
    WaitPort(&self->pr_MsgPort);
    wb_msg = GetMsg(&self->pr_MsgPort);
    args = "";
    length = 0;
  }

  int32_t rc = kSysReturnFail;
  if (SysBase->AttnFlags & AFF_68020) {
    rc = demo_main(args, length);
  } else {
    static constexpr char kNeeds68020[] = "demo1 needs a 68020 or better\n";
    cli_print(kNeeds68020, sizeof kNeeds68020 - 1);
  }

  if (wb_msg != nullptr) {
    Forbid();
    ReplyMsg(wb_msg);
  }
  return rc;
}

int32_t sys_init(struct sys* s) {
  *s = (struct sys){};

  GfxBase = (struct GfxBase*)OpenLibrary("graphics.library", 33);
  if (GfxBase == nullptr) {
    return kSysReturnFail;
  }
  s->gfx = GfxBase;

  // 68010 and later have a relocatable vector table.
  if (SysBase->AttnFlags & AFF_68010) {
    s->vbr = read_vbr(SysBase);
  }
  return kSysReturnOk;
}

void* sys_alloc_chip(size_t bytes) {
  return AllocMem(bytes, MEMF_CHIP | MEMF_CLEAR);
}

void sys_free_chip(void* p, size_t bytes) {
  if (p != nullptr) {
    FreeMem(p, bytes);
  }
}

// Let every floppy drive finish its I/O and switch its motor off. Asking
// trackdisk.device (a TD_MOTOR off request per unit) rather than writing
// CIA-B directly means the request waits behind any transfer already queued,
// and trackdisk's own record of the motor stays right. Without this a
// floppy-launched demo keeps the drive running for its whole length, since
// trackdisk's motor-off timer needs the interrupts the takeover disables.
// Units that fail to open have no drive, and are skipped.
static void floppies_quiesce(void) {
  struct MsgPort* port = CreateMsgPort();
  if (port == nullptr) {
    return;
  }
  struct IOStdReq* io =
      (struct IOStdReq*)CreateIORequest(port, sizeof(struct IOStdReq));
  if (io != nullptr) {
    for (uint32_t unit = 0; unit < NUMUNITS; unit++) {
      if (OpenDevice(TD_NAME, unit, (struct IORequest*)io, 0) == 0) {
        io->io_Command = TD_MOTOR;
        io->io_Length = 0;  // 0 = motor off
        DoIO((struct IORequest*)io);
        CloseDevice((struct IORequest*)io);
      }
    }
    DeleteIORequest((struct IORequest*)io);
  }
  DeleteMsgPort(port);
}

void sys_takeover(struct sys* s) {
  // OS phase still: these calls may wait, and Wait() would break a Forbid().
  floppies_quiesce();

  // No other task runs from here until sys_restore() permits it again.
  // WaitTOF() and OwnBlitter() below can still wait, which lets other tasks
  // run briefly; that window closes once interrupts are off.
  Forbid();

  // Take the blitter from the OS and let any blit it started finish, so
  // switching DMA off below cannot cut one short.
  OwnBlitter();
  WaitBlit();

  s->old_view = s->gfx->ActiView;
  LoadView(nullptr);
  WaitTOF();
  WaitTOF();

  s->old_dmacon = CUSTOM->dmaconr;
  s->old_intena = CUSTOM->intenar;
  s->old_adkcon = CUSTOM->adkconr;
  CUSTOM->intena = kIntfAllSources;  // every interrupt source off
  CUSTOM->intreq = kIntfAllSources;  // and nothing left pending
  CUSTOM->dmacon = kDmafAllBits;     // every DMA channel off
}

void sys_restore(struct sys* s) {
  CUSTOM->dmacon = kDmafAllBits;
  CUSTOM->intena = kIntfAllSources;
  // Drop every request the demo's hardware raised (blitter done, copper,
  // CIAs...), or the OS's handlers take them as spurious interrupts the
  // moment their sources are enabled again.
  CUSTOM->intreq = kIntfAllSources;
  CUSTOM->adkcon = kAdkconAllBits;
  CUSTOM->adkcon = ADKF_SETCLR | s->old_adkcon;

  CUSTOM->cop1lc = (uintptr_t)s->gfx->copinit;
  CUSTOM->copjmp1 = 0;
  CUSTOM->dmacon = DMAF_SETCLR | (s->old_dmacon & kDmafWritable);
  CUSTOM->intena = INTF_SETCLR | (s->old_intena & kIntfAllSources);
  LoadView(s->old_view);
  WaitTOF();
  WaitTOF();
  DisownBlitter();
  Permit();
}

void sys_exit(struct sys* s) {
  if (s->gfx != nullptr) {
    CloseLibrary((struct Library*)s->gfx);
  }
  s->gfx = nullptr;
}
