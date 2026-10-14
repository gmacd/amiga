# demo1: bouncing red box on a yellow screen, built with the same dockerized
# bebbo m68k-amigaos-gcc image that Copperline's guest/ programs use.
#
#   make           build build/demo1 and its Workbench icon demo1.info
#   make run       open Copperline (A1200, bundled AROS ROM) and run it
#   make test      headless: move the box, hold it still, check a screenshot
#                  taken during the hold and the debug output, expect exit
#                  status 0 (the CLI return code)
#   make bless     regenerate the expected screenshot from a fresh run
#   make adf       build/demo1.adf: a bootable 880K floppy image
#   make test-adf  boot that floppy headless, check a screenshot, exit via
#                  a scheduled mouse click (bless with make bless-adf)
#   make format    clang-format the sources in place (.clang-format)
#   make check-format  fail if any source is not formatted; for CI/hooks
#   make tidy      clang-tidy checks from .clangd (braces around statements)
#   make check-style   fail on bare int/short/long/unsigned or __attribute__
#   make lint      check-format, tidy and check-style together
#   make tools-image   build the image the four targets above use; they
#                  build it themselves when it is missing or the Dockerfile
#                  changes
#   make ndk       copy the NDK headers out of the image into ndk-include/
#                  for editor navigation (see .clangd); gitignored
#   make clean
#
# Variables: COPPERLINE (binary), MODEL (A1200), ROM (a Kickstart image;
# empty = bundled AROS), CC (a host m68k-amigaos-gcc instead of Docker),
# HOST_TOOLCHAIN=1 (compiler and clang tools from the host, no docker),
# NDEBUG=1 (compile the DBG() debug output out).
# The test screenshot is taken while the box is held still, so it does not
# depend on load time; the floppy one does (a floppy boot passes no
# arguments), so re-bless it with bless-adf after the binary changes size.

# Recipes that tee a log set pipefail first, so a failing Copperline run is
# not hidden behind the tee (bash: Apple's make 3.81 lacks .SHELLFLAGS).
SHELL = /bin/bash

COPPERLINE ?= ../Copperline/target/release/copperline
COPPERLINE_CTL ?= ../Copperline/target/release/copperline-ctl
MODEL      ?= A1200
ROM        ?=

# The compiler image, pinned by digest: the gcc-v16.2 tag can move. Also the
# base of the tools image (Dockerfile), so both always match.
IMAGE   = stefanreinauer/amiga-gcc@sha256:c98a0c0bf72d69c4b60334ac3eaafd3e1e63ddca9dfd0a2353f4e17ec284b5c5
# Format/lint tools (Dockerfile): the compiler image plus clang-format and
# clang-tidy, built locally once with `make tools-image`.
TOOLS_IMAGE = demo1-tools
TOOLS   = docker run --rm --platform linux/amd64 --user $(shell id -u):$(shell id -g) \
          -v "$(CURDIR):/src" -w /src $(TOOLS_IMAGE)
DOCKER  = docker run --rm --platform linux/amd64 --user $(shell id -u):$(shell id -g) \
          -v "$(CURDIR):/src" -w /src $(IMAGE)
CC      = $(DOCKER) m68k-amigaos-gcc

# Freestanding: no libc or startup code. system.c's start() sits in its
# own .text.start section, which link.ld places at the top of the text
# hunk. -fno-tree-loop-distribute-patterns stops GCC turning byte loops into
# memset/memcpy calls, which nothing here provides. -m68020 (CPU, below)
# is the A1200's CPU (native 32-bit multiply). -lgcc covers any helper GCC still emits
# (64-bit arithmetic, division on 68000 builds). c23 is strict ISO, so the
# top-level asm blocks are spelt __asm__.
CFLAGS  = -Os $(CPU) -fomit-frame-pointer -std=c23 -nostdlib -nostartfiles \
          -ffreestanding -fno-tree-loop-distribute-patterns \
          $(WARNINGS)
LDSCRIPT = link.ld
# The demo is 68020 code; system.c is plain 68000 (below) so its CPU check
# can refuse an A500 cleanly instead of crashing on the first 68020
# instruction.
CPU = -m68020

