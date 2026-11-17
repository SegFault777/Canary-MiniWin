/* SPDX-License-Identifier: GPL-3.0-only */
/* paint.c -- the mouse API: hold the left button to draw, right button to erase, click a swatch to pick a colour,
 * and 'S' saves a quarter-size copy of the canvas (one colour index per byte, 100x70) to /PAINT.RAW with file_write.
 * Shows the error path too: when the disk has no room, err_text says why in the status line. ESC quits.
 * On a kernel with API version 4 the pointer becomes a pen over the canvas (set_cursor), and a right-click on a colour swatch opens a menu the
 * program builds itself (menu_popup): use that colour, fill the canvas with it, clear the canvas. */
#include "miniwin.h"

void mwp_entry(void) {
    if (!mw_has(2)) return;
    static const u32 pal[8] = { MW_BLACK, MW_RED, MW_GREEN, MW_BLUE, MW_YELLOW, MW_WHITE, MW_SHADOW, MW_ACCENT };
    int win = mw_has(3) && mw_windowed();
    int CW = 400, CH = 280, CX = ((int)sys.screen_w - CW) / 2, CY = win ? 8 : 50;
    if (win) { CW = (int)sys.screen_w - 16; CH = (int)sys.screen_h - 48; CX = 8; if (CW > 400) CW = 400; if (CH > 280) CH = 280; if (CW < 40) CW = 40; if (CH < 40) CH = 40; }   /* the canvas fits the window it opened in */
    u8 *px = sys.mem_alloc((u32)(400 * 280));              /* one colour index per canvas pixel: 112 KB from the kernel heap */
    if (!px) return;
    int v4 = mw_has(4);                                    /* set_cursor and menu_popup exist from API version 4 */
    int color = 0, mx = 0, my = 0, mb = 0, prev_mb = 0;
    char status[48]; status[0] = 0;
    mw_fill_screen(MW_FACE);
    for (int i = 0; i < CW * CH; i++) px[i] = 5;          /* index 5 = white */
    if (!win) sys.window_frame(CX - 8, CY - 24, CW + 16, CH + 60, "PAINT.MWP", MWI_NOTEPAD);
    sys.fill_rect(CX, CY, CW, CH, MW_WHITE);              /* after the frame: the frame paints its whole body grey */
    for (;;) {
        prev_mb = mb;
        sys.mouse_poll(&mx, &my, &mb);
        if (v4) sys.set_cursor(mw_in_rect(mx, my, CX, CY, CW, CH) ? MW_CURSOR_PEN : MW_CURSOR_ARROW);   /* a pen over the canvas, the arrow elsewhere */
        if (v4 && (mb & 2) && !(prev_mb & 2)) {              /* a right-click on a swatch: a menu of our own */
            for (int i = 0; i < 8; i++) if (mw_in_rect(mx, my, CX + i * 28, CY + CH + 6, 24, 18)) {
                int pick = sys.menu_popup(mx, my, "*Use this colour\nFill the canvas with it\n-Clear the canvas\n-!Paint (this entry is greyed out)");
                if (pick == 0) color = i;
                else if (pick == 1 || pick == 2) {
                    int fill = pick == 1 ? i : 5;
                    for (int k = 0; k < CW * CH; k++) px[k] = (u8)fill;
                    sys.fill_rect(CX, CY, CW, CH, pal[fill]);
                }
                break;
            }
        }
        if (mb && mw_in_rect(mx, my, CX, CY, CW, CH)) {
            u32 c = (mb & 2) ? MW_WHITE : pal[color];
            int cx = mx - CX, cy = my - CY;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                int x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= CW || y >= CH) continue;
                px[y * CW + x] = (mb & 2) ? 5 : (u8)color; sys.put_pixel(CX + x, CY + y, c);
            }
        } else if (mb & 1) {
            for (int i = 0; i < 8; i++) if (mw_in_rect(mx, my, CX + i * 28, CY + CH + 6, 24, 18)) color = i;
        }
        for (int i = 0; i < 8; i++) { sys.fill_rect(CX + i * 28, CY + CH + 6, 24, 18, pal[i]); sys.draw_rect(CX + i * 28 - (i == color), CY + CH + 6 - (i == color), 24 + 2 * (i == color), 18 + 2 * (i == color), MW_BLACK); }
        mw_text(CX + 232, CY + CH + 8, status, MW_BLACK);
        sys.present();
        int k = sys.key_poll();
        if (k == MW_KEY_ESC) break;
        if (k == 's' || k == 'S') {
            u8 *small = sys.mem_alloc(100 * 70);
            if (small) for (int y = 0; y < 70; y++) for (int x = 0; x < 100; x++) small[y * 100 + x] = px[(y * 4) * CW + x * 4];
            int e = small ? sys.file_write("/PAINT.RAW", small, 100 * 70) : -2;
            sys.mem_free(small);
            int n = 0; status[0] = 0;
            if (e == 0) mw_append(status, &n, "saved /PAINT.RAW"); else mw_append(status, &n, sys.err_text(e));
            sys.fill_rect(CX + 232, CY + CH + 4, 160, 20, MW_FACE);
        }
    }
    sys.mem_free(px);
}
