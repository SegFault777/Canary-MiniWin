/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MWP_ABI_H
#define MWP_ABI_H

/* mwp_abi.h -- THE contract between the MiniWin kernel and a loadable program (.MWP): the syscall table.
 *
 * ONE FILE, TWO READERS. kernel/mwp.h includes this file to fill the table in; sdk/include/miniwin.h includes it so a program
 * is compiled against exactly the same layout. (Before 1.1-alpha-10 each side kept its own hand-copied struct and a comment
 * begged the next editor to keep them in step.) Change this file and BOTH sides change together.
 *
 * ABI RULES: the table is at a fixed address (MWP_SYSCALL_TABLE_ADDR). Fields are only ever APPENDED; an existing field
 * never moves and never changes meaning, so a program built against version 1 keeps working on a version 2 kernel. A program
 * that wants a newer function checks `sys.abi_version` first (miniwin.h: mw_require(2)). Version history:
 *   1  (alpha-3)   drawing, present, key_poll, screen size
 *   2  (alpha-10)  memory (heap), files and folders, time, mouse, icons, widgets, text width, serial log
 *   3  (alpha-11)  run_flags (is the program in a window?), set_title; a program started by `run` now lives in a WINDOW and
 *                  sys.screen_w/screen_h are the size of its client area (they can change between frames: read them every frame)
 *   4  (alpha-14)  menu_popup: a right-click (context) menu a program builds itself. Also the first version a program can run in RING 3.
 *
 * TWO WAYS TO CALL THE KERNEL (alpha-14). A program is built for one of them:
 *   ABI 1/2 (everything before alpha-14): the table above, called directly. The program runs in ring 0 on the kernel's stack; there is no
 *             memory protection (see kernel/mwp.h).
 *   ABI 3     (MW_RING3, a trailer flag set by `tools/build_mwp.sh --ring3`, or an "@mwp ring3" line in a source comment): the program runs in RING 3 -- in its own
 *             pages, with its own stack, unable to touch the kernel -- and asks for things with `int 0x80`: eax = a MW_SYS_* number,
 *             ebx, ecx, edx, esi, edi = up to five arguments, the result comes back in eax. (A call with six arguments passes ebx = a pointer
 *             to six ints instead.) sdk/include/mw_abi3.h hides all of this: it fills a user-side `sys` table with small stubs, so a
 *             program's source is the same for both. Every pointer is checked by the kernel against the pages the program owns. */

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#define MWP_ABI_VERSION 4

typedef struct { u16 year; u8 month, day, hour, minute, second, weekday; } mw_time_t;        /* weekday: 0 = Sunday */
/* One entry of dir_list: return non-zero to go on, 0 to stop. `size` is 0 for folders. */
typedef int (*mw_dir_cb)(const char *name, int is_dir, u32 size, void *ctx);

/* Icon numbers for draw_icon16 / draw_icon12 (the same order as kernel/ui/icon16.h; the kernel asserts it). */
enum {
    MWI_DOC, MWI_PROGRAM, MWI_FOLDER, MWI_FOLDER_OPEN, MWI_PC, MWI_DRIVE, MWI_IDE, MWI_SATA, MWI_NVME, MWI_SCSI, MWI_USB, MWI_FLOPPY, MWI_CD,
    MWI_HTML, MWI_CSS, MWI_JS, MWI_C, MWI_ASM, MWI_NOTEPAD, MWI_SETTING, MWI_WEB, MWI_TERMINAL, MWI_FILES, MWI_WARNING, MWI_ERROR, MWI_HELP,
    MWI_POWER, MWI_REFRESH, MWI_TRASH, MWI_SEARCH, MWI_SECURITY, MWI_EARTH, MWI_UNKNOWN, MWI_NEXT, MWI_UP
};

