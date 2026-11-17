/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_PAGING_H
#define MW_PAGING_H

/* paging.h -- x86 paging for the kernel (1.1-alpha-11).
 *
 * WHAT IT DOES TODAY. The CPU is switched to paged addressing with an IDENTITY map (virtual address = physical address), so no kernel
 * pointer, DMA buffer or device register changes -- but the pages are no longer all alike:
 *   - page 0 is NOT PRESENT: a null-pointer read or write (in the kernel or in a program) is a page fault, not a silent access to the
 *     BIOS data at 0;
 *   - the kernel's own code and constants (`_kernel_start` = MW_KERNEL_BASE up to `_ro_end`, kernel/link.ld) are READ-ONLY (CR0.WP makes ring 0 obey it too);
 *   - guard pages -- present in the address space but not mapped -- sit under the kernel stack and under the stack of every program that
 *     runs in a window, so a runaway recursion faults at the edge of the stack instead of eating whatever lies below it;
 *   - those faults are survivable: a program in a window that faults is stopped, its window closed, and the desktop carries on
 *     (apps/progwin.h's hook into cpu_fault, fatal.h). A stack overflow needs a stack to report itself on, which is exactly what has
 *     run out -- so a double fault (vector 8) is delivered through a TASK GATE to a TSS with a stack of its own.
 *
 * HOW THE MAP IS BUILT. RAM up to 1 GB is mapped with 4 KB pages (so single pages can be guarded or made read-only); everything else up
 * to 4 GB -- RAM beyond 1 GB, the video frame buffer, device registers (PCI BARs) -- with 4 MB pages (PSE), present and writable, with
 * the cache attributes left to the MTRRs as before paging. The page directory and tables come from mm.h (about 128 KB for 128 MB of RAM).
 *
 * WHAT IT DOES NOT DO, plainly: a program built the classic way still runs in ring 0, so it can write anywhere that is writable -- the
 * kernel's data, other windows' memory, the page tables themselves. A program built with --ring3 (1.1-alpha-14, kernel/ring3.h) is the
 * answer: every page here is supervisor-only (US = 0) EXCEPT the ones ring3.h hands to a program in ITS OWN directory (as_t below) -- its slot,
 * stack and heap blocks -- and it asks the kernel for things through `int 0x80` (the GDT's user segments 0x2B/0x33 and the TSS's esp0, built below).
 *
 * Needs: mm.h (page allocation), fatal.h (the IDT: gate 8 is replaced by the task gate), memmap.h (the stack). */

#define PG_P   1u
#define PG_RW  2u
#define PG_US  4u
#define PG_PS  0x80u
#define PG_4K_LIMIT 0x40000000u          /* RAM below 1 GB gets 4 KB pages */

extern u8 _kernel_start[];                /* kernel/link.ld: where the kernel (unpacked by the boot loader) starts */
extern u8 _ro_end[];                     /* kernel/link.ld: end of the kernel's code + constants, page aligned */

static u32 *pg_dir = 0;
static u32 *pg_cur_dir = 0;                /* the directory CR3 holds right now (the kernel's, or a ring-3 program's: see as_enter below) */
static int  pg_enabled = 0;
static u32  pg_tables = 0, pg_guards = 0, pg_ro_pages = 0, pg_ram_top = 0;

static inline void pg_invlpg(u32 a) { __asm__ volatile ("invlpg (%0)" : : "r"(a) : "memory"); }
static inline u32 *pg_pte(u32 a) {
    if (!pg_dir) return 0;
    u32 pde = pg_dir[a >> 22];
    if (!(pde & PG_P) || (pde & PG_PS)) return 0;                  /* a 4 MB page has no table to edit */
    return (u32 *)(pde & ~0xFFFu) + ((a >> 12) & 1023u);
}

/* Makes the 4 KB page at `addr` inaccessible (any touch faults). 1 = done; 0 = not possible (paging off, or the address is in a 4 MB page). */
static int paging_guard(void *addr) {
    u32 a = (u32)(unsigned long)addr & ~0xFFFu;
    u32 *e = pg_enabled ? pg_pte(a) : 0;
    if (!e) return 0;
    if (*e & PG_P) { *e &= ~PG_P; pg_guards++; pg_invlpg(a); }
    return 1;
}
/* Gives it back (call this BEFORE freeing the memory: mm_page_alloc() clears pages it hands out, which would fault on a guard). */
static void paging_unguard(void *addr) {
    u32 a = (u32)(unsigned long)addr & ~0xFFFu;
    u32 *e = pg_enabled ? pg_pte(a) : 0;
    if (e && !(*e & PG_P)) { *e = a | PG_P | PG_RW; if (pg_guards) pg_guards--; pg_invlpg(a); }
}

