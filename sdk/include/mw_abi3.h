/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_ABI3_H
#define MW_ABI3_H

/* mw_abi3.h -- the program's side of ABI 3: system calls for a program that runs in RING 3 (built with `tools/build_mwp.sh --ring3`, or with a
 * comment line  / * @mwp ring3 * /  in the source). miniwin.h includes this instead of using the fixed-address table.
 *
 * A ring-3 program cannot read the kernel's table of function pointers (that page belongs to the kernel) and could not call a kernel function if
 * it could: it would be running kernel code at the wrong privilege. Everything it asks of the kernel is `int 0x80`: eax = a MW_SYS_* number
 * (mwp_abi.h), ebx ecx edx esi edi = the arguments, the answer comes back in eax; the kernel checks every pointer against the pages the program
 * owns. This header keeps the SOURCE the same for both ways of running: it builds `mw3_table`, a table of the same type as the kernel's, in the
 * program's own memory, whose entries are small stubs that make the call -- and `sys` (miniwin.h) means that table. A program written for the
 * table runs unchanged in ring 3.
 *
 * Differences a program can notice, all small:
 *   - screen_w / screen_h are refreshed after present(), key_poll() and sleep_ms() (they are system calls here, not fields the kernel writes);
 *   - mem_alloc hands out whole 4 KB pages (zero-filled; at most 64 blocks and 4 MB), so many tiny allocations are better made from one block --
 *     and each block ends exactly at an unmapped guard page: one byte past what you allocated is a page fault; freeing a pointer you were not
 *     given, or twice, stops the program ("invalid free"); touching a block after freeing it is a page fault too;
 *   - dir_list calls your callback from a copy of the first 8 KB of the listing (about 200 entries);
 *   - err_text keeps its text in a 96-byte buffer of the program, overwritten by the next call;
 *   - touching anything that is not the program's -- the kernel, the screen, a device, address 0 -- is a page fault that stops the program (and
 *     only it); cli/sti/in/out and friends are a general protection fault.
 * The entry function is still written `void mwp_entry(void)`: this header renames it (below) and puts a small start-up in front of it. */

#ifndef MWP_ABI_H
#include "mwp_abi.h"
#endif

static inline int mw3_sc(int n, int a, int b, int c, int d, int e) {
    int r;
    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e) : "memory", "cc");
    return r;
}

#ifndef MW3_TABLE_SECTION
#define MW3_TABLE_SECTION                   /* kernel/compat_rt.c puts the table at the fixed address classic programs expect ... */
#define MW3_TABLE_STORAGE static
#endif
#ifndef MW3_TABLE_STORAGE
#define MW3_TABLE_STORAGE                   /* ... and gives it external linkage: nobody in that file reads it, so a static one would be optimised away */
#endif
MW3_TABLE_STORAGE mwp_syscalls_t mw3_table MW3_TABLE_SECTION;            /* filled in by mw3_init() below; `sys` in miniwin.h is this */
static char mw3_errbuf[96] = { 'e' };       /* (initialised, so it lives in .data: the linker script has no .bss) */
static char mw3_argbuf[64]  = { 'a' };

static void mw3_refresh(void) { mw3_table.screen_w = (u32)mw3_sc(MW_SYS_SCREEN_W, 0, 0, 0, 0, 0); mw3_table.screen_h = (u32)mw3_sc(MW_SYS_SCREEN_H, 0, 0, 0, 0, 0); }

