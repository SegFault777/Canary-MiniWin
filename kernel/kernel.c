/* SPDX-License-Identifier: GPL-3.0-only */
/* kernel.c -- MiniWin's kernel: pulls the layers together and runs the main loop.
 *
 * The whole kernel is ONE translation unit (every module is a header of `static` code), so the order of the
 * includes below IS the dependency order: a module may use anything included above it and nothing below.
 * That order is the architecture; docs/en/dev/ARCHITECTURE.md explains each layer and the rules between them.
 *
 *   hardware & protocols   io vga font keyboard mouse ata storage fs os_inst speaker rtc serial pci  nic rtl8139 e1000
 *   (flat headers in       net arp ip icmp udp dns dhcp tcp tls http https           net_stack mwp
 *    kernel/)              dom css layout render  <- the HTML5 engine used by the web app
 *   design                 ui/theme.h (+ ui/themes/*.h)  ui/widgets.h
 *   system                 sys/util  sys/lang  sys/power  sys/boot
 *   shell                  ui/icons  clock  window  taskbar  startmenu  desktop_files  resize  cursor  compose  input
 *   apps                   apps/notepad/*  apps/setting  apps/web/*  apps/terminal
 *
 * kmain() is only boot + loop: kernel_boot() (sys/boot.h), then forever { handle_mouse(); handle_keyboard();
 * poll the network and the web fetch; render_frame(); }.
 */
#include "version.h"
#include "io.h"
#include "vga.h"
#include "font.h"
#include "keyboard.h"
#include "mouse.h"
#include "ata.h"
#include "storage.h"
#include "fs.h"
#include "os_inst.h"
#include "font_ko.h"
#include "hangul_ime.h"
#include "speaker.h"
#include "rtc.h"
#include "worldmap_data.h"   /* the 256x102 1-bit world map of the Time Zone tab (tools/gen_worldmap.py) */
#include "tz.h"            /* time zones: the list, minute offsets, daylight-saving rules; tz_local() */
#include "serial.h"
#include "fatal.h"
#include "sysinfo.h"
#include "pci.h"
#include "usb/core.h"
#include "nic.h"
#include "rtl8139.h"
#include "e1000.h"
#include "net.h"
#include "arp.h"
#include "ip.h"
#include "icmp.h"
#include "udp.h"
#include "dns.h"
#include "dhcp.h"
#include "tcp.h"
#include "tls.h"
#include "http.h"
#include "https.h"
#include "render.h"        /* the HTML5 engine: dom.h + css.h + layout.h + painter */
#include "net_stack.h"
#include "mwp.h"

#include "clipboard.h"       /* the OS clipboard (Notepad's Edit menu, programs' sys.clipboard_*) */
#include "mm.h"              /* page allocator + heap (kmalloc/kfree); mm_init_boot() runs from boot.h */
#include "js_dom.h"          /* MiniWeb's JavaScript engine (js.h) and its DOM glue; needs kmalloc, dom.h, render.h, netclock.h */
#include "live.h"            /* the live disc: a RAM copy of the El Torito boot image becomes the boot device (needs the heap) */

#include "paging.h"          /* page tables, guard pages, the double-fault task (needs mm.h, fatal.h's IDT, memmap.h) */
#include "mwp_meta.h"        /* the icon/title packed into a program file (needs fs.h + mm.h) */

/* ---- design: color roles + metrics, and every reusable piece of chrome ---- */
#include "ui/theme.h"      /* pick a look with  THEME=name ./build.sh  (kernel/ui/themes/<name>.h) */
#include "ui/icon16.h"      /* shared 16x16 icons (needs fs.h's catalog + vga.h) */
#include "ui/widgets.h"

/* The desktop colours are variables since alpha-12 (Settings > Display picks a preset; the theme only supplies the first value). The
 * caption colour follows the background so the icon names stay readable on any preset. */
static u32 desktop_bg = TH_DESKTOP;
static u32 desktop_fg = TH_DESKTOP_TEXT;
#define DESKTOP_COLOR_BG      (desktop_bg)
#define DESKTOP_COLOR_ICON_BG (desktop_bg)

/* ---- system ---- */
#include "sys/util.h"
#include "sys/lang.h"

/* ---- shell + apps, in dependency order (Notepad's pieces are interleaved with the shell because the shell
 *      draws Notepad's pills and the Notepad menus draw over the shell) ---- */
