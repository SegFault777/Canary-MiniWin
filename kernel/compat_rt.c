/* SPDX-License-Identifier: GPL-3.0-only */
/* compat_rt.c -- the "classic program" runtime of ring 3 (1.1-alpha-14).
 *
 * A program built before ring 3 (ABI 1 / 2, and any built without --ring3) is written to call the kernel's functions through a table of function
 * pointers at a FIXED address, 0x8F0000 (sdk/include/mwp_abi.h). Running it in ring 3 takes nothing from the program: the kernel gives it, at that same
 * address, a page of its own that holds the table -- but the table points to the stubs below, which make the call with `int 0x80` (sdk/include/mw_abi3.h
 * is the very same code a native ring-3 program carries in its own memory). This file is compiled at build time into a flat blob (tools/gen_compat_rt.py
 * turns it into kernel/compat_rt_blob.h); the kernel copies it into every classic program's address space before it starts, writes the program's real
 * entry point into compat_entry_ptr, and the program starts at compat_start(): set the table up, run the program, exit. The program cannot tell the
 * difference, except that mem_alloc rounds up to pages, dir_list shows the first 8 KB of a listing, and the things it should never have done fault. */
#define MW3_NO_CRT 1
#define MW3_TABLE_SECTION __attribute__((section(".mw3table")))
#include "mwp_abi.h"
#include "mw_abi3.h"

static void (*volatile compat_entry_ptr)(void) = (void (*)(void))1;   /* patched by the kernel: the classic program's own mwp_entry */

__attribute__((section(".text.compat_start"), no_stack_protector)) void compat_start(void) {
    mw3_init();
    compat_entry_ptr();
    mw3_sc(MW_SYS_EXIT, 0, 0, 0, 0, 0);
    for (;;) { }
}
