/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_APPS_NOTEPAD_DRAW_H
#define MW_APPS_NOTEPAD_DRAW_H

/* apps/notepad/draw.h -- drawing a Notepad window (title bar, menu bar, text area, caret).
 * (Split out of kernel.c in pre-29; it is still one translation unit, so modules are included by
 * kernel.c in dependency order -- see the module map there.) */

/* ============================================================
 * Notepad window drawing
 * ============================================================ */
static void draw_titlebar_buttons(void) {
    int id = active_np->id;
    ui_titlebar_buttons(btn_min_x(), btn_max_x(), btn_close_x(), btn_y(),
                        pressed_btn_kind == BTN_MIN && pressed_btn_win == id,
                        pressed_btn_kind == BTN_MAX && pressed_btn_win == id,
                        pressed_btn_kind == BTN_CLOSE && pressed_btn_win == id);
}

static void draw_window(void) {
    int wx = active_np->win.x, wy = active_np->win.y, ww = active_np->win.w, wh = active_np->win.h;

    /* shadow (not when maximized), body, outline, title bar */
    ui_window_frame_ic(wx, wy, ww, wh, active_np->win.maximized, "NOTEPAD.MWP", IC_NOTEPAD);

    draw_titlebar_buttons();

    /* menu bar: File / Edit / Help (apps/notepad/menus.h; the open drop-down is drawn last, see render_frame) */
    mb_draw_bar(active_np->id, wx, wy, ww, np_menus, NP_MENU_N);

    /* text edit area (white, sunken border) */
    int ex = edit_x(), ey = edit_y(), ew = edit_w(), eh = edit_h();
    ui_well(ex, ey, ew, eh, TH_FIELD);

    /* The text, wrapped and scrollable. The wrapping rules live in np_rows_ex() (apps/notepad/save.h) and are repeated
     * here character for character; a row is drawn only if it is inside the window of rows the scroll bar shows.
     * UTF-8 aware: a 3-byte sequence (0xE0 lead byte) is one Hangul syllable/jamo drawn via the Korean font and advances one
     * FONT_CELL; a Latin letter advances its kerned width (np_adv() in save.h; one FONT_CELL with Kerning off). */
    int vis_rows = np_visible_rows(eh), limit = np_wrap_limit(ew), end_rel = 0;
    int total_rows = np_rows_ex(active_np, ew, &end_rel);
    np_clamp();
    int crow, crel;
    np_pos_of(active_np->caret, &crow, &crel);                         /* where the caret is: the view follows it when it (or the text) changed */
    if (active_np->scroll_len != active_np->text_len || active_np->scroll_caret != active_np->caret) {
        active_np->scroll_len = active_np->text_len; active_np->scroll_caret = active_np->caret;
        if (crow < active_np->scroll) active_np->scroll = crow;
        else if (crow >= active_np->scroll + vis_rows) active_np->scroll = crow - vis_rows + 1;
    }
    if (active_np->scroll > total_rows - vis_rows) active_np->scroll = total_rows > vis_rows ? total_rows - vis_rows : 0;
    if (active_np->scroll < 0) active_np->scroll = 0;
    int first = active_np->scroll, x0 = ex + 2;
    u32 slo = np_sel_lo(), shi = np_sel_hi();                            /* the selection [slo, shi): drawn highlighted */
    int rel = 0, row = 0;
    for (u32 i = 0; i < active_np->text_len; ) {
        int clen = ko_utf8_char_len((unsigned char)active_np->text_buf[i]);
        int in_sel = i >= slo && i < shi;
        u32 fg = in_sel ? TH_TEXT_INVERSE : TH_FIELD_TEXT;
        if (clen == 3 && i + 3 <= active_np->text_len) {
            if (rel > limit) { rel = 0; row++; }
            if (row >= first && row < first + vis_rows) {
                int cp = ko_utf8_decode3(&active_np->text_buf[i]);
                if (in_sel) bb_fillrect(x0 + rel, ey + 2 + (row - first) * FONT_LINE_H, FONT_CELL, FONT_LINE_H, TH_ACCENT);
                ko_font_draw_codepoint(x0 + rel, ey + 2 + (row - first) * FONT_LINE_H, cp, fg);
            }
            rel += FONT_CELL;
            i += 3;
            continue;
        }
        char c = active_np->text_buf[i];
        if (c == '\n' || rel > limit) {
            if (c == '\n' && in_sel && row >= first && row < first + vis_rows)         /* a selected line break shows as a half-cell block */
                bb_fillrect(x0 + rel, ey + 2 + (row - first) * FONT_LINE_H, FONT_CELL / 2, FONT_LINE_H, TH_ACCENT);
            rel = 0; row++;
            if (c == '\n') { i++; continue; }
        }
        int adv = np_adv(active_np->text_buf, active_np->text_len, i, 0);                  /* kerned advance (FONT_CELL with Kerning off) */
        if (row >= first && row < first + vis_rows) {
            if (in_sel) bb_fillrect(x0 + rel, ey + 2 + (row - first) * FONT_LINE_H, adv, FONT_LINE_H, TH_ACCENT);
            if (font_kerning) font_draw_char_k(x0 + rel, ey + 2 + (row - first) * FONT_LINE_H, c, fg);
            else font_draw_char(x0 + rel, ey + 2 + (row - first) * FONT_LINE_H, c, fg);
        }
        rel += adv;
        i++;
    }
    int cx = x0 + crel, cy = ey + 2 + (crow - first) * FONT_LINE_H;

    /* Live preview of the syllable currently being composed (Hangul mode), shown at the caret before it is committed to the text. */
    if (current_ime == IME_KOREAN && ko_ime_is_composing()) {
        int preview_cp = ko_ime_preview_codepoint();
        if (preview_cp >= 0 && crow >= first && crow < first + vis_rows) {
            ko_font_draw_codepoint(cx, cy, preview_cp, TH_FIELD_TEXT);
            cx += FONT_CELL;
        }
    }

    if (crow >= first && crow < first + vis_rows) bb_fillrect(cx, cy, 2, FONT_CELL, TH_FIELD_TEXT); /* text cursor */
    sb_draw(active_np->id);                                                                   /* the scroll bar (ui/scrollbar.h) */
}

