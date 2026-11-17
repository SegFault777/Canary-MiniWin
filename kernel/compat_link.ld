/* SPDX-License-Identifier: GPL-3.0-only */
/* The classic-program runtime (compat_rt.c): ONE page at 0x8F0000, the address every classic program reads its table from. The table is first. */
ENTRY(compat_start)
SECTIONS {
    . = 0x8F0000;
    .mw3table : { *(.mw3table) }
    .data : { *(.data*) *(.rodata*) }
    .text : { *(.text.compat_start) *(.text*) }
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) *(.bss*) *(COMMON) }
}
