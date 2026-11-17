#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Superseded by tools/gen_trusted_roots.py.

This script appended full DER certificates to the old trusted_roots.h. The table is now a compact anchor list (subject Name + public
key per root) and tools/gen_trusted_roots.py regenerates all of it, ECDSA roots included. Run that one instead."""
import sys
sys.exit("add_ecc_roots.py is obsolete: run tools/gen_trusted_roots.py")
