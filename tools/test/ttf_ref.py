#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Writes the reference faces for host_ttf_test.c: every face of the checked-in font pack (third_party/galmuri-font/fontpack-full.bin),
inflated, as  /tmp/ttf_ref_<NAME>.raw  =  u8 cell, u8 desc, then the raw u16 rows (the layout kernel/fontpack.h describes).
usage: ttf_ref.py [outdir]"""
import os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import importlib.util
spec = importlib.util.spec_from_file_location("gen_hangul_font", os.path.join(HERE, "..", "gen_hangul_font.py"))
ghf = importlib.util.module_from_spec(spec); spec.loader.exec_module(ghf)
out = sys.argv[1] if len(sys.argv) > 1 else "/tmp"
d = open(os.path.join(HERE, "..", "..", "third_party", "galmuri-font", "fontpack-full.bin"), "rb").read()
ver, count = struct.unpack_from("<II", d, 4)
for i in range(count):
    name, cell, bold, desc, start, n, raw_n, packed_n, crc = struct.unpack_from("<16sBBHIIIII", d, 12 + i * 40)
    raw = ghf.lz4_decompress(d[start * 512: start * 512 + packed_n], raw_n)
    nm = name.rstrip(b"\0").decode()
    open(os.path.join(out, "ttf_ref_%s.raw" % nm), "wb").write(bytes([cell, desc]) + raw)
    print("ref", nm, cell, desc, len(raw))