/* ---------------------------------------------------------------- the double-fault task */
typedef struct {
    u32 link, esp0, ss0, esp1, ss1, esp2, ss2, cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi, es, cs, ss, ds, fs, gs, ldt;
    u16 trap, iomap;
} __attribute__((packed)) pg_tss_t;
static pg_tss_t pg_tss_main, pg_tss_df;
static u32 pg_gdt5[7 * 2];                                         /* null, code, data (copied from stage 2), main TSS (0x18), double-fault TSS (0x20), user code (0x2B), user data (0x33) */
#define PG_SEL_UCODE 0x2Bu                                         /* ring 3 (RPL 3) flat code, 4 GB */
#define PG_SEL_UDATA 0x33u                                         /* ring 3 flat data/stack */
static u8  pg_df_stack[8192] __attribute__((aligned(16)));
#define PG_SEL_TSS_MAIN 0x18u
#define PG_SEL_TSS_DF   0x20u

static void pg_gdt_tss(int index, const void *tss) {
    u32 base = (u32)(unsigned long)tss, limit = (u32)sizeof(pg_tss_t) - 1;
    pg_gdt5[index * 2]     = (limit & 0xFFFFu) | ((base & 0xFFFFu) << 16);
    pg_gdt5[index * 2 + 1] = ((base >> 16) & 0xFFu) | (0x89u << 8) | (limit & 0xF0000u) | (base & 0xFF000000u);   /* present, ring 0, 32-bit TSS (available) */
}

static void pg_df_entered(void) __attribute__((noreturn));
static void pg_df_entered(void) {
    u32 fl;                                                         /* we got here by a task switch: the NT flag is set -- clear it */
    __asm__ volatile ("pushfl; popl %0" : "=r"(fl));
    fl &= ~0x4000u;
    __asm__ volatile ("pushl %0; popfl" : : "r"(fl) : "cc");
    pg_gdt_tss(3, &pg_tss_main); pg_gdt_tss(4, &pg_tss_df);          /* both TSS descriptors available again ... */
    __asm__ volatile ("ltr %%ax" : : "a"((u16)PG_SEL_TSS_MAIN));     /* ... and TR back on the main one, so the next double fault can be taken */
    u32 f[13];                                                      /* what the faulting code was doing, in the layout cpu_fault() reads */
    f[0] = pg_tss_main.edi; f[1] = pg_tss_main.esi; f[2] = pg_tss_main.ebp; f[3] = pg_tss_main.esp; f[4] = pg_tss_main.ebx;
    f[5] = pg_tss_main.edx; f[6] = pg_tss_main.ecx; f[7] = pg_tss_main.eax; f[8] = 8; f[9] = 0;
    f[10] = pg_tss_main.eip; f[11] = pg_tss_main.cs; f[12] = pg_tss_main.eflags;
    cpu_fault(f);                                                   /* a program in a window is stopped there; otherwise the error screen */
    for (;;) { }
}

/* ---------------------------------------------------------------- switching paging on */
static void pg_build_gdt(void) {
    struct { u16 limit; u32 base; } __attribute__((packed)) old;
    __asm__ volatile ("sgdt %0" : "=m"(old));
    const u32 *src = (const u32 *)(unsigned long)old.base;
    for (int i = 0; i < 6; i++) pg_gdt5[i] = src[i];                 /* null, code, data: the flat segments stage 2 made */
    pg_tss_main.ss0 = 0x10; pg_tss_main.esp0 = MW_STACK_TOP; pg_tss_main.iomap = (u16)sizeof(pg_tss_t);
    pg_tss_df.eip = (u32)(unsigned long)pg_df_entered; pg_tss_df.eflags = 2;
    pg_tss_df.esp = (u32)(unsigned long)pg_df_stack + sizeof pg_df_stack - 16; pg_tss_df.ss0 = 0x10;
    pg_tss_df.cs = 0x08; pg_tss_df.ss = pg_tss_df.ds = pg_tss_df.es = pg_tss_df.fs = pg_tss_df.gs = 0x10;
    pg_tss_df.cr3 = (u32)(unsigned long)pg_dir; pg_tss_main.cr3 = (u32)(unsigned long)pg_dir; pg_tss_df.iomap = (u16)sizeof(pg_tss_t);
    pg_gdt_tss(3, &pg_tss_main); pg_gdt_tss(4, &pg_tss_df);
    pg_gdt5[10] = 0x0000FFFFu; pg_gdt5[11] = 0x00CFFA00u;             /* 5: user code: base 0, limit 4 GB, present, DPL 3, execute/read, 32-bit */
    pg_gdt5[12] = 0x0000FFFFu; pg_gdt5[13] = 0x00CFF200u;             /* 6: user data: the same, read/write */
    struct { u16 limit; u32 base; } __attribute__((packed)) d = { (u16)(sizeof pg_gdt5 - 1), (u32)(unsigned long)pg_gdt5 };
    __asm__ volatile ("lgdt %0" : : "m"(d));                         /* same flat code/data, two TSS descriptors more: no segment register needs reloading */
    __asm__ volatile ("ltr %%ax" : : "a"((u16)PG_SEL_TSS_MAIN));
    idt_gates[8].lo = (u32)PG_SEL_TSS_DF << 16;                      /* vector 8 = a task gate to the DF TSS: its own stack */
    idt_gates[8].hi = 0x8500u;                                       /* present, ring 0, type 5 (task gate) */
}

