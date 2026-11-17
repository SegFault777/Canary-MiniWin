#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Writes a font pack (tools/gen_font_pack.py) into a disk image at kernel/fs.h's MW_FONT_LBA, zero-filling the rest of the region.

    tools/install_fontpack.py IMAGE PACK        # PACK: third_party/galmuri-font/fontpack-min.bin (Galmuri11 only) or fontpack-full.bin

build.sh installs the minimal pack into every image it makes; tools/install_all.sh replaces it with the full one in the image that
becomes the ISO (the floppy copy is taken before that and keeps the minimal one).
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
fs_h = open(os.path.join(HERE, "..", "kernel", "fs.h")).read()
LBA = int(re.search(r"#define\s+MW_FONT_LBA\s+(\d+)u?", fs_h).group(1))
SECTORS = int(re.search(r"#define\s+MW_FONT_SECTORS\s+(\d+)u?", fs_h).group(1))

def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    img, pack = sys.argv[1], open(sys.argv[2], "rb").read()
    if pack[:4] != b"MWFP" or len(pack) % 512:
        sys.exit("ERROR: %s is not a font pack" % sys.argv[2])
    if len(pack) > SECTORS * 512:
        sys.exit("ERROR: the pack is %d sectors, the region MW_FONT_SECTORS is %d" % (len(pack) // 512, SECTORS))
    with open(img, "r+b") as f:
        f.seek(LBA * 512)
        f.write(pack + bytes(SECTORS * 512 - len(pack)))
    print("install_fontpack: %s (%d sectors) -> %s at LBA %d (region %d sectors)" % (os.path.basename(sys.argv[2]), len(pack) // 512, img, LBA, SECTORS))

main()