# Warnings are errors. -fanalyzer is GCC's static analyser; -Wstack-usage
# bounds any one function's frame, since the demo shares the CLI's stack
# with its interrupt handler.
WARNINGS = -Werror -Wall -Wextra -Wpedantic \
           -Wconversion -Wsign-conversion -Wshadow \
           -Wcast-qual -Wcast-align=strict -Wpointer-arith -Wwrite-strings -Wundef \
           -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations \
           -Wold-style-definition -Wredundant-decls \
           -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wnull-dereference \
           -Wjump-misses-init -Wswitch-enum -Wimplicit-fallthrough=5 -Wvla \
           -Wdouble-promotion -Wformat=2 -Wunused-macros \
           -fanalyzer -Wstack-usage=256
ifdef NDEBUG
CFLAGS += -DNDEBUG
endif
LDLIBS  = -lgcc

SRC     = src
BUILD   = build
BIN     = $(BUILD)/demo1
OBJS    = $(BUILD)/system.o $(BUILD)/demo1.o
MAP     = $(BUILD)/demo1.map
ADF     = $(BUILD)/demo1.adf
ICON    = $(BUILD)/demo1.info

$(BUILD)/system.o: CPU = -m68000

# Header dependencies: each compile writes a .d file beside its object
# (-MMD, user headers only; -MP adds phony targets so a deleted header
# does not break the build). Included below so a changed header rebuilds
# exactly the objects that use it.
DEPFLAGS = -MMD -MP

# Headless test: the program moves the box for TEST_FRAMES frames, holds it
# still for TEST_HOLD more and returns 0, which --exit-on-return turns into
# Copperline's exit status. The program loads at about 3 s, so the hold runs
# from about 5 s to 8 s; SHOT falls in the middle of it, which leaves a
# second or more of margin either way for load-time changes. The bounding
# screenshot at BOUND_AT only fires if the program never returns, which
# ends the run with status 4.
TEST_FRAMES = 100
TEST_HOLD   = 150
SHOT        = 6.5
BOUND_AT    = 40
RUN_FLAGS   = --factory --noaudio --model $(MODEL) $(ROM) --run $(BIN) \
              --run-args "$(TEST_FRAMES) $(TEST_HOLD)" --exit-on-return \
              --native-screenshots

all: $(BIN) $(ICON)

# The link map lists every function and object with its size: the first
# place to look when the binary grows.
$(BIN): $(OBJS) $(LDSCRIPT)
	$(CC) $(CFLAGS) -T $(LDSCRIPT) $(OBJS) -o $@ $(LDLIBS) -Wl,-Map=$(MAP)

$(BUILD)/%.o: $(SRC)/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

-include $(OBJS:.o=.d)

# Workbench tool icon: without one, Workbench does not show the program.
# Copy it beside build/demo1 to start the demo from a Workbench window.
$(ICON): tools/make-icon.py | $(BUILD)
	$(DOCKER) python3 tools/make-icon.py $@

$(BUILD):
	mkdir -p $@

run: $(BIN)
	$(COPPERLINE) --factory --model $(MODEL) $(ROM) --run $(BIN)

test: $(BIN)
	set -o pipefail; $(COPPERLINE) $(RUN_FLAGS) \
	    --expect-screenshot $(SHOT) test_images/expected.png \
	    --screenshot-after $(BOUND_AT) $(BUILD)/end.png 2>&1 | tee $(BUILD)/test.log
	@grep -q "DBG: demo1: done after" $(BUILD)/test.log || { echo "demo1: FAIL (no debug output)"; exit 1; }
	@echo "demo1: PASS"

# Bootable floppy: the executable plus an S/Startup-Sequence that runs it.
# No arguments reach the program from a floppy boot, so it runs until the
# mouse button; the headless test clicks for it. Booting from floppy is
# slower than --run: the demo is on screen at about 9 s with the AROS ROM.
ADF_SHOT    = 12
ADF_CLICK   = 16
ADF_END     = 18
ADF_FLAGS   = --factory --noaudio --model $(MODEL) $(ROM) \
              --insert-disk-after 0 df0 $(ADF) --native-screenshots \
              --click-after $(ADF_CLICK) left 100

adf: $(ADF)

$(ADF): $(BIN) | $(BUILD)
	$(COPPERLINE_CTL) exe2adf $(BIN) --boot --out $@

test-adf: $(ADF)
	set -o pipefail; $(COPPERLINE) $(ADF_FLAGS) \
	    --expect-screenshot $(ADF_SHOT) test_images/expected-adf.png \
	    --screenshot-after $(ADF_END) $(BUILD)/end.png 2>&1 | tee $(BUILD)/test-adf.log
	@grep -q "DBG: demo1: done after" $(BUILD)/test-adf.log || { echo "demo1: FAIL (no debug output)"; exit 1; }
	@echo "demo1: PASS (floppy)"