static void s3_put_pixel(int x, int y, u32 c) { mw3_sc(MW_SYS_PUT_PIXEL, x, y, (int)c, 0, 0); }
static void s3_fill_rect(int x, int y, int w, int h, u32 c) { mw3_sc(MW_SYS_FILL_RECT, x, y, w, h, (int)c); }
static void s3_draw_rect(int x, int y, int w, int h, u32 c) { mw3_sc(MW_SYS_DRAW_RECT, x, y, w, h, (int)c); }
static void s3_draw_string(int x, int y, const char *s, u32 c) { mw3_sc(MW_SYS_DRAW_STRING, x, y, (int)s, (int)c, 0); }
static void s3_draw_string_ko(int x, int y, const char *s, u32 c) { mw3_sc(MW_SYS_DRAW_STRING_KO, x, y, (int)s, (int)c, 0); }
static void s3_present(void) { mw3_sc(MW_SYS_PRESENT, 0, 0, 0, 0, 0); mw3_refresh(); }
static int  s3_key_poll(void) { int k = mw3_sc(MW_SYS_KEY_POLL, 0, 0, 0, 0, 0); mw3_refresh(); return k; }
static void *s3_mem_alloc(u32 n) { return (void *)mw3_sc(MW_SYS_MEM_ALLOC, (int)n, 0, 0, 0, 0); }
static int  s3_mem_free(void *p) { return mw3_sc(MW_SYS_MEM_FREE, (int)p, 0, 0, 0, 0); }
static u32  s3_mem_free_kb(void) { return (u32)mw3_sc(MW_SYS_MEM_FREE_KB, 0, 0, 0, 0, 0); }
static int  s3_file_size(const char *p) { return mw3_sc(MW_SYS_FILE_SIZE, (int)p, 0, 0, 0, 0); }
static int  s3_file_read(const char *p, void *b, u32 max) { return mw3_sc(MW_SYS_FILE_READ, (int)p, (int)b, (int)max, 0, 0); }
static int  s3_file_write(const char *p, const void *d, u32 len) { return mw3_sc(MW_SYS_FILE_WRITE, (int)p, (int)d, (int)len, 0, 0); }
static int  s3_file_delete(const char *p) { return mw3_sc(MW_SYS_FILE_DELETE, (int)p, 0, 0, 0, 0); }
static int  s3_dir_make(const char *p) { return mw3_sc(MW_SYS_DIR_MAKE, (int)p, 0, 0, 0, 0); }
/* the kernel fills a buffer with records [u32 size][u32 is_dir][name\0, padded to 4]; we walk it and call the program's callback */
static int  s3_dir_list(const char *path, mw_dir_cb cb, void *ctx) {
    enum { BUF = 8192 };
    unsigned char *buf = (unsigned char *)mw3_sc(MW_SYS_MEM_ALLOC, BUF, 0, 0, 0, 0);
    if (!buf) return -2;
    int n = mw3_sc(MW_SYS_DIR_LIST, (int)path, (int)buf, BUF, 0, 0);
    if (n > 0) {
        unsigned off = 0;
        for (int i = 0; i < n; i++) {
            const u32 *h = (const u32 *)(buf + off);
            const char *name = (const char *)(buf + off + 8);
            unsigned nl = 0; while (name[nl]) nl++;
            if (!cb(name, (int)h[1], h[0], ctx)) { n = i + 1; break; }
            off += 8u + ((nl + 1u + 3u) & ~3u);
        }
    }
    mw3_sc(MW_SYS_MEM_FREE, (int)buf, 0, 0, 0, 0);
    return n;
}
static const char *s3_err_text(int err) { mw3_sc(MW_SYS_ERR_TEXT, err, (int)mw3_errbuf, (int)sizeof mw3_errbuf, 0, 0); return mw3_errbuf; }
static u32  s3_ticks_ms(void) { return (u32)mw3_sc(MW_SYS_TICKS_MS, 0, 0, 0, 0, 0); }
static void s3_sleep_ms(u32 ms) { mw3_sc(MW_SYS_SLEEP_MS, (int)ms, 0, 0, 0, 0); mw3_refresh(); }
static void s3_time_now(mw_time_t *t) { mw3_sc(MW_SYS_TIME_NOW, (int)t, 0, 0, 0, 0); }
static int  s3_mouse_poll(int *x, int *y, int *b) { int o[3] = { 0, 0, 0 }; int r = mw3_sc(MW_SYS_MOUSE_POLL, (int)o, 0, 0, 0, 0); if (x) *x = o[0]; if (y) *y = o[1]; if (b) *b = o[2]; return r; }
static void s3_draw_icon16(int x, int y, int i) { mw3_sc(MW_SYS_ICON16, x, y, i, 0, 0); }
static void s3_draw_icon12(int x, int y, int i) { mw3_sc(MW_SYS_ICON12, x, y, i, 0, 0); }
static int  s3_text_width(const char *s) { return mw3_sc(MW_SYS_TEXT_WIDTH, (int)s, 0, 0, 0, 0); }
static void s3_bevel(int x, int y, int w, int h, int pressed) { mw3_sc(MW_SYS_BEVEL, x, y, w, h, pressed); }
static void s3_button(int x, int y, int w, int h, const char *label, int pressed) { int a[6] = { x, y, w, h, (int)label, pressed }; mw3_sc(MW_SYS_BUTTON, (int)a, 0, 0, 0, 0); }
static void s3_window_frame(int x, int y, int w, int h, const char *title, int icon) { int a[6] = { x, y, w, h, (int)title, icon }; mw3_sc(MW_SYS_WINDOW_FRAME, (int)a, 0, 0, 0, 0); }
static void s3_log(const char *s) { mw3_sc(MW_SYS_LOG, (int)s, 0, 0, 0, 0); }
static u32  s3_rand32(void) { return (u32)mw3_sc(MW_SYS_RAND32, 0, 0, 0, 0, 0); }
static void s3_set_title(const char *s) { mw3_sc(MW_SYS_SET_TITLE, (int)s, 0, 0, 0, 0); }
static int  s3_clipboard_get(void *b, u32 max) { return mw3_sc(MW_SYS_CLIP_GET, (int)b, (int)max, 0, 0, 0); }
static int  s3_clipboard_set(const void *d, u32 len) { return mw3_sc(MW_SYS_CLIP_SET, (int)d, (int)len, 0, 0, 0); }
static void s3_set_size(int w, int h) { mw3_sc(MW_SYS_SET_SIZE, w, h, 0, 0, 0); mw3_refresh(); }
static void s3_set_icon(const u32 *argb16) { mw3_sc(MW_SYS_SET_ICON, (int)argb16, 0, 0, 0, 0); }
static u32  s3_theme_color(int id) { return (u32)mw3_sc(MW_SYS_THEME_COLOR, id, 0, 0, 0, 0); }
static void s3_beep(u32 hz, u32 ms) { mw3_sc(MW_SYS_BEEP, (int)hz, (int)ms, 0, 0, 0); }
static int  s3_menu_popup(int x, int y, const char *items) { return mw3_sc(MW_SYS_MENU_POPUP, x, y, (int)items, 0, 0); }
static void s3_set_cursor(int kind) { mw3_sc(MW_SYS_SET_CURSOR, kind, 0, 0, 0, 0); }

