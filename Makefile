# demo1: bouncing red box on a yellow screen, built with the same dockerized
# bebbo m68k-amigaos-gcc image that Copperline's guest/ programs use.
#
#   make           build build/demo1
#   make run       open Copperline (A1200, bundled AROS ROM) and run it
#   make test      headless: run 300 frames, check two screenshots and the
#                  debug output, expect exit status 0 (the CLI return code)
#   make bless     regenerate the expected screenshots from a fresh run
#   make ndk       copy the NDK headers out of the image into ndk-include/
#                  for editor navigation (see .clangd); gitignored
#   make clean
#
# Variables: COPPERLINE (binary), MODEL (A1200), ROM (a Kickstart image;
# empty = bundled AROS), CC (a host m68k-amigaos-gcc instead of Docker),
# NDEBUG=1 (compile the DBG() debug output out).
# The blessed screenshots depend on boot timing, so they match the default
# ROM only; re-bless after changing ROM or MODEL.

# Recipes that tee a log set pipefail first, so a failing Copperline run is
# not hidden behind the tee (bash: Apple's make 3.81 lacks .SHELLFLAGS).
SHELL = /bin/bash

COPPERLINE ?= ../Copperline/target/release/copperline
MODEL      ?= A1200
ROM        ?=

IMAGE   = stefanreinauer/amiga-gcc:gcc-v16.2
DOCKER  = docker run --rm --platform linux/amd64 --user $(shell id -u):$(shell id -g) \
          -v "$(CURDIR):/src" -w /src $(IMAGE)
CC      = $(DOCKER) m68k-amigaos-gcc

# Freestanding: no libc or startup code. system.c carries the entry stub
# and is linked first so the stub is at the top of the text hunk;
# -fno-toplevel-reorder keeps it there, and applies to system.c only since
# it also stops GCC dropping unreferenced constexpr objects (see
# STUB_CFLAGS). -fno-tree-loop-distribute-patterns stops GCC turning the
# mem.h loops into memset/memcpy calls. -m68020 is the A1200's CPU (native
# 32-bit multiply). -lgcc covers any helper GCC still emits (64-bit
# arithmetic, division on 68000 builds).
CFLAGS  = -Os -m68020 -fomit-frame-pointer -std=c23 -nostdlib -nostartfiles \
          -ffreestanding -fno-tree-loop-distribute-patterns \
          $(WARNINGS)
STUB_CFLAGS = -fno-toplevel-reorder

# Warnings are errors. -fanalyzer is GCC's static analyser; -Wstack-usage
# bounds any one function's frame, since the demo shares the CLI's stack
# with its interrupt handler. c23: the top-level asm is spelt __asm__ for strict ISO mode.
WARNINGS = -Werror -Wall -Wextra -Wpedantic \
           -Wconversion -Wsign-conversion -Wshadow \
           -Wcast-qual -Wcast-align=strict -Wpointer-arith -Wwrite-strings -Wundef \
           -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations \
           -Wold-style-definition -Wredundant-decls \
           -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wnull-dereference \
           -Wjump-misses-init -Wswitch-enum -Wimplicit-fallthrough=5 -Wvla \
           -Wdouble-promotion -Wformat=2 -Wunused-macros \
           -fanalyzer -Wstack-usage=128
ifdef NDEBUG
CFLAGS += -DNDEBUG
endif
LDLIBS  = -lgcc

BUILD   = build
BIN     = $(BUILD)/demo1
OBJS    = $(BUILD)/system.o $(BUILD)/demo1.o
MAP     = $(BUILD)/demo1.map

# Header dependencies: each compile writes a .d file beside its object
# (-MMD, user headers only; -MP adds phony targets so a deleted header
# does not break the build). Included below so a changed header rebuilds
# exactly the objects that use it.
DEPFLAGS = -MMD -MP

# Headless test: the program runs TEST_FRAMES frames (6 s PAL) and returns
# 0, which --exit-on-return turns into Copperline's exit status. Screenshots
# are checked mid-run; the bounding screenshot at BOUND_AT only fires if the
# program never returns, which ends the run with status 4.
TEST_FRAMES = 300
SHOT_A      = 5
SHOT_B      = 8
BOUND_AT    = 40
RUN_FLAGS   = --factory --noaudio --model $(MODEL) $(ROM) --run $(BIN) \
              --run-args $(TEST_FRAMES) --exit-on-return --native-screenshots

all: $(BIN)

# The link map lists every function and object with its size: the first
# place to look when the binary grows.
$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDLIBS) -Wl,-Map=$(MAP)

$(BUILD)/system.o: system.c | $(BUILD)
	$(CC) $(CFLAGS) $(STUB_CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD)/demo1.o: demo1.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

-include $(OBJS:.o=.d)

$(BUILD):
	mkdir -p $@

run: $(BIN)
	$(COPPERLINE) --factory --model $(MODEL) $(ROM) --run $(BIN)

test: $(BIN)
	set -o pipefail; $(COPPERLINE) $(RUN_FLAGS) \
	    --expect-screenshot $(SHOT_A) test_images/expected-a.png \
	    --expect-screenshot $(SHOT_B) test_images/expected-b.png \
	    --screenshot-after $(BOUND_AT) test_images/end.png 2>&1 | tee $(BUILD)/test.log
	@grep -q "DBG: demo1: done after" $(BUILD)/test.log || { echo "demo1: FAIL (no debug output)"; exit 1; }
	@echo "demo1: PASS"

bless: $(BIN)
	$(COPPERLINE) $(RUN_FLAGS) \
	    --screenshot-after $(SHOT_A) test_images/expected-a.png \
	    --screenshot-after $(SHOT_B) test_images/expected-b.png \
	    --screenshot-after $(BOUND_AT) test_images/end.png

# Editor support: the NDK headers live only in the image, so copy them
# out once. host-shim/ replaces the proto/ headers for host tools.
NDK_DIR = ndk-include
ndk:
	docker create --platform linux/amd64 --name demo1-ndk $(IMAGE) >/dev/null
	docker cp demo1-ndk:/opt/amiga-16.2/m68k-amigaos/$(NDK_DIR) ./$(NDK_DIR)
	docker rm demo1-ndk >/dev/null

clean:
	rm -rf $(BUILD)

.PHONY: all run test bless ndk clean