bless-adf: $(ADF)
	$(COPPERLINE) $(ADF_FLAGS) \
	    --screenshot-after $(ADF_SHOT) test_images/expected-adf.png \
	    --screenshot-after $(ADF_END) $(BUILD)/end.png

bless: $(BIN)
	$(COPPERLINE) $(RUN_FLAGS) \
	    --screenshot-after $(SHOT) test_images/expected.png \
	    --screenshot-after $(BOUND_AT) $(BUILD)/end.png

SOURCES = $(wildcard $(SRC)/*.c $(SRC)/*.h)

# clang-tidy parses as host clang does in the editor: 32-bit target, the
# prototype-only host-shim/ in front of the NDK, GCC attribute macros
# defined away, integer types matched to m68k GCC (long-based int32_t,
# uintptr_t, size_t). The checks come from .clangd (readability-braces-...).
TIDY_FLAGS = --target=i386-unknown-none -std=c23 -ffreestanding \
             -Ihost-shim -Indk-include -D__stdargs= -D__regargs= -D__saveds= \
             -D__interrupt= -D__chip= -D__far= -D__near= -D__aligned= \
             -Wno-unknown-attributes -Wno-pointer-sign \
             -U__INT32_TYPE__ '-D__INT32_TYPE__=long' \
             -U__UINT32_TYPE__ '-D__UINT32_TYPE__=long unsigned int' \
             -U__INTPTR_TYPE__ '-D__INTPTR_TYPE__=long' \
             -U__UINTPTR_TYPE__ '-D__UINTPTR_TYPE__=long unsigned int' \
             -U__SIZE_TYPE__ '-D__SIZE_TYPE__=long unsigned int' \
             -U__PTRDIFF_TYPE__ '-D__PTRDIFF_TYPE__=long'

# Editor support: the NDK headers live only in the image, so copy them
# out once. host-shim/ replaces the proto/ headers for host tools. Defined
# before any rule names it: make expands prerequisites as it reads them.
NDK_DIR = ndk-include

# The tools image, rebuilt when the Dockerfile changes. The stamp file
# records the build, since make cannot see inside Docker.
TOOLS_STAMP = $(BUILD)/tools-image.stamp

tools-image: $(TOOLS_STAMP)

$(TOOLS_STAMP): Dockerfile | $(BUILD)
	docker build --platform linux/amd64 --build-arg IMAGE=$(IMAGE) \
	    -t $(TOOLS_IMAGE) .
	touch $@

# HOST_TOOLCHAIN=1: the compiler, clang-format and clang-tidy are installed
# on the host (the homelab's agent image carries the same pinned toolchain,
# extracted from $(IMAGE)), so nothing is built or run in docker, and the
# NDK headers are copied from the host's /opt/amiga instead of a scratch
# container. Defined after TOOLS_STAMP, so these assignments win.
HOST_TOOLCHAIN ?= 0
ifeq ($(HOST_TOOLCHAIN),1)
CC          = m68k-amigaos-gcc
TOOLS       =
TOOLS_STAMP =
DOCKER      =
endif

check-style: $(TOOLS_STAMP)
	$(TOOLS) tools/check-style.sh $(SOURCES)

lint: check-format tidy check-style

format: $(TOOLS_STAMP)
	$(TOOLS) clang-format --style=file -i $(SOURCES)

check-format: $(TOOLS_STAMP)
	$(TOOLS) clang-format --style=file --dry-run -Werror $(SOURCES)

tidy: $(TOOLS_STAMP) | $(NDK_DIR)
	$(TOOLS) clang-tidy --quiet --warnings-as-errors='*' \
	    --checks=-*,readability-braces-around-statements \
	    $(wildcard $(SRC)/*.c) -- $(TIDY_FLAGS)

$(NDK_DIR) ndk:
ifeq ($(HOST_TOOLCHAIN),1)
	cp -a /opt/amiga/m68k-amigaos/$(NDK_DIR) ./$(NDK_DIR)
else
	docker create --platform linux/amd64 --name demo1-ndk $(IMAGE) >/dev/null
	docker cp demo1-ndk:/opt/amiga-16.2/m68k-amigaos/$(NDK_DIR) ./$(NDK_DIR)
	docker rm demo1-ndk >/dev/null
endif

clean:
	rm -rf $(BUILD)

.PHONY: all run test bless adf test-adf bless-adf format check-format tidy check-style lint tools-image ndk clean