/* (the definition proper: with an initialiser, so the table is in .data -- the linker script has no .bss) */
MW3_TABLE_STORAGE mwp_syscalls_t mw3_table MW3_TABLE_SECTION = { .put_pixel = s3_put_pixel };
/* The stack protector (build_mwp.sh --ring3 compiles with -fstack-protector-strong -mstack-protector-guard=global): every function with a local array
 * keeps a random canary between its locals and its return address and checks it before returning; a buffer overflow that reaches the return address
 * has to overwrite it first, and the program stops itself with MW_SYS_ABORT instead of being hijacked. The canary is chosen at start-up (below). */
unsigned long __stack_chk_guard = 0x595E9FBDu;
void __attribute__((noreturn)) __stack_chk_fail(void) {
    mw3_sc(MW_SYS_ABORT, 1, 0, 0, 0, 0);
    for (;;) { }
}
static void __attribute__((no_stack_protector)) mw3_init(void) {
    mwp_syscalls_t *t = &mw3_table;
    __stack_chk_guard = (unsigned long)mw3_sc(MW_SYS_RAND32, 0, 0, 0, 0, 0) | 0x01000000ul;      /* (before any protected function is running) */
    t->put_pixel = s3_put_pixel; t->fill_rect = s3_fill_rect; t->draw_rect = s3_draw_rect; t->draw_string = s3_draw_string; t->draw_string_ko = s3_draw_string_ko;
    t->present = s3_present; t->key_poll = s3_key_poll;
    t->abi_version = MWP_ABI_VERSION; t->table_bytes = (u32)sizeof(mwp_syscalls_t);
    t->mem_alloc = s3_mem_alloc; t->mem_free = s3_mem_free; t->mem_free_kb = s3_mem_free_kb;
    t->file_size = s3_file_size; t->file_read = s3_file_read; t->file_write = s3_file_write; t->file_delete = s3_file_delete; t->dir_make = s3_dir_make;
    t->dir_list = s3_dir_list; t->err_text = s3_err_text;
    t->ticks_ms = s3_ticks_ms; t->sleep_ms = s3_sleep_ms; t->time_now = s3_time_now; t->mouse_poll = s3_mouse_poll;
    t->draw_icon16 = s3_draw_icon16; t->draw_icon12 = s3_draw_icon12; t->text_width = s3_text_width; t->bevel = s3_bevel; t->button = s3_button;
    t->window_frame = s3_window_frame; t->log = s3_log; t->rand32 = s3_rand32;
    t->run_flags = (u32)mw3_sc(MW_SYS_RUN_FLAGS, 0, 0, 0, 0, 0);          /* windowed, or full screen when started with `run -f` */
    t->set_title = s3_set_title;
    mw3_sc(MW_SYS_ARGS, (int)mw3_argbuf, (int)sizeof mw3_argbuf, 0, 0, 0); t->args = mw3_argbuf;
    t->clipboard_get = s3_clipboard_get; t->clipboard_set = s3_clipboard_set; t->set_size = s3_set_size; t->set_icon = s3_set_icon;
    t->theme_color = s3_theme_color; t->beep = s3_beep; t->menu_popup = s3_menu_popup; t->set_cursor = s3_set_cursor;
    mw3_refresh();
}

/* The start-up: byte 0 of the program (the linker script puts mwp_entry first). It sets the table up, runs the program's own entry function --
 * which the program's source calls mwp_entry too, renamed by the #define below -- and ends the program with the exit call (a ring-3 function
 * must not simply `ret` out of the program: there is no caller to return to). */
#ifndef MW3_NO_CRT                          /* (kernel/compat_rt.c has its own start-up) */
void mw3_main(void);
void __attribute__((no_stack_protector)) mwp_entry(void) {
    mw3_init();
    mw3_main();
    mw3_sc(MW_SYS_EXIT, 0, 0, 0, 0, 0);
    for (;;) { }
}
#define mwp_entry mw3_main

#define sys mw3_table
#endif

#endif
