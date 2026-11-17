/* SPDX-License-Identifier: GPL-3.0-only */
ENTRY(_start)

/* Every section pattern below uses the trailing "*" wildcard
 * (.text*, .data*, etc.) instead of matching the bare section name.
 * That's not decoration -- it's load-bearing. Compiling with
 * -ffunction-sections/-fdata-sections (see build.sh) puts every single
 * function and global into its own uniquely-named section (.text.foo,
 * .data.bar, ...) specifically so the linker's --gc-sections pass can
 * throw away the ones nothing calls. Match only the bare ".text" here
 * and every one of those per-function sections silently falls through
 * the cracks -- gc-sections has nothing to collect, and the whole
 * optimization quietly does nothing while looking like it's enabled.
 * This is exactly the kind of "builds fine, works fine, just doesn't
 * do the one thing you added it for" mistake that's invisible until
 * someone diffs the binary size before and after. */
SECTIONS
{
    /* 1.1-alpha-14: the kernel no longer runs from low memory (where code + .bss had to squeeze under 0x9EC00, ~560 KB in all). boot/stage2.asm
     * unpacks the LZ4-packed kernel from the disk to this address; kernel/memmap.h calls it MW_KERNEL_BASE (keep the two in step) and gives it
     * 1 MB (up to MW_KERNEL_END, where the memory manager's heap begins). */
    . = 0x900000;
    _kernel_start = .;

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    /* paging.h makes everything below this address (the kernel's code and constants) READ-ONLY, so a stray write into them is a page
     * fault instead of silent damage. Page granularity: .data must start on a fresh page, hence the alignment. */
    . = ALIGN(4096);
    _ro_end = .;

    .data : {
        *(.data*)
    }

    .bss : {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        _bss_end = .;
    }

    /* Debug/metadata sections nobody at runtime ever reads -- a
     * freestanding kernel with no debugger attached has no use for
     * unwind tables or comment strings, so drop them instead of
     * shipping them inside the boot image. */
    /DISCARD/ : {
        *(.eh_frame*)
        *(.comment*)
        *(.note*)
    }
}
