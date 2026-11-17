#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Builds the FONT PACK (third_party/galmuri-font/fontpack.bin): the extra faces of the full image -- Galmuri7, Galmuri9, Galmuri14 and
Galmuri11 Bold -- as glyph tables the kernel can use at once (1.1-alpha-14, see docs/dev and kernel/fontpack.h).

Galmuri11 (regular) is face 0 and the DEFAULT: the kernel no longer carries its Hangul table (that was 62,755 bytes of a kernel that
had run out of room); fontpack_boot() (kernel/fontpack.h) loads face 0 from the pack at boot. There are two packs: fontpack-min.bin
(Galmuri11 only: what the 1.44 MB floppy image and a plain build have) and fontpack-full.bin (the five faces of the ISO). The pack lives
in the system area of the image (kernel/fs.h MW_FONT_LBA .. +MW_FONT_SECTORS), so an installed system carries it too.

A face is `cell` rows of u16 per glyph (MSB-aligned, bit 15 = the leftmost pixel; a 7-pixel face just uses fewer bits), the same layout as
the built-in tables, in this order: the 11172 Hangul syllables (so face 0's table sits where font8x8_ko always pointed), the 51 jamo
U+3131..U+3163, then LATIN_COUNT glyphs (U+0020..U+007E). Each face is
one LZ4 block (kernel/lz.h decodes it). The pack:

    sector 0   header: "MWFP", u32 version (1), u32 count, then `count` entries of 40 bytes:
                 char name[16]; u8 cell; u8 bold; u16 0; u32 start_sector (from the pack's first sector); u32 sectors;
                 u32 raw_bytes; u32 packed_bytes; u32 crc32 of the raw bytes
    sector 1.. each face's packed bytes, padded to whole sectors

The glyph placement is tools/bdf_common.py's place_glyph(): baseline on the cell's last row (cap letters and every Hangul syllable flush
with the cell). The tails of g j p q y and the comma that go below the cell are KEPT (1.1-alpha-14): a face's Latin glyphs are `cell + desc`
rows each, the entry's former padding field holds desc (0 = none, as before), and kernel/font.h draws the extra rows under the cell.

    tools/gen_font_pack.py --min                  # fontpack-min.bin: Galmuri11 only, from third_party/galmuri-font/Galmuri11-subset.bdf
    tools/gen_font_pack.py --bdf-dir DIR          # DIR holds Galmuri7.bdf, Galmuri9.bdf, Galmuri14.bdf, Galmuri11-Bold.bdf (from the
                                                  # Galmuri release zip, SIL OFL 1.1); writes fontpack-full.bin (and the min pack too)
    tools/gen_font_pack.py --check                # (no BDFs needed) validates both checked-in packs: header, CRCs, round trip,
                                                  # and that face 0 hashes to KO_FONT_CHECKSUM (kernel/font_ko_data.h)
"""
import argparse
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from bdf_common import parse_bdf, place_glyph, place_glyph_rows, descender_rows
import importlib.util
_spec = importlib.util.spec_from_file_location("gen_hangul_font", os.path.join(HERE, "gen_hangul_font.py"))
_ghf = importlib.util.module_from_spec(_spec); _spec.loader.exec_module(_ghf)

GALMURI_DIR = os.path.join(HERE, "..", "third_party", "galmuri-font")
PACK_MIN  = os.path.join(GALMURI_DIR, "fontpack-min.bin")
PACK_FULL = os.path.join(GALMURI_DIR, "fontpack-full.bin")
FACES = [   # (name in the pack, BDF file or None = the subset in third_party/galmuri-font, cell, bold)
    ("GALMURI11",  None,                11, 0),
    ("GALMURI7",   "Galmuri7.bdf",       7, 0),
    ("GALMURI9",   "Galmuri9.bdf",       9, 0),
    ("GALMURI14",  "Galmuri14.bdf",     14, 0),
    ("GALMURI11B", "Galmuri11-Bold.bdf", 11, 1),
]
LATIN_FIRST, LATIN_COUNT = 0x20, 95
SYL_BASE, SYL_COUNT = 0xAC00, 11172
JAMO_BASE, JAMO_COUNT = 0x3131, 51
SECTOR = 512
HDR_ENTRY = 40
KO_CHECKSUM = int(__import__("re").search(r"KO_FONT_CHECKSUM\s+0x([0-9A-Fa-f]+)u", open(os.path.join(HERE, "..", "kernel", "font_ko_data.h")).read()).group(1), 16)


def face_rows(glyphs, cell, path):
    rows, clipped = [], 0
    desc = descender_rows(glyphs, cell, LATIN_FIRST, LATIN_FIRST + LATIN_COUNT - 1)
    for base, count in ((SYL_BASE, SYL_COUNT), (JAMO_BASE, JAMO_COUNT), (LATIN_FIRST, LATIN_COUNT)):
        for i in range(count):
            cp = base + i
            entry = glyphs.get(cp)
            if entry is None:
                if base == LATIN_FIRST:                  # a Latin glyph a face does not have: blank (a space is blank anyway)
                    rows.extend([0] * (cell + desc)); continue
                sys.exit("ERROR: U+%04X missing from %s" % (cp, path))
            if base == LATIN_FIRST:
                canvas, ct, cb = place_glyph_rows(entry, cell, cell + desc, cell - 1)      # the cell and the tails under it
            else:
                canvas, ct, cb = place_glyph(entry, cell=cell, baseline_row=cell - 1)
            clipped += 1 if (ct or cb) else 0
            rows.extend(r << (16 - cell) for r in canvas)
    return struct.pack("<%dH" % len(rows), *rows), clipped, desc


def build(bdf_dir, minimal):
    faces = []
    for name, fn, cell, bold in FACES:
        if minimal and name != "GALMURI11":
            continue
        path = _ghf.BDF_PATH if fn is None else os.path.join(bdf_dir, fn)
        raw, clipped, desc = face_rows(parse_bdf(path), cell, path)
        if name == "GALMURI11":                                  # must be EXACTLY the table the kernel used to carry
            h = _ghf.checksum(raw[:SYL_COUNT * cell * 2])
            assert h == KO_CHECKSUM, "Galmuri11 table differs from font_ko_data.h (0x%08X != 0x%08X)" % (h, KO_CHECKSUM)
        packed = _ghf.lz4_compress(raw)
        assert _ghf.lz4_decompress(packed, len(raw)) == raw
        faces.append((name, cell, bold, raw, packed, clipped, desc))
        print("  %-11s cell %2d  desc %d  raw %7d  packed %6d  (%d glyphs clipped)" % (name, cell, desc, len(raw), len(packed), clipped))
    hdr = bytearray(SECTOR)
    hdr[0:4] = b"MWFP"; struct.pack_into("<II", hdr, 4, 1, len(faces))
    body = bytearray(); sector = 1
    for i, (name, cell, bold, raw, packed, clipped, desc) in enumerate(faces):
        n = (len(packed) + SECTOR - 1) // SECTOR
        struct.pack_into("<16sBBHIIIII", hdr, 12 + i * HDR_ENTRY, name.encode(), cell, bold, desc, sector, n, len(raw), len(packed), zlib.crc32(raw) & 0xFFFFFFFF)
        body += packed + bytes(n * SECTOR - len(packed)); sector += n
    return bytes(hdr) + bytes(body)


def check(path):
    d = open(path, "rb").read()
    assert d[:4] == b"MWFP" and len(d) % SECTOR == 0, "bad header"
    ver, count = struct.unpack_from("<II", d, 4); assert ver == 1
    for i in range(count):
        name, cell, bold, desc, start, n, raw_n, packed_n, crc = struct.unpack_from("<16sBBHIIIII", d, 12 + i * HDR_ENTRY)
        packed = d[start * SECTOR: start * SECTOR + packed_n]
        raw = _ghf.lz4_decompress(packed, raw_n)
        assert zlib.crc32(raw) & 0xFFFFFFFF == crc, name
        assert desc <= 4 and raw_n == ((SYL_COUNT + JAMO_COUNT) * cell + LATIN_COUNT * (cell + desc)) * 2, name
        if i == 0:
            assert name.rstrip(b"\0") == b"GALMURI11" and cell == 11 and not bold, "face 0 must be Galmuri11"
            assert _ghf.checksum(raw[:SYL_COUNT * 22]) == KO_CHECKSUM, "face 0 does not hash to KO_FONT_CHECKSUM"
        print("  ok %-11s cell %2d desc %d raw %d packed %d sectors %d" % (name.rstrip(b"\0").decode(), cell, desc, raw_n, packed_n, n))
    print("pack: %d bytes = %d sectors" % (len(d), len(d) // SECTOR))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bdf-dir"); ap.add_argument("--check", action="store_true"); ap.add_argument("--min", action="store_true")
    a = ap.parse_args()
    if a.check:
        for p in (PACK_MIN, PACK_FULL):
            print(os.path.basename(p)); check(p)
        return
    if not a.bdf_dir and not a.min:
        sys.exit("need --min or --bdf-dir (or --check)")
    data = build(a.bdf_dir, True)
    open(PACK_MIN, "wb").write(data); print("wrote %s: %d bytes = %d sectors" % (PACK_MIN, len(data), len(data) // SECTOR)); check(PACK_MIN)
    if a.bdf_dir:
        data = build(a.bdf_dir, False)
        open(PACK_FULL, "wb").write(data); print("wrote %s: %d bytes = %d sectors" % (PACK_FULL, len(data), len(data) // SECTOR)); check(PACK_FULL)


if __name__ == "__main__":
    main()
