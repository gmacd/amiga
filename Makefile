# test1: bouncing red box on a yellow screen, built with the same dockerized
# bebbo m68k-amigaos-gcc image that Copperline's guest/ programs use.
#
#   make           build test1
#   make run       open Copperline (A1200, bundled AROS ROM) and run it
#   make test      headless: run 300 frames, check two screenshots and the
#                  debug output, expect exit status 0 (the CLI return code)
#   make bless     regenerate the expected screenshots from a fresh run
#   make clean
#
# Variables: COPPERLINE (binary), MODEL (A1200), ROM (a Kickstart image;
# empty = bundled AROS), CC (a host m68k-amigaos-gcc instead of Docker),
# NDEBUG=1 (compile the DBG() debug output out).
# The blessed screenshots depend on boot timing, so they match the default
# ROM only; re-bless after changing ROM or MODEL.

COPPERLINE ?= ../Copperline/target/release/copperline
MODEL      ?= A1200
ROM        ?=

IMAGE   = stefanreinauer/amiga-gcc:gcc-v16.2
DOCKER  = docker run --rm --platform linux/amd64 --user $(shell id -u):$(shell id -g) \
          -v "$(CURDIR):/src" -w /src $(IMAGE)
CC      = $(DOCKER) m68k-amigaos-gcc

# Freestanding: no libc or startup code. system.c carries the entry stub
# and is linked first so the stub is at the top of the text hunk;
# -fno-toplevel-reorder keeps it there. -fno-tree-loop-distribute-patterns
# stops GCC turning the mem.h loops into memset/memcpy calls. -m68020 is
# the A1200's CPU (native 32-bit multiply). -lgcc covers any helper GCC
# still emits (64-bit arithmetic, division on 68000 builds).
CFLAGS  = -Os -m68020 -fomit-frame-pointer -Wall -Wextra -nostdlib -nostartfiles \
          -ffreestanding -fno-toplevel-reorder -fno-tree-loop-distribute-patterns
ifdef NDEBUG
CFLAGS += -DNDEBUG
endif
LDLIBS  = -lgcc

SRCS    = system.c test1.c
HDRS    = system.h hardware.h copper.h debug.h mem.h

# Headless test: the program runs TEST_FRAMES frames (6 s PAL) and returns
# 0, which --exit-on-return turns into Copperline's exit status. Screenshots
# are checked mid-run; the bounding screenshot at BOUND_AT only fires if the
# program never returns, which ends the run with status 4.
TEST_FRAMES = 300
SHOT_A      = 5
SHOT_B      = 8
BOUND_AT    = 40
RUN_FLAGS   = --factory --noaudio --model $(MODEL) $(ROM) --run test1 \
              --run-args $(TEST_FRAMES) --exit-on-return --native-screenshots

all: test1

test1: $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDLIBS)

run: test1
	$(COPPERLINE) --factory --model $(MODEL) $(ROM) --run test1

test: test1
	$(COPPERLINE) $(RUN_FLAGS) \
	    --expect-screenshot $(SHOT_A) expected-a.png \
	    --expect-screenshot $(SHOT_B) expected-b.png \
	    --screenshot-after $(BOUND_AT) end.png 2>&1 | tee test.log
	@grep -q "DBG: test1: done after" test.log || { echo "test1: FAIL (no debug output)"; exit 1; }
	@echo "test1: PASS"

bless: test1
	$(COPPERLINE) $(RUN_FLAGS) \
	    --screenshot-after $(SHOT_A) expected-a.png \
	    --screenshot-after $(SHOT_B) expected-b.png \
	    --screenshot-after $(BOUND_AT) end.png

clean:
	rm -f test1 end.png test.log expected-a.actual.png expected-a.diff.png \
	      expected-b.actual.png expected-b.diff.png

.PHONY: all run test bless clean
