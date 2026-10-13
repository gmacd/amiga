#!/bin/sh
# Fail if any source uses a bare C integer type (int, short, long, signed,
# unsigned) instead of a <stdint.h> fixed-width type. Comments are stripped
# by the preprocessor (without expanding macros or includes) and string
# literals are ignored, so prose and asm text never trigger it. Line numbers
# are mapped back to the original file through the preprocessor's markers.
# Usage: check-int-types.sh FILE...   (runs in the tools image)
status=0
for f in "$@"; do
  m68k-amigaos-gcc -w -fpreprocessed -dD -E -x c "$f" |
    awk -v file="$f" '
      /^# [0-9]+ "/ { line = $2 - 1; next }
      {
        line++
        code = $0
        gsub(/"([^"\\]|\\.)*"/, "\"\"", code)
        if (code ~ /(^|[^A-Za-z0-9_])(int|short|long|signed|unsigned)([^A-Za-z0-9_]|$)/) {
          printf "%s:%d: bare integer type, use <stdint.h>: %s\n", file, line, $0
          bad = 1
        }
      }
      END { exit bad }' || status=1
done
exit $status