typedef struct {
    /* ---- version 1 ---- */
    void (*put_pixel)(int x, int y, u32 color);
    void (*fill_rect)(int x, int y, int w, int h, u32 color);
    void (*draw_rect)(int x, int y, int w, int h, u32 color);                   /* a 1-pixel outline */
    void (*draw_string)(int x, int y, const char *s, u32 color);                /* Latin only; every glyph is 11 pixels wide */
    void (*draw_string_ko)(int x, int y, const char *s, u32 color);             /* Latin + Hangul (UTF-8) */
    void (*present)(void);                                                      /* nothing drawn is visible until this */
    int  (*key_poll)(void);                                                     /* an ASCII byte, or 0 when none is waiting; never blocks */
    u32  screen_w;
    u32  screen_h;

    /* ---- version 2 ---- */
    u32  abi_version;                                                           /* MWP_ABI_VERSION of the kernel that filled this in */
    u32  table_bytes;                                                           /* sizeof(mwp_syscalls_t) of that kernel */
    /* memory: from the kernel heap (kernel/mm.h). Blocks a program forgets to free are freed when it returns. */
    void *(*mem_alloc)(u32 bytes);                                              /* zero-filled, 16-byte aligned; 0 = out of memory (64 blocks at most) */
    int   (*mem_free)(void *p);                                                 /* 1 = freed, 0 = not a block of this program */
    u32   (*mem_free_kb)(void);                                                 /* KB of RAM not yet handed out */
    /* files and folders: absolute paths of the root filesystem ("/home/docs/DOC.TXT", "/mnt/ide200/x"). Errors are NEGATIVE:
     * -3 not found, -2 disk full, -4 exists, -6 is a folder, -7 folder not empty, -8 bad name, -10 too big, -13 not allowed;
     * err_text turns one into a sentence. */
    int   (*file_size)(const char *path);                                       /* bytes, or an error */
    int   (*file_read)(const char *path, void *buf, u32 max);                   /* bytes read, or an error (-10 if it does not fit in max) */
    int   (*file_write)(const char *path, const void *data, u32 len);           /* 0 or an error; replaces the file */
    int   (*file_delete)(const char *path);                                     /* a file or an EMPTY folder */
    int   (*dir_make)(const char *path);
    int   (*dir_list)(const char *path, mw_dir_cb cb, void *ctx);               /* entries visited, or an error */
    const char *(*err_text)(int err);
    /* time */
    u32   (*ticks_ms)(void);                                                    /* milliseconds since the first call; call at least every 2 s */
    void  (*sleep_ms)(u32 ms);
    void  (*time_now)(mw_time_t *t);                                            /* the real-time clock, local to the hardware setting */
    /* mouse: the program keeps its own pointer (starts mid-screen); x/y are clamped to the screen; buttons: bit 0 left, bit 1 right */
    int   (*mouse_poll)(int *x, int *y, int *buttons);                          /* 1 if anything changed since the last call */
    /* drawing helpers */
    void  (*draw_icon16)(int x, int y, int icon);                               /* MWI_* */
    void  (*draw_icon12)(int x, int y, int icon);
    int   (*text_width)(const char *s);                                         /* pixels, Latin and Hangul alike */
    void  (*bevel)(int x, int y, int w, int h, int pressed);                    /* the raised (or sunken) 3-D box of buttons and panels */
    void  (*button)(int x, int y, int w, int h, const char *label, int pressed);
    void  (*window_frame)(int x, int y, int w, int h, const char *title, int icon);   /* shadow, body, title bar with icon (icon < 0: none) */
    void  (*log)(const char *s);                                                /* a line to the serial port (the host's debug log) */
    u32   (*rand32)(void);

    /* ---- version 3 ---- */
    u32   run_flags;                                                            /* MW_RUN_* bits, see below */
    void  (*set_title)(const char *title);                                      /* the window's title bar text (windowed programs; ignored full-screen) */
    const char *args;                                                           /* what followed the program name in `run NAME args...` ("" if nothing) */
    int   (*clipboard_get)(void *buf, u32 max);                                 /* copies up to max bytes; returns the clipboard's FULL length (more than max = cut short) */
    int   (*clipboard_set)(const void *data, u32 len);                          /* 1 = ok, 0 = too big (4096 bytes at most). Shared with Notepad's Edit menu */
    void  (*set_size)(int client_w, int client_h);                              /* windowed: resize the window so the drawing area is this size (clamped to the screen) */
    void  (*set_icon)(const u32 *argb16);                                       /* windowed: the window's icon, 16x16 pixels 0xAARRGGBB (copied); an icon packed into the file is used until then */
    u32   (*theme_color)(int id);                                               /* MW_TH_*: the current theme's colour, so a program follows the dark theme too */
    void  (*beep)(u32 hz, u32 ms);                                              /* the PC speaker (ms capped at 2000) */

    /* ---- version 4 ---- */
    /* A pop-up (right-click) menu at (x, y) of the window's drawing area. `items` is one string, the entries separated by '\n'; an entry may start
     * with '-' (a separator line above it), '!' (greyed out: it cannot be chosen) and '*' (bold: the default action), in any order. At most 14
     * entries. Returns the index of the chosen entry (0 = the first), or -1 if the menu was dismissed. It waits like present(): the desktop keeps
     * running while the menu is open. */
    int   (*menu_popup)(int x, int y, const char *items);

    /* The mouse pointer's shape while it is over this program's window (a MW_CURSOR_* value; anything else is ignored). It stays as set until
     * the program sets another, so a program that draws a pen over its canvas must set the arrow again when the pointer leaves it. Elsewhere
     * on screen the system chooses the shape itself (resize arrows on window edges, an I-beam over text fields, a hand over links, ...). */
    void  (*set_cursor)(int kind);
} mwp_syscalls_t;