/* Is (px,py) over the draggable part of the title bar -- i.e. the title
 * bar itself, but not over any of the three control buttons? */
static int titlebar_drag_hit(int px, int py) {
    if (!in_rect(px, py, active_np->win.x + 1, active_np->win.y + 1, active_np->win.w - 2, TITLEBAR_H)) return 0;
    if (in_rect(px, py, btn_min_x(), btn_y(), BTN_W, BTN_H)) return 0;
    if (in_rect(px, py, btn_max_x(), btn_y(), BTN_W, BTN_H)) return 0;
    if (in_rect(px, py, btn_close_x(), btn_y(), BTN_W, BTN_H)) return 0;
    return 1;
}

/* Generic maximize/restore -- works on any window_t, so both Notepad and
 * SETTING.MWP's window can share one implementation instead of two
 * copies of the same four assignments. */
static void maximize_window(window_t *w) {
    if (w->maximized) return;
    w->restore_x = w->x;
    w->restore_y = w->y;
    w->restore_w = w->w;
    w->restore_h = w->h;
    w->x = MAXIMIZED_X;
    w->y = MAXIMIZED_Y;
    w->w = MAXIMIZED_W;
    w->h = MAXIMIZED_H;
    w->maximized = 1;
}

static void unmaximize_window(window_t *w) {
    if (!w->maximized) return;
    w->x = w->restore_x;
    w->y = w->restore_y;
    w->w = w->restore_w;
    w->h = w->restore_h;
    w->maximized = 0;
}

#endif