/* Builds the tables and switches paging on. Returns 1 if it is on, 0 if it was left off (no PSE, not enough memory): the kernel then runs
 * exactly as before. Call after mm_init_boot() and idt_init(). */
static int paging_init(void) {
    u32 a, b, c, d;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    if (!(d & 8u)) return 0;                                         /* no 4 MB pages: not worth building the map two ways */
    u32 kb = *(volatile u32 *)0x9200;
    u32 ram_top = kb >= 0x400000u ? 0xFFFFF000u : kb * 1024u;
    pg_ram_top = ram_top;
    u32 ram4k = ram_top < PG_4K_LIMIT ? ram_top : PG_4K_LIMIT;
    u32 chunks = (ram4k + 0x3FFFFFu) >> 22;                          /* 4 MB chunks with a table of their own (the last one is mapped whole) */
    u32 *dir = (u32 *)mm_page_alloc();
    if (!dir) return 0;
    u32 *tabs[256];
    if (chunks > 256) chunks = 256;
    for (u32 i = 0; i < chunks; i++) {
        tabs[i] = (u32 *)mm_page_alloc();
        if (!tabs[i]) { for (u32 k = 0; k < i; k++) mm_pages_free(tabs[k], 1); mm_pages_free(dir, 1); return 0; }
    }
    u32 ro_end = (u32)(unsigned long)_ro_end, guard_stack = MW_STACK_TOP - MW_STACK_BYTES;
    for (u32 i = 0; i < chunks; i++) {
        for (u32 j = 0; j < 1024; j++) {
            u32 addr = (i * 1024u + j) * 4096u, fl = PG_P | PG_RW;
            if (addr == 0) { tabs[i][j] = 0; continue; }             /* the null page */
            if (addr == guard_stack) { tabs[i][j] = 0; pg_guards++; continue; }      /* under the kernel stack */
            if (addr >= (u32)(unsigned long)_kernel_start && addr < ro_end) { fl = PG_P; pg_ro_pages++; }      /* kernel code + constants: read-only */
            tabs[i][j] = addr | fl;
        }
        dir[i] = (u32)(unsigned long)tabs[i] | PG_P | PG_RW | PG_US;      /* user/supervisor is decided per page: every PTE below has US = 0 unless ring3.h hands it to a program */
    }
    for (u32 i = chunks; i < 1024; i++) dir[i] = (i << 22) | PG_P | PG_RW | PG_PS;   /* the rest of the 4 GB: 4 MB pages */
    pg_dir = dir; pg_cur_dir = dir; pg_tables = chunks;
    pg_build_gdt();
    __asm__ volatile (
        "movl %%cr4, %%eax\n orl $0x10, %%eax\n movl %%eax, %%cr4\n"           /* PSE */
        "movl %0, %%cr3\n"
        "movl %%cr0, %%eax\n orl $0x80010000, %%eax\n movl %%eax, %%cr0\n"      /* PG | WP */
        "jmp 1f\n1:\n" : : "r"((u32)(unsigned long)dir) : "eax", "memory");
    pg_enabled = 1;
    return 1;
}


/* ---------------------------------------------------------------- an address space per program (1.1-alpha-14) */
/* Every ring-3 program gets a PAGE DIRECTORY of its own. It starts as a copy of the kernel's (pg_dir): the same identity map of the whole machine,
 * every page supervisor-only, so the kernel runs unchanged under any of them. What makes a program's directory different is a handful of PRIVATE page
 * tables (a copy of the shared table, taken the first time the program gets a page in that 4 MB region) in which the program's own pages are marked
 * user -- and, for the loader's slot (0x8F0000 .. 0x900000, the address every .MWP is linked for), mapped to pages that are THE PROGRAM'S ALONE: the
 * virtual address is the same in every program, the physical pages are not. That is what lets several programs run at once, and what makes another
 * program's memory (and the kernel's) simply not present, as far as a program is concerned.
 * The CPU's CR3 is switched to the program's directory while the program runs (and while the kernel serves its system calls) and back to the
 * kernel's whenever the desktop has the CPU (as_enter / as_leave, called by apps/progwin.h). pg_cur_dir says which one is live: the pointer checks
 * below (pg_user_ok) walk THAT one. */
