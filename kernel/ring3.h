/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_RING3_H
#define MW_RING3_H

/* ring3.h -- programs that run in RING 3 (1.1-alpha-14).
 *
 * A program built with `tools/build_mwp.sh --ring3` (MWP_FLAG_RING3 in its trailer, "ABI 3": sdk/include/mwp_abi.h) does not call the kernel's
 * functions: it runs with the CPU in privilege level 3, in pages the kernel marked as the program's (paging.h: the user bit), and asks for
 * everything with `int 0x80` -- eax = a MW_SYS_* number, ebx ecx edx esi edi = the arguments, the answer comes back in eax. What that buys:
 *   - it cannot read or write a kernel page, another window's memory, the page tables or a device: that is a PAGE FAULT, and (as for any program
 *     in a window: apps/progwin.h) only the program is stopped, with a sentence on the status line;
 *   - it cannot execute cli/sti/in/out/lgdt/mov cr3 ...: that is a GENERAL PROTECTION FAULT;
 *   - every pointer it hands the kernel is checked against the pages it owns BEFORE the kernel touches it (pg_user_ok(), r3_str()).
 *
 * HOW IT FITS THE WINDOW. The program is still a coroutine on apps/progwin.h's stack, resumed once per desktop frame: pw_trampoline() calls
 * r3_run(), which `iret`s into ring 3 and never comes back through that frame. From then on the program lives in ring 3 and the same stack
 * (pw.stack) is its KERNEL stack: the TSS (paging.h) names the top of it as esp0, so `int 0x80` and every exception land there. A
 * syscall that waits (present, key_poll, sleep_ms, menu_popup) calls pw_yield() -- the coroutine switch -- from inside the handler exactly as
 * a ring-0 program does, and the handler resumes later and `iret`s back to the program. EXIT, or a fault, switches to the desktop's stack for
 * good, as before. Interrupts stay OFF (IF = 0, and a ring-3 program cannot turn them on: the kernel has no interrupt-driven code to protect).
 *
 * WHAT THE PROGRAM OWNS -- in an ADDRESS SPACE OF ITS OWN (paging.h as_t): (1) the loader's slot, 0x8F1000 .. 0x900000, as pages that are its alone
 * (the file is copied there; the rest is zero, a place for globals) -- the same virtual addresses in every program, different physical pages, which is
 * why several programs can run at once; (2) a 64 KB stack with an unmapped guard page under it; (3) heap blocks from MW_SYS_MEM_ALLOC: whole pages
 * plus a guard page, the object right-aligned against it (r3_alloc), at most 64 blocks and 4 MB. Everything else -- the kernel, the other programs --
 * is simply not mapped for it. When the program ends its pages go back and its directory is destroyed.
 * The page at 0x8F0000 is the kernel's table for ring-0 programs; a native ring-3 program does not see it (its own table of `int 0x80` stubs lives in its
 * data, sdk/include/mw_abi3.h) -- and a program built the classic way gets, at that address, a user-side copy made of stubs (compat_rt, below).
 *
 * LIMITS, plainly: a program's own bugs INSIDE its own memory are its own business -- nothing can stop a program that overwrites its own variables
 * (the guard pages, the stack protector of --ring3 builds and the checked free() catch the common memory errors, not all of them); IA-32 has no
 * no-execute bit without PAE, so its data can be executed by itself, harmlessly to everybody else. Needs: paging.h (as_t), apps/progwin.h (pw, pw_yield),
 * mwp_api2.h (the kernel's implementation of each call), ui/ctxmenu_actions.h (menu_popup). */

#include "compat_rt_blob.h"            /* GENERATED (tools/gen_compat_rt.py, run by build.sh): the classic-program runtime, see compat_rt.c */

#define R3_USTACK_BYTES   (64u * 1024u)
#define R3_KSTACK_RESERVE 4096u            /* the top of pw.stack: the frames of pw_trampoline()/r3_enter(), abandoned by the iret; esp0 sits below them */
#define R3_HEAP_CAP       (4u * 1024u * 1024u)
#define R3_STR_MAX        512u             /* the longest string a call takes (paths, titles, labels) */

