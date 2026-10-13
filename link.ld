/* link.ld -- linker script for demo1: a freestanding hunk executable.
 *
 * Code first, with the _start entry stub (system.c, section .text.start)
 * at the very first byte, since AmigaDOS enters there. Placing it here,
 * rather than relying on -fno-toplevel-reorder, lets GCC drop unused
 * statics. Nothing else from the toolchain's default script is needed:
 * no libc startup symbols, constructor lists, exception tables or debug
 * sections. */
OUTPUT_FORMAT("amiga")
OUTPUT_ARCH(m68k)

SECTIONS
{
  .text : {
    KEEP(*(.text.start))
    *(.text .text.*)
    *(.rodata .rodata.*)
  }
  .data : { *(.data .data.*) }
  .bss : { *(.bss .bss.*) *(COMMON) }
}

ASSERT(_start == ADDR(.text), "_start must be the first byte of the text hunk")
