/* SPDX-License-Identifier: GPL-3.0-only */
/* @mwp title: Ring3 */
/* @mwp ring3 */
/* ring3.c -- a program that runs in RING 3 (1.1-alpha-14): the CPU's lowest privilege level, in pages of its own, asking the kernel for everything with
 * `int 0x80` (sdk/include/mw_abi3.h). The same source would run in ring 0; what changes is what a mistake costs. Build and install:
 *   tools/build_mwp.sh sdk/examples/ring3.c      (the "@mwp ring3" line above is what selects ring 3)
 * The top line shows the privilege level the CPU reports (the low two bits of CS). Then try, each stops ONLY this program, with a sentence in
 * the status line saying what the CPU refused:
 *   1 write into the kernel's code      2 read the kernel's system-call table      3 cli (turn interrupts off)      4 in al, 0x60 (read the keyboard port)
 * and the ones that must keep working:
 *   5 allocate memory, write a file, read it back, delete it (every call is a system call)      6 60 levels of recursion (fine)
 *   7 runaway recursion: the guard page under the program's stack faults, only the program stops
 *   8 write past the end of a heap block (the guard page right after it: page fault; the up-to-3 bytes of alignment padding are the block's own)   9 use a block after freeing it (page fault)
 *   0 free the same block twice (the program is stopped: invalid free)                      b overflow a local buffer (the stack protector stops it)
 * (The mouse and the window's own controls are not involved: it is a keyboard program.) */
#include "miniwin.h"

static int privilege_level(void) { unsigned cs; __asm__ volatile ("movl %%cs, %0" : "=r"(cs)); return (int)(cs & 3u); }
static void __attribute__((noinline)) smash(const char *src) { volatile char buf[8]; int i = 0; while (src[i] && i < 60) { buf[i] = src[i]; i++; } buf[i] = 0; }
static int runaway(int n) { volatile char pad[400]; pad[0] = (char)n; return runaway(n + 1) + pad[0]; }
static int depth(int n) { volatile char pad[200]; pad[0] = (char)n; return n == 0 ? 0 : depth(n - 1) + pad[0] - n + 1; }

void mwp_entry(void) {
    static char status[80] = "press a key";                     /* initialised: it lives in .data (the program's own, writable pages) */
    for (;;) {
        u32 face = mw_ui_color(MW_TH_FACE), text = mw_ui_color(MW_TH_TEXT);
        mw_fill_screen(face);
        char line[40]; int n = 0; char t[12];
        line[0] = 0; mw_append(line, &n, "CPU privilege level (CPL): "); mw_append(line, &n, mw_itoa(privilege_level(), t));
        mw_text(8, 8, line, text);
        { char fb[32]; int m = 0; fb[0] = 0; mw_append(fb, &m, "free RAM KB: "); mw_append(fb, &m, mw_itoa((int)sys.mem_free_kb(), t)); mw_text(8, 166, fb, mw_ui_color(MW_TH_SHADOW)); }
        mw_text(8, 26, "1  write into the kernel", text);
        mw_text(8, 40, "2  read the kernel's syscall table", text);
        mw_text(8, 54, "3  cli", text);
        mw_text(8, 68, "4  in al, 0x60", text);
        mw_text(8, 82, "5  memory + file system calls", text);
        mw_text(8, 96, "6  deep recursion", text);
        mw_text(8, 110, "7  runaway recursion   8 heap +1   9 freed   0 free x2   b smash", text);
        mw_text(8, 130, status, mw_ui_color(MW_TH_ACCENT));
        mw_text(8, 150, "ESC quits", mw_ui_color(MW_TH_SHADOW));
        sys.present();
        int k = sys.key_poll();
        if (k == MW_KEY_ESC) return;
        if (k == '1') *(volatile unsigned *)0x900000u = 0xDEAD;                 /* the kernel's first byte: supervisor + read-only */
        if (k == '2') { volatile unsigned v = *(volatile unsigned *)0x8F0000u; (void)v; }     /* the kernel's syscall table page */
        if (k == '3') __asm__ volatile ("cli");
        if (k == '4') { unsigned char b; __asm__ volatile ("inb $0x60, %0" : "=a"(b)); (void)b; }
        if (k == '6') { volatile int d = depth(60); (void)d; status[0] = 'r'; status[1] = 'e'; status[2] = 'c'; status[3] = 'u'; status[4] = 'r'; status[5] = 's'; status[6] = 'i'; status[7] = 'o'; status[8] = 'n'; status[9] = ' '; status[10] = 'o'; status[11] = 'k'; status[12] = 0; }
        if (k == '8') { char *p = sys.mem_alloc(10); if (p) { for (int i = 0; i <= 12; i++) ((volatile char *)p)[i] = 1;      /* 10 bytes are rounded up to 12 (4-byte alignment): the 13th is the first one the guard page catches */ } }
        if (k == '9') { char *p = sys.mem_alloc(10); if (p) { sys.mem_free(p); ((volatile char *)p)[0] = 1; } }
        if (k == '0') { char *p = sys.mem_alloc(10); if (p) { sys.mem_free(p); sys.mem_free(p); } }
        if (k == 'b') smash("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
        if (k == '7') { volatile int r = runaway(0); (void)r; }
        if (k == '5') {
            char *buf = sys.mem_alloc(100); int ok = 0;
            if (buf) {
                for (int i = 0; i < 99; i++) buf[i] = (char)('A' + i % 26);
                buf[99] = 0;
                int w = sys.file_write("/home/docs/R3TEST.TXT", buf, 100);
                char back[100]; for (int i = 0; i < 100; i++) back[i] = 0;
                int r = sys.file_read("/home/docs/R3TEST.TXT", back, 100);
                int same = 1; for (int i = 0; i < 100; i++) if (back[i] != buf[i]) same = 0;
                int d = sys.file_delete("/home/docs/R3TEST.TXT");
                ok = (w == 0 && r == 100 && same && d == 0);
                sys.mem_free(buf);
            }
            n = 0; status[0] = 0;
            mw_append(status, &n, ok ? "memory and file calls: OK" : "memory and file calls: FAILED");
        }
    }
}
