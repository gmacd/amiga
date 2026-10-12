/* debug.h -- printf-style debugging while the OS is frozen.
 *
 * DBG(fmt, ...) writes through Copperline's memory-mapped debug port
 * (docs/guide/run.md, "Memory-mapped debug output"): each argument is
 * written to $BFFF00, then the format pointer to $BFFF04, and the host
 * prints "DBG: ..." on its console. No OS, no libc, safe with interrupts
 * and DMA off. On real hardware the writes land in unmapped space and do
 * nothing. Build with -DNDEBUG to compile every DBG() out.
 *
 * Conversions follow the port, not printf: %d/%u/%x are 16-bit, so use
 * %ld/%lu/%lx for int and long (both 32-bit here), %p for pointers, %s for
 * strings. Each conversion consumes one 32-bit argument; '*' widths are
 * not supported. */
#ifndef DEBUG_H
#define DEBUG_H

#ifdef NDEBUG

#define DBG(...) ((void)0)

#else

#include <exec/types.h>
#include <stdarg.h>

#define DBG_ARG_PORT ((volatile ULONG *)0xbfff00)
#define DBG_FMT_PORT ((volatile ULONG *)0xbfff04)

static inline void dbg_printf(const char *fmt, ...)
{
    va_list ap;
    const char *s;

    va_start(ap, fmt);
    for (s = fmt; *s; s++) {
        if (*s != '%')
            continue;
        s++;
        if (*s == '%')
            continue;                       /* literal percent */
        if (*s == '\0')
            break;
        *DBG_ARG_PORT = va_arg(ap, ULONG);  /* one argument per conversion */
    }
    va_end(ap);
    *DBG_FMT_PORT = (ULONG)fmt;
}

#define DBG(...) dbg_printf(__VA_ARGS__)

#endif /* NDEBUG */

#endif
