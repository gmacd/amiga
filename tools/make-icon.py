#!/usr/bin/env python3
"""Write demo1's Workbench tool icon (a .info file).

Usage: make-icon.py OUT.info [STACK]

A tool icon is a DiskObject (workbench/workbench.h) as it sits in memory,
pointers and all, followed by the data the non-null pointers stand for: one
Image header and its bitplanes (one image, so selection inverts it), and no
default tool, tool types or drawer. All values are big-endian. Workbench
needs the icon to show the program at all, and starts it with the stack
size given here.
"""

import struct
import sys

# Two bitplanes in the Workbench 2.0+ palette: grey, black, white, blue.
PENS = {".": 0, "#": 1, "o": 2, "+": 3}
ART = """
................................
.##############################.
.#++++++++++++++++++++++++++++#.
.#++++++++++++++++++++++++++++#.
.#+++++++++++++++++oooooo+++++#.
.#+++++++++++++++++oooooo+++++#.
.#+++++++++++++++++oooooo+++++#.
.#++++++++++++++++++++++++++++#.
.#++++++++++++++++++++++++++++#.
.#++++++++++++++++++++++++++++#.
.#++++++++++++++++++++++++++++#.
.#++++++++++++++++++++++++++++#.
.##############################.
..............####..............
...........##########...........
................................
""".split()

WB_DISKMAGIC = 0xE310
WB_DISKVERSION = 1
WB_DISKREVISION = 1  # the low byte of do_Gadget.UserData
WBTOOL = 3
NO_ICON_POSITION = 0x80000000
GFLG_GADGIMAGE = 0x0004  # GFLG_GADGHCOMP (0): highlight by inverting
GACT_RELVERIFY = 0x0001
GACT_IMMEDIATE = 0x0002
GTYP_BOOLGADGET = 0x0001
PRESENT = 1  # any non-null pointer: "this item follows"


def planes(art, depth):
    width, height = len(art[0]), len(art)
    words = (width + 15) // 16
    data = bytearray()
    for plane in range(depth):
        for row in art:
            assert len(row) == width, row
            bits = 0
            for x, ch in enumerate(row):
                if PENS[ch] >> plane & 1:
                    bits |= 1 << (words * 16 - 1 - x)
            data += bits.to_bytes(words * 2, "big")
    return width, height, bytes(data)


def icon(stack):
    depth = 2
    width, height, data = planes(ART, depth)
    gadget = struct.pack(
        ">IhhhhHHHIIIiIHI",
        0,  # NextGadget
        0, 0, width, height,  # LeftEdge, TopEdge, Width, Height
        GFLG_GADGIMAGE,
        GACT_RELVERIFY | GACT_IMMEDIATE,
        GTYP_BOOLGADGET,
        PRESENT,  # GadgetRender: the image below
        0,  # SelectRender: none
        0,  # GadgetText
        0,  # MutualExclude
        0,  # SpecialInfo
        0,  # GadgetID
        WB_DISKREVISION,  # UserData
    )
    disk_object = struct.pack(
        ">HH44sBxIIIIIIi",
        WB_DISKMAGIC,
        WB_DISKVERSION,
        gadget,
        WBTOOL,
        0,  # do_DefaultTool
        0,  # do_ToolTypes
        NO_ICON_POSITION,  # do_CurrentX: let Workbench place it
        NO_ICON_POSITION,  # do_CurrentY
        0,  # do_DrawerData
        0,  # do_ToolWindow
        stack,  # do_StackSize
    )
    assert len(disk_object) == 78
    image = struct.pack(
        ">hhhhhIBBI",
        0, 0, width, height, depth,
        PRESENT,  # ImageData: the planes below
        (1 << depth) - 1,  # PlanePick
        0,  # PlaneOnOff
        0,  # NextImage
    )
    assert len(image) == 20
    return disk_object + image + data


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    stack = int(sys.argv[2]) if len(sys.argv) == 3 else 4096
    with open(sys.argv[1], "wb") as out:
        out.write(icon(stack))


if __name__ == "__main__":
    main()