/* ---------------------------------------------------------------- the int 0x80 gate */
void mw_isr_80(void);
void mw_syscall_c(u32 *r);
/* The CPU has switched to the kernel stack (esp0) and pushed ss, esp, eflags, cs, eip. pushal makes the register block mw_syscall_c() reads and
 * writes (edi esi ebp esp ebx edx ecx eax: r[0..7], eip r[8], cs r[9], eflags r[10]); cld because a program may have set the direction flag. The kernel
 * runs with ds = es = 0x10; the ring-3 selectors (0x33) are put back before the iret, or the CPU would zero them on the way out. */
__asm__(
    ".text\n"
    ".globl mw_isr_80\n"
    "mw_isr_80:\n"
    "    pushal\n"
    "    cld\n"
    "    movw $0x10, %ax\n"
    "    movw %ax, %ds\n"
    "    movw %ax, %es\n"
    "    movl %esp, %eax\n"
    "    pushl %eax\n"
    "    call mw_syscall_c\n"
    "    addl $4, %esp\n"
    "    movw $0x33, %ax\n"
    "    movw %ax, %ds\n"
    "    movw %ax, %es\n"
    "    popal\n"
    "    iret\n"
);

static void r3_init(void) {                                            /* installs vector 0x80: an interrupt gate a ring-3 program may use (DPL 3) */
    u32 cs;
    __asm__ volatile ("movl %%cs, %0" : "=r"(cs));
    u32 a = (u32)(unsigned long)mw_isr_80;
    idt_gates[0x80].lo = (a & 0xFFFFu) | ((cs & 0xFFFFu) << 16);
    idt_gates[0x80].hi = (a & 0xFFFF0000u) | 0xEE00u;                  /* present, ring 3, 32-bit interrupt gate */
}

/* The kernel's own segments (after a fault or an exit left ds/es at the program's 0x33). */
static inline void r3_kernel_segs(void) {
    __asm__ volatile ("movw $0x10, %%ax\n movw %%ax, %%ds\n movw %%ax, %%es\n movw %%ax, %%fs\n movw %%ax, %%gs\n" : : : "eax");
}

/* ---------------------------------------------------------------- what the program owns */
#define R3_SLOT_BASE MWP_LOAD_ADDR
#define R3_SLOT_END  MW_MWP_END
#define R3_SLOT_PAGES ((R3_SLOT_END - R3_SLOT_BASE) / MM_PAGE)

/* Flushes the TLB of the live address space (a page changed owner or went away while the CPU may still remember it). */
static inline void r3_flush(void) { as_enter(&pw.as); }

/* Called by pw_launch_args() after mwp_load() has put the file in the kernel's loader slot: the program gets an ADDRESS SPACE of its own -- the
 * slot's pages are copied into pages that are its alone and mapped at the same virtual addresses (0x8F1000 ..), the rest of the slot is zero (so
 * globals start at 0), and it gets a 64 KB stack with an unmapped guard page under it. The kernel's slot is free for the next program at once.
 * Returns 0, or a sentence. */
static const char *r3_prepare(u32 loaded_bytes) {
    if (!pg_enabled) return "This program needs memory protection, which this computer's CPU or memory setup does not provide.";
    if (!as_create(&pw.as)) return "Not enough memory to start it.";
    if (pw.compat) {                                                    /* a classic program: its table at 0x8F0000 is a page of its own, made of int 0x80 stubs */
        pw.compat_phys = (u32)(unsigned long)mm_pages_alloc_z(1, 1);
        if (!pw.compat_phys) return "Not enough memory to start it.";
        u8 *pg = (u8 *)(unsigned long)pw.compat_phys;
        for (u32 i = 0; i < COMPAT_RT_SIZE; i++) pg[i] = compat_rt_blob[i];
        *(u32 *)(pg + (COMPAT_RT_ENTRY_PTR - MWP_SYSCALL_TABLE_ADDR)) = (u32)(unsigned long)pw.entry;     /* the program's own entry, which compat_start() calls */
        if (!as_map_page(&pw.as, MWP_SYSCALL_TABLE_ADDR, pw.compat_phys, PG_P | PG_RW | PG_US)) return "Not enough memory to start it.";
        pw.r3_entry = COMPAT_RT_START;
    } else pw.r3_entry = (u32)(unsigned long)pw.entry;
    pw.slot_phys = (u32)(unsigned long)mm_pages_alloc_z(R3_SLOT_PAGES, 1);
    if (!pw.slot_phys) return "Not enough memory to start it.";
    for (u32 i = 0; i < loaded_bytes && i < R3_SLOT_PAGES * MM_PAGE; i++) ((u8 *)(unsigned long)pw.slot_phys)[i] = ((const u8 *)(unsigned long)R3_SLOT_BASE)[i];
    for (u32 i = 0; i < R3_SLOT_PAGES; i++)
        if (!as_map_page(&pw.as, R3_SLOT_BASE + i * MM_PAGE, pw.slot_phys + i * MM_PAGE, PG_P | PG_RW | PG_US)) return "Not enough memory to start it.";
    u32 pages = R3_USTACK_BYTES / MM_PAGE + 1;
    pw.ustack_alloc = (u8 *)mm_pages_alloc_z(pages, 1);
    if (!pw.ustack_alloc) return "Not enough memory to start it.";
    pw.ustack_pages = pages;
    u32 sb = (u32)(unsigned long)pw.ustack_alloc;
    as_unmap(&pw.as, sb);                                               /* the lowest page is not there for the program: a runaway recursion faults on it */
    if (!as_user_range(&pw.as, sb + MM_PAGE, R3_USTACK_BYTES, 1)) return "Not enough memory to start it.";
    return 0;
}