#define PG_AS_PT_MAX 24                    /* private tables per program (each covers 4 MB of the address space) */
typedef struct {
    u32 *dir;
    struct { u32 idx; u32 *pt; } priv[PG_AS_PT_MAX];
    int npriv;
} as_t;

static inline u32 *pg_pte_in(u32 *dir, u32 a) {
    if (!dir) return 0;
    u32 pde = dir[a >> 22];
    if (!(pde & PG_P) || (pde & PG_PS)) return 0;
    return (u32 *)(pde & ~0xFFFu) + ((a >> 12) & 1023u);
}
static int as_create(as_t *as) {
    as->npriv = 0; as->dir = 0;
    if (!pg_enabled) return 0;
    u32 *d = (u32 *)mm_page_alloc();
    if (!d) return 0;
    for (u32 i = 0; i < 1024; i++) d[i] = pg_dir[i];
    as->dir = d;
    return 1;
}
/* The program's private table for the 4 MB region of `addr` (made on first use). 0 = not possible (a 4 MB page, no memory, too many regions). */
static u32 *as_pt(as_t *as, u32 addr) {
    if (!as->dir) return 0;
    u32 idx = addr >> 22, pde = as->dir[idx];
    if (!(pde & PG_P) || (pde & PG_PS)) return 0;
    for (int k = 0; k < as->npriv; k++) if (as->priv[k].idx == idx) return as->priv[k].pt;
    if (as->npriv >= PG_AS_PT_MAX) return 0;
    u32 *pt = (u32 *)mm_page_alloc();
    if (!pt) return 0;
    const u32 *src = (const u32 *)(pde & ~0xFFFu);
    for (u32 i = 0; i < 1024; i++) pt[i] = src[i];
    as->dir[idx] = (u32)(unsigned long)pt | PG_P | PG_RW | PG_US;
    as->priv[as->npriv].idx = idx; as->priv[as->npriv].pt = pt; as->npriv++;
    return pt;
}
/* Maps the physical page `phys` at virtual `virt` in the program's space with `flags` (PG_P | PG_RW | PG_US ...). */
static int as_map_page(as_t *as, u32 virt, u32 phys, u32 flags) {
    u32 *pt = as_pt(as, virt);
    if (!pt) return 0;
    pt[(virt >> 12) & 1023u] = (phys & ~0xFFFu) | flags;
    return 1;
}
/* Gives (on = 1) or takes back (on = 0) the user bit of the identity-mapped pages of [addr, addr + bytes) in the program's space (the kernel's own table is not touched). */
static int as_user_range(as_t *as, u32 addr, u32 bytes, int on) {
    for (u32 a = addr & ~0xFFFu; a < addr + bytes; a += 4096u) {
        u32 *pt = as_pt(as, a);
        if (!pt) return 0;
        u32 *e = &pt[(a >> 12) & 1023u];
        if (!(*e & PG_P)) continue;
        if (on) *e |= PG_US; else *e &= ~PG_US;
    }
    return 1;
}
static void as_unmap(as_t *as, u32 addr) {                       /* the page at addr is not there for this program at all (a guard page) */
    u32 *pt = as_pt(as, addr);
    if (pt) pt[(addr >> 12) & 1023u] &= ~PG_P;
}
static void as_destroy(as_t *as) {
    for (int k = 0; k < as->npriv; k++) mm_pages_free(as->priv[k].pt, 1);
    as->npriv = 0;
    if (as->dir) mm_pages_free(as->dir, 1);
    as->dir = 0;
}
static inline void as_enter(as_t *as) {
    if (!as->dir) return;
    pg_cur_dir = as->dir;
    __asm__ volatile ("movl %0, %%cr3" : : "r"((u32)(unsigned long)as->dir) : "memory");
}
static inline void as_leave(void) {
    if (!pg_enabled) return;
    pg_cur_dir = pg_dir;
    __asm__ volatile ("movl %0, %%cr3" : : "r"((u32)(unsigned long)pg_dir) : "memory");
}
/* Is every byte of [addr, addr + len) in present user pages of the live address space (writable ones if `write`)? len 0 is always fine. Ring 0 could
 * read anything, so this is the check that stops a program from handing the kernel a pointer into the kernel. */
static int pg_user_ok(u32 addr, u32 len, int write) {
    if (!len) return 1;
    if (!pg_enabled || addr + len < addr) return 0;
    u32 *dir = pg_cur_dir ? pg_cur_dir : pg_dir;
    for (u32 a = addr & ~0xFFFu; a < addr + len; a += 4096u) {
        u32 *e = pg_pte_in(dir, a);
        if (!e || (*e & (PG_P | PG_US)) != (PG_P | PG_US) || (write && !(*e & PG_RW))) return 0;
        if (a + 4096u < a) break;
    }
    return 1;
}

#endif