/* theme_color ids */
enum { MW_TH_FACE, MW_TH_TEXT, MW_TH_ACCENT, MW_TH_ACCENT_TEXT, MW_TH_FIELD, MW_TH_FIELD_TEXT, MW_TH_LIGHT, MW_TH_SHADOW, MW_TH_OUTLINE, MW_TH_DESKTOP, MW_TH_WARNING };

#define MW_RUN_WINDOWED 1u        /* the program runs in a window: present/key_poll/sleep_ms hand the CPU back to the desktop, and the
                                   * drawing area is screen_w x screen_h, not the whole screen */

/* ABI 3 system call numbers (`int 0x80`, eax). Arguments in ebx, ecx, edx, esi, edi. Pointers are checked: a string must end inside memory the program
 * owns, a buffer must lie wholly inside it (and be writable for an output). A bad pointer gets -1 (or 0 where the call returns a pointer). */
enum {
    MW_SYS_EXIT = 0,        /* (code): never returns */
    MW_SYS_PRESENT = 1, MW_SYS_KEY_POLL = 2, MW_SYS_PUT_PIXEL = 3, MW_SYS_FILL_RECT = 4, MW_SYS_DRAW_RECT = 5,
    MW_SYS_DRAW_STRING = 6, MW_SYS_DRAW_STRING_KO = 7, MW_SYS_LOG = 8, MW_SYS_TICKS_MS = 9, MW_SYS_SLEEP_MS = 10,
    MW_SYS_SCREEN_W = 11, MW_SYS_SCREEN_H = 12, MW_SYS_MOUSE_POLL = 13 /* (int out[3]) */, MW_SYS_TIME_NOW = 14 /* (mw_time_t *) */,
    MW_SYS_RAND32 = 15, MW_SYS_THEME_COLOR = 16, MW_SYS_TEXT_WIDTH = 17, MW_SYS_BEVEL = 18, MW_SYS_BUTTON = 19 /* (int a[6]) */,
    MW_SYS_WINDOW_FRAME = 20 /* (int a[6]) */, MW_SYS_ICON16 = 21, MW_SYS_ICON12 = 22, MW_SYS_SET_TITLE = 23, MW_SYS_SET_SIZE = 24,
    MW_SYS_CLIP_GET = 25, MW_SYS_CLIP_SET = 26, MW_SYS_MEM_ALLOC = 27, MW_SYS_MEM_FREE = 28, MW_SYS_MEM_FREE_KB = 29,
    MW_SYS_FILE_SIZE = 30, MW_SYS_FILE_READ = 31, MW_SYS_FILE_WRITE = 32, MW_SYS_FILE_DELETE = 33, MW_SYS_DIR_MAKE = 34,
    MW_SYS_DIR_LIST = 35 /* (path, buf, size): fills buf with records, see mw_abi3.h; returns the entry count */,
    MW_SYS_ERR_TEXT = 36 /* (err, buf, size) */, MW_SYS_BEEP = 37, MW_SYS_ARGS = 38 /* (buf, size) */, MW_SYS_MENU_POPUP = 39,
    MW_SYS_SET_ICON = 40 /* (const u32 argb16[256]) */, MW_SYS_SET_CURSOR = 41,
    MW_SYS_RUN_FLAGS = 42 /* () -> MW_RUN_WINDOWED / MW_RUN_FULLSCREEN (a ring-3 program learns how it was started) */,
    MW_SYS_ABORT = 43 /* (reason): the program found it is corrupted and stops itself: 1 = stack smashing detected, 2 = invalid free */, MW_SYS_COUNT = 44
};
/* Mouse pointer shapes (set_cursor). */
enum { MW_CURSOR_ARROW = 0, MW_CURSOR_HELP, MW_CURSOR_HAND, MW_CURSOR_IBEAM, MW_CURSOR_WAIT, MW_CURSOR_WAIT_ARROW, MW_CURSOR_MOVE, MW_CURSOR_NO,
       MW_CURSOR_PEN, MW_CURSOR_HRESIZE, MW_CURSOR_VRESIZE, MW_CURSOR_DIAG_NWSE, MW_CURSOR_DIAG_NESW, MW_CURSOR_COUNT };
#define MWP_FLAG_RING3 8u           /* mwp trailer flag (tools/mwp_pack.py --ring3): run this program in ring 3 with ABI 3 */

#define MWP_SYSCALL_TABLE_ADDR 0x8F0000u      /* must equal kernel/memmap.h's MW_MWP_SYSCALL_ADDR (the kernel asserts it) */

#endif