/* Gives everything of the ring-3 run back (pw_free_run() calls it, under the KERNEL's directory): the program's heap blocks, stack, slot pages, then the
 * address space itself. Nothing of it is reachable from any other program: their directories never had these pages as user pages. */
static void r3_release(void) {
    for (int i = 0; i < MWP_ALLOC_MAX; i++) if (pw.ublk[i].p) {
        mm_pages_free((void *)(unsigned long)pw.ublk[i].p, pw.ublk[i].pages);
        pw.ublk[i].p = 0; pw.ublk[i].pages = 0;
    }
    pw.ubytes = 0;
    if (pw.ustack_alloc) { mm_pages_free(pw.ustack_alloc, pw.ustack_pages); pw.ustack_alloc = 0; pw.ustack_pages = 0; }
    if (pw.slot_phys) { mm_pages_free((void *)(unsigned long)pw.slot_phys, R3_SLOT_PAGES); pw.slot_phys = 0; }
    if (pw.compat_phys) { mm_pages_free((void *)(unsigned long)pw.compat_phys, 1); pw.compat_phys = 0; }
    as_destroy(&pw.as);
}

/* Heap blocks. Each is whole pages plus ONE MORE page after them that is not mapped for the program (a guard), and the object is placed so that its
 * last byte is the last byte before the guard: a write or read one byte past the end of what it allocated is a page fault, caught on the spot,
 * not silent corruption (the classic "electric fence"). Underruns are not caught (the bytes before it are its own page); freeing a pointer that
 * was never handed out, or twice, is refused (and the program is stopped by the caller). Freed pages vanish from the program's space, so a use after
 * free faults too -- until a later allocation reuses them. Objects are 4-byte aligned. */