#include "ui/icons.h"
#include "ui/clock.h"
#include "ui/taskbar_metrics.h"
#include "ui/window.h"
#include "ui/menubar.h"
#include "ui/scrollbar.h"
#include "ui/textfield.h"  /* the one-line text field: caret, selection, kerning, clipboard (MiniFiles rename, address bar, command line, Save As) */
#include "ui/controls.h"   /* drop-down lists and check boxes (the list uses the standard scroll bar of ui/scrollbar.h) */
#include "ui/sheet.h"       /* property-sheet frame (tabs, page, OK/Cancel/Apply) and 32x32 icons by name; needs controls.h and the icon catalog */
#include "ui/listbox.h"     /* a list box with the standard scroll bar */
#include "apps/notepad/windows.h"
#include "apps/notepad/file_menu.h"
#include "apps/notepad/save.h"
#include "apps/notepad/edit.h"    /* Notepad's text cursor and selection: positions, insert/delete, the mouse */
#include "apps/notepad/menus.h"   /* Notepad's File / Edit / Help menus (data + what each item does) */
#include "ui/taskbar.h"
#include "ui/startmenu.h"
#include "apps/notepad/draw.h"
#include "ui/cursor.h"   /* the pointer shapes and trails: Settings > Mouse draws them, so it comes before it */
#include "fontpack.h"        /* the font pack reader: Galmuri11 (and later the other faces) from the system area of the disk */
#include "ttf.h"             /* the TrueType rasterizer: a .ttf becomes a face */
#include "fonttf.h"          /* the installed TrueType fonts and the building of their faces (Control Panel > Font > Install New Font) */
#include "fontface.h"        /* switching the system font face at run time (Control Panel > Font) */
#include "apps/setting.h"
#include "apps/web/state.h"
#include "apps/web/address.h"
#include "apps/web/fetch.h"
#include "apps/web/page.h"
#include "apps/web/autotest.h"
#include "apps/web/view.h"
#include "apps/terminal.h"
#include "apps/minifiles.h"
#include "syscfg.h"            /* /etc/SYSTEM.CFG (language, input methods, desktop colour, time zone, user name) and the user's password */
#include "apps/oobe.h"          /* the first-run setup and the logon prompt */
#include "apps/notepad/dialogs.h"
#include "ui/dialog.h"          /* dialogs are windows: Notepad's questions and MiniWeb's About box (needs notepad/dialogs.h, web/view.h) */
#include "ui/scroll_targets.h"
#include "ui/desktop_files.h"
#include "ui/resize.h"
#include "coro.h"            /* mw_coro_switch: the stack switch under programs in windows */
#include "apps/progwin.h"    /* `run` in a window (needs windows.h's ids and the widgets; mwp_api2.h below hooks into it) */
#include "ui/ctxmenu.h"       /* the right-click menu: state, drawing, hit-testing (actions: ui/ctxmenu_actions.h, below) */
#include "ui/clock_popup.h"  /* the taskbar clock's popup: analog clock + calendar (needs apps/setting_dt.h) */
#include "ui/compose.h"
#include "mwp_api2.h"       /* the program syscall table's version-2 functions (needs mm.h, vfs.h, icons, widgets, MiniFiles) */
#include "sys/power.h"
#include "apps/installer.h"  /* the live disc's INSTALL window (needs progwin.h, power.h, terminal.h, os_inst.h) */
#include "apps/startmenu_add.h"  /* start-menu shortcuts and the Add... wizard (needs installer.h's helpers, progwin.h, vfs.h) */
#include "sys/boot.h"
#include "ring3.h"           /* programs in ring 3: the int 0x80 gate, user pages, the syscall dispatcher (needs paging.h, progwin.h, mwp_api2.h and the menu actions below) */
#include "ui/ctxmenu_actions.h"  /* what the right-click menus hold and do (needs MiniFiles, the Start-menu wizard, the desktop icons) */
#include "ui/input.h"
#include "ui/cursor_ctx.h"   /* which pointer shape fits where the pointer is (needs the input code and every window's geometry) */

void kmain(void) {
    for (volatile u32 *z = (volatile u32 *)MW_BSSX_BASE; z < (volatile u32 *)MW_BSSX_END; z++) *z = 0;   /* the buffers evicted from .bss (memmap.h) used to be born zeroed -- and some keep state, so this is BEFORE the boot */
    kernel_boot();
    mx = VGA_WIDTH / 2; my = VGA_HEIGHT / 2;       /* the pointer starts in the middle of the screen the loader gave us */

    render_frame(mx, my, status);

    while (1) {
        u32 pass_start = sysinfo_tsc32();          /* for `fetch`'s CPU load: work vs the pacing spin below */
        tick++;

        usb_poll();                                /* USB reports -> the same key/mouse state PS/2 fills */
        handle_mouse();
        handle_keyboard();

        net_stack_poll(); /* drains and dispatches any received frames: ARP, IP/ICMP/UDP/TCP, DHCP */
        term_install_poll(); /* advances a running `os-inst` a few sectors (kernel/os_inst.h) */

        /* Advances whatever MiniWeb (MINIWEB.MWP) currently has outstanding
         * -- a DNS lookup, an HTTP fetch, or nothing at all. The app
         * itself is the network stack's real verification now (see
         * draw_web_window()/web_go() above): it drove a real DNS
         * resolution, a full TCP 3-way handshake, an HTTP/1.1 request,
         * and a genuine multi-header response from pypi.org during this
         * kernel's own development, all reachable by clicking an icon
         * rather than reading a serial log. web_poll() itself no-ops
         * once everything's settled (DONE/FAILED on either the DNS or
         * HTTP side) until the next click on a site row calls
         * web_go() again. */
        web_poll();
        { const char *pn = pw_step(mx, my, mouse_left, mouse_right);    /* the program in the window, if any, runs until it yields (apps/progwin.h) */
          if (pn) status = pn; }
#ifdef MW_AUTOTEST_URL
        mw_autotest_step();
#endif

        render_frame(mx, my, status);
        u32 work_end = sysinfo_tsc32();
        delay(2000); /* lowered further from 8000 -- mouse felt sluggish/
                      * capped at 8000; this loop's real-world speed varies
                      * a lot by CPU, so double_click_window above is
                      * scaled proportionally to keep the same real-time
                      * double-click window. */
        sysinfo_account(work_end - pass_start, sysinfo_tsc32() - work_end);
    }
}
