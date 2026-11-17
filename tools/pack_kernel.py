#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Packs kernel.bin for the disk (1.1-alpha-14): an 8-byte header + one LZ4 block.

    python3 tools/pack_kernel.py build/kernel.bin build/kernel.packed

    offset 0   u32  packed length (the LZ4 block that follows, in bytes)
    offset 4   u32  unpacked length (the size of kernel.bin)
    offset 8        the LZ4 block (plain LZ4 block format; the last sequence is literals only)

boot/stage2.asm reads this to 0x10000 (low memory, only a staging area now), switches to protected mode and unpacks it to 0x900000, where
kernel/link.ld links the kernel. Why: below 1 MB there is room for ~560 KB of kernel including .bss; up there it only has to fit in the
512 KB the disk reserves for it *packed* (see kernel/memmap.h MW_KERNEL_BASE/END). Uses the LZ4 compressor of tools/gen_hangul_font.py."""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_hangul_font as ghf

def pack(raw, chain=24):
    packed = ghf.lz4_compress(raw, chain=chain)
    assert ghf.lz4_decompress(packed, len(raw)) == raw, "LZ4 round trip failed"
    return struct.pack("<II", len(packed), len(raw)) + packed

if __name__ == "__main__":
    raw = open(sys.argv[1], "rb").read()
    out = pack(raw)
    open(sys.argv[2], "wb").write(out)
    print("  kernel packed: %d -> %d bytes (%.0f%%)" % (len(raw), len(out), 100.0 * len(out) / len(raw)))