static void *r3_alloc(u32 n) {
    if (n == 0 || n > R3_HEAP_CAP) return 0;
    u32 sz = (n + 3u) & ~3u;
    u32 pages = (sz + MM_PAGE - 1) / MM_PAGE;
    if (pw.ubytes + pages * MM_PAGE > R3_HEAP_CAP) return 0;
    for (int i = 0; i < MWP_ALLOC_MAX; i++) {
        if (pw.ublk[i].p) continue;
        u8 *base = (u8 *)mm_pages_alloc_z(pages + 1, 1);                  /* the last page is the guard */
        if (!base) return 0;
        u32 b = (u32)(unsigned long)base;
        if (!as_user_range(&pw.as, b, pages * MM_PAGE, 1)) { mm_pages_free(base, pages + 1); return 0; }
        as_unmap(&pw.as, b + pages * MM_PAGE);
        pw.ublk[i].p = b; pw.ublk[i].pages = pages + 1; pw.ublk[i].user = b + pages * MM_PAGE - sz; pw.ubytes += pages * MM_PAGE;
        r3_flush();
        return (void *)(unsigned long)pw.ublk[i].user;
    }
    return 0;
}
/* 1 = freed, 0 = that is not a pointer this program was given (never allocated, already freed, or the middle of a block). */
static int r3_free(u32 p) {
    for (int i = 0; i < MWP_ALLOC_MAX; i++) {
        if (!p || !pw.ublk[i].p || pw.ublk[i].user != p) continue;
        u32 b = pw.ublk[i].p, pages = pw.ublk[i].pages;
        as_user_range(&pw.as, b, (pages - 1) * MM_PAGE, 0);
        as_map_page(&pw.as, b + (pages - 1) * MM_PAGE, b + (pages - 1) * MM_PAGE, PG_P | PG_RW);      /* the guard page is an ordinary kernel page again */
        r3_flush();
        mm_pages_free((void *)(unsigned long)b, pages);
        pw.ubytes -= (pages - 1) * MM_PAGE; pw.ublk[i].p = 0; pw.ublk[i].pages = 0; pw.ublk[i].user = 0;
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- entering ring 3 */
static void r3_enter(u32 eip, u32 esp) __attribute__((noreturn));
static void r3_enter(u32 eip, u32 esp) {
    __asm__ volatile (
        "movw $0x33, %%ax\n movw %%ax, %%ds\n movw %%ax, %%es\n movw %%ax, %%fs\n movw %%ax, %%gs\n"
        "pushl $0x33\n"                                                 /* ss: user data */
        "pushl %1\n"                                                    /* esp */
        "pushl $0x2\n"                                                  /* eflags: IF = 0, IOPL = 0 */
        "pushl $0x2B\n"                                                 /* cs: user code */
        "pushl %0\n"                                                    /* eip */
        "iret\n"
        : : "r"(eip), "r"(esp) : "eax", "memory");
    __builtin_unreachable();
}
/* pw_trampoline() calls this instead of the entry function: the program starts in ring 3 and the kernel only sees it again through int 0x80
 * and the exceptions. */
static void r3_run(void) __attribute__((noreturn));
static void r3_run(void) {
    u32 top = ((u32)(unsigned long)pw.ustack_alloc + pw.ustack_pages * MM_PAGE) - 16u;
    ((u32 *)(unsigned long)top)[0] = 0;                                 /* a return address of 0: an entry function that returns faults on the null page */
    r3_enter(pw.r3_entry, top);
}

/* ---------------------------------------------------------------- checking what the program hands us */
static int r3_str(u32 a, u32 maxlen) {                                  /* length of the NUL-terminated string at a, or -1 if it leaves the program's pages */
    for (u32 i = 0; i < maxlen; i++) {
        if (i == 0 || ((a + i) & 0xFFFu) == 0) { if (!pg_user_ok(a + i, 1, 0)) return -1; }
        if (*(const char *)(unsigned long)(a + i) == 0) return (int)i;
    }
    return -1;
}
static int r3_copy_out(u32 buf, u32 size, const char *s) {              /* a string into the program's buffer (always terminated); returns its length */
    if (!size || !pg_user_ok(buf, size, 1)) return -1;
    u32 n = 0;
    while (s[n] && n + 1 < size) { ((char *)(unsigned long)buf)[n] = s[n]; n++; }
    ((char *)(unsigned long)buf)[n] = 0;
    return (int)n;
}

/* The records MW_SYS_DIR_LIST writes: [u32 size][u32 is_dir][name, NUL, padded to 4 bytes], one after the other. */
typedef struct { u8 *buf; u32 size, used; int count; } r3_ls_t;
static int r3_ls_cb(const char *name, int is_dir, u32 size, void *ctx) {
    r3_ls_t *x = (r3_ls_t *)ctx;
    u32 nl = 0; while (name[nl]) nl++;
    u32 rec = 8u + ((nl + 1u + 3u) & ~3u);
    if (x->used + rec > x->size) return 0;                               /* the buffer is full: stop here */
    u32 *h = (u32 *)(x->buf + x->used);
    h[0] = size; h[1] = (u32)(is_dir != 0);
    char *d = (char *)(x->buf + x->used + 8);
    for (u32 i = 0; i < rec - 8u; i++) d[i] = i < nl ? name[i] : 0;
    x->used += rec; x->count++;
    return 1;
}

/* ---------------------------------------------------------------- the system calls */
#define R3_BAD  (-1)
#define A0 (r[4])            /* ebx */
#define A1 (r[6])            /* ecx */
#define A2 (r[5])            /* edx */
#define A3 (r[1])            /* esi */
#define A4 (r[0])            /* edi */
__attribute__((used)) void mw_syscall_c(u32 *r) {
    u32 n = r[7];
    int ret = 0;
    int sl;
    if (!pw_cur || !pw.ring3) { r[7] = (u32)R3_BAD; return; }          /* only the program in the window may call */
    switch (n) {
    case MW_SYS_EXIT:
        pw.finished = 1;
        mw_coro_switch(&pw.sp, pw.caller_sp);                          /* the desktop takes over for good; pw_close() frees everything */
        break;
    case MW_SYS_PRESENT:   mwp_sys_present(); break;
    case MW_SYS_KEY_POLL:  ret = mwp_sys_key_poll(); break;
    case MW_SYS_PUT_PIXEL: bb_putpixel((int)A0, (int)A1, A2); break;
    case MW_SYS_FILL_RECT: bb_fillrect((int)A0, (int)A1, (int)A2, (int)A3, A4); break;
    case MW_SYS_DRAW_RECT: bb_rect((int)A0, (int)A1, (int)A2, (int)A3, A4); break;
    case MW_SYS_DRAW_STRING: case MW_SYS_DRAW_STRING_KO:
        if (r3_str(A2, R3_STR_MAX) < 0) { ret = R3_BAD; break; }
        if (n == MW_SYS_DRAW_STRING) font_draw_string((int)A0, (int)A1, (const char *)(unsigned long)A2, A3);
        else ko_draw_mixed_string((int)A0, (int)A1, (const char *)(unsigned long)A2, A3);
        break;
    case MW_SYS_LOG:       if (r3_str(A0, R3_STR_MAX) < 0) ret = R3_BAD; else mwp_sys_log((const char *)(unsigned long)A0); break;
    case MW_SYS_TICKS_MS:  ret = (int)mwp_sys_ticks_ms(); break;
    case MW_SYS_SLEEP_MS:  mwp_sys_sleep_ms(A0); break;
    case MW_SYS_SCREEN_W:  ret = pw.fullscreen ? (int)VGA_WIDTH : pw_cw(); break;               /* the window's client area (the whole screen with `run -f`) */
    case MW_SYS_SCREEN_H:  ret = pw.fullscreen ? (int)VGA_HEIGHT : pw_ch(); break;
    case MW_SYS_RUN_FLAGS: ret = pw.fullscreen ? 0 : MW_RUN_WINDOWED; break;
    case MW_SYS_ABORT:     pw.abort_code = (int)A0; pw.finished = 1; mw_coro_switch(&pw.sp, pw.caller_sp); break;      /* the program found itself corrupted */
    case MW_SYS_MOUSE_POLL: {
        int x = 0, y = 0, b = 0;
        if (!pg_user_ok(A0, 12, 1)) { ret = R3_BAD; break; }
        ret = mwp_sys_mouse_poll(&x, &y, &b);
        int *o = (int *)(unsigned long)A0; o[0] = x; o[1] = y; o[2] = b;
        break; }
    case MW_SYS_TIME_NOW:  if (!pg_user_ok(A0, sizeof(mw_time_t), 1)) ret = R3_BAD; else mwp_sys_time_now((mw_time_t *)(unsigned long)A0); break;
    case MW_SYS_RAND32:    ret = (int)mwp_sys_rand32(); break;
    case MW_SYS_THEME_COLOR: ret = (int)mwp_sys_theme_color((int)A0); break;
    case MW_SYS_TEXT_WIDTH: if (r3_str(A0, R3_STR_MAX) < 0) ret = R3_BAD; else ret = mwp_sys_text_width((const char *)(unsigned long)A0); break;
    case MW_SYS_BEVEL:     mwp_sys_bevel((int)A0, (int)A1, (int)A2, (int)A3, (int)A4); break;
    case MW_SYS_BUTTON: {                                              /* (int a[6]): x, y, w, h, label, pressed */
        if (!pg_user_ok(A0, 24, 0)) { ret = R3_BAD; break; }
        const int *a = (const int *)(unsigned long)A0;
        if (r3_str((u32)a[4], R3_STR_MAX) < 0) { ret = R3_BAD; break; }
        mwp_sys_button(a[0], a[1], a[2], a[3], (const char *)(unsigned long)a[4], a[5]);
        break; }
    case MW_SYS_WINDOW_FRAME: {                                        /* (int a[6]): x, y, w, h, title, icon */
        if (!pg_user_ok(A0, 24, 0)) { ret = R3_BAD; break; }
        const int *a = (const int *)(unsigned long)A0;
        if (r3_str((u32)a[4], R3_STR_MAX) < 0) { ret = R3_BAD; break; }
        mwp_sys_window_frame(a[0], a[1], a[2], a[3], (const char *)(unsigned long)a[4], a[5]);
        break; }
    case MW_SYS_ICON16:    mwp_sys_icon16((int)A0, (int)A1, (int)A2); break;
    case MW_SYS_ICON12:    mwp_sys_icon12((int)A0, (int)A1, (int)A2); break;
    case MW_SYS_SET_TITLE: if (r3_str(A0, R3_STR_MAX) < 0) ret = R3_BAD; else mwp_sys_set_title((const char *)(unsigned long)A0); break;
    case MW_SYS_SET_SIZE:  mwp_sys_set_size((int)A0, (int)A1); break;
    case MW_SYS_CLIP_GET:  if (A1 && !pg_user_ok(A0, A1, 1)) ret = R3_BAD; else ret = mwp_sys_clip_get((void *)(unsigned long)A0, A1); break;
    case MW_SYS_CLIP_SET:  if (A1 > 4096u || !pg_user_ok(A0, A1, 0)) ret = 0; else ret = mwp_sys_clip_set((const void *)(unsigned long)A0, A1); break;
    case MW_SYS_MEM_ALLOC: ret = (int)(unsigned long)r3_alloc(A0); break;
    case MW_SYS_MEM_FREE:                                                /* a pointer that was never handed out, or already freed: the program is corrupted */
        ret = r3_free(A0);
        if (!ret) { pw.abort_code = 2; pw.finished = 1; mw_coro_switch(&pw.sp, pw.caller_sp); }
        break;
    case MW_SYS_MEM_FREE_KB: ret = (int)mwp_sys_mem_free_kb(); break;
    case MW_SYS_FILE_SIZE: case MW_SYS_FILE_DELETE: case MW_SYS_DIR_MAKE:
        if (r3_str(A0, R3_STR_MAX) < 0) { ret = -MWFS_E_NAME; break; }
        if (n == MW_SYS_FILE_SIZE) ret = mwp_sys_file_size((const char *)(unsigned long)A0);
        else if (n == MW_SYS_FILE_DELETE) ret = mwp_sys_file_delete((const char *)(unsigned long)A0);
        else ret = mwp_sys_dir_make((const char *)(unsigned long)A0);
        break;
    case MW_SYS_FILE_READ:
        if (r3_str(A0, R3_STR_MAX) < 0 || !pg_user_ok(A1, A2, 1)) { ret = -MWFS_E_NAME; break; }
        ret = mwp_sys_file_read((const char *)(unsigned long)A0, (void *)(unsigned long)A1, A2);
        break;
    case MW_SYS_FILE_WRITE:
        if (r3_str(A0, R3_STR_MAX) < 0 || !pg_user_ok(A1, A2, 0)) { ret = -MWFS_E_NAME; break; }
        ret = mwp_sys_file_write((const char *)(unsigned long)A0, (const void *)(unsigned long)A1, A2);
        break;
    case MW_SYS_DIR_LIST: {                                            /* (path, buf, size): the records of r3_ls_cb(); returns the entry count */
        if (r3_str(A0, R3_STR_MAX) < 0 || !pg_user_ok(A1, A2, 1)) { ret = -MWFS_E_NAME; break; }
        r3_ls_t x; x.buf = (u8 *)(unsigned long)A1; x.size = A2; x.used = 0; x.count = 0;
        int e = mwp_sys_dir_list((const char *)(unsigned long)A0, r3_ls_cb, &x);
        ret = e < 0 ? e : x.count;
        break; }
    case MW_SYS_ERR_TEXT:  ret = r3_copy_out(A1, A2, mwp_sys_err_text((int)A0)); break;
    case MW_SYS_BEEP:      mwp_sys_beep(A0, A1); break;
    case MW_SYS_ARGS:      ret = r3_copy_out(A0, A1, pw.args); break;
    case MW_SYS_MENU_POPUP:
        sl = r3_str(A2, R3_STR_MAX);
        if (sl < 0) { ret = -1; break; }
        ret = mwp_sys_menu_popup((int)A0, (int)A1, (const char *)(unsigned long)A2);
        break;
    case MW_SYS_SET_ICON:  if (!pg_user_ok(A0, 1024, 0)) ret = R3_BAD; else mwp_sys_set_icon((const u32 *)(unsigned long)A0); break;
    case MW_SYS_SET_CURSOR: mwp_sys_set_cursor((int)A0); break;
    default: ret = R3_BAD; break;                                      /* an unknown call number */
    }
    r[7] = (u32)ret;
}
#undef A0
#undef A1
#undef A2
#undef A3
#undef A4

#endif
