/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_APPS_NOTEPAD_DIALOGS_H
#define MW_APPS_NOTEPAD_DIALOGS_H

/* apps/notepad/dialogs.h -- Notepad's File dropdown and the save/new confirm dialog (drawing).
 * (Split out of kernel.c in pre-29; it is still one translation unit, so modules are included by
 * kernel.c in dependency order -- see the module map there.) */

/* ============================================================
 * File menu dropdown + Save/New confirm dialog drawing
 * ============================================================ */
static void draw_file_menu(int mx, int my) {
    int x = file_menu_x();
    int y = file_menu_top_y();
    int w = FILE_MENU_W;
    int h = FILE_MENU_ITEM_H * 3;

    ui_panel(x, y, w, h, TH_MENU_SHADOW);

    const char *labels[3] = { t(STR_SAVE_AS), t(STR_SAVE), t(STR_NEW) };
    for (int i = 0; i < 3; i++) {
        int iy = y + i * FILE_MENU_ITEM_H;
        int hover = in_rect(mx, my, x, iy, w, FILE_MENU_ITEM_H);
        u32 fg = ui_item(x + 1, iy, w - 2, FILE_MENU_ITEM_H, hover);
        ko_draw_mixed_string(x + 4, iy + 1, labels[i], fg);
    }
}

/* A small yellow warning triangle with a black "!" inside -- built out
 * of scanlines and a couple of fillrects, since one hand-drawn icon
 * doesn't justify writing a general polygon rasterizer. Sits to the
 * left of the confirm dialog's message so "Save changes?" actually
 * looks like it's asking something, not just stating it. */
static void draw_warning_icon(int x, int y) {
    int h = 13;
    for (int row = 0; row < h; row++) {
        int half = (row * 6) / (h - 1); /* 0..6: widens going down */
        bb_fillrect(x + 6 - half, y + row, half * 2 + 1, 1, TH_WARNING);
        bb_putpixel(x + 6 - half, y + row, TH_WARNING_INK);
        bb_putpixel(x + 6 + half, y + row, TH_WARNING_INK);
    }
    for (int i = 0; i <= 12; i++) bb_putpixel(x + i, y + h - 1, TH_WARNING_INK);
    bb_fillrect(x + 5, y + 3, 2, 5, TH_WARNING_INK); /* the "!" stem */
    bb_fillrect(x + 5, y + 9, 2, 2, TH_WARNING_INK); /* the "!" dot */
}

static void saveas_sync(void) { active_np->saveas_len = (int)tf_len(active_np->saveas_name); }      /* tf_key() calls this after every change */
/* The Save As name box (where draw_confirm_dialog() puts it): a click in it moves the caret. */
static int saveas_field_hit(int px, int py) {
    return in_rect(px, py, confirm_x() + 10, confirm_y() + TITLEBAR_H + 3 + 14, CONFIRM_W - 20, 14);
}
/* The contents of Notepad's question window: the frame, the title bar and the _ [] X buttons are drawn by the dialog's window (ui/dialog.h,
 * dlg_draw); this draws what is inside it, measured from the dialog's own corner (confirm_x/y). */
static void draw_confirm_dialog(int mx, int my) {
    int x = confirm_x(), y = confirm_y();
    int info = (active_np->confirm_mode == CONFIRM_INFO);

    int body_y = y + TITLEBAR_H + 3;
    if (info) {                                                              /* Help > Keys / About: plain lines and an OK button */
        static const char *const keys[5] = { "Ctrl+S  Save", "Ctrl+N  New", "Ctrl+W  Close", "Ctrl+A  Select", "R-Alt   Han/Eng" };
        static const char *const about[5] = { "NOTEPAD.MWP", "MiniWin " MW_VERSION, "Documents up to 4 KB", "Types at the end;", "Edit works on all text" };
        const char *const *ln = active_np->info_kind == 1 ? keys : about;
        for (int i = 0; i < 5; i++) font_draw_string(x + 12, body_y + 1 + i * 12, ln[i], TH_TEXT);
    } else if (active_np->confirm_mode == CONFIRM_SAVEAS) {
        /* Save As: the same frame, but a file-name field instead of the question. */
        ko_draw_mixed_string(x + 10, body_y + 1, t(STR_FILE_NAME), TH_TEXT);
        int fx = x + 10, fy = body_y + 14, fw = CONFIRM_W - 20;
        ui_field(fx, fy, fw, 14, TH_FIELD, 1);
        tf_draw(&active_np->saveas_tf, active_np->saveas_name, TF_NAME, fx + 3, fy + 2, fw - 6, 0, 1, TH_FIELD_TEXT);   /* kerned text, selection, caret */
    } else {
    if (ic16_ready(IC_WARNING)) ic16_blit(x + 6, body_y, IC_WARNING);     /* the pack's own warning sign ... */
    else draw_warning_icon(x + 8, body_y);                                 /* ... or the hand-drawn one when the image has no icon pack */
    ko_draw_mixed_string(x + 26, body_y + 1,  t(STR_SAVE_CHANGES), TH_TEXT);
    if (active_np->confirm_mode == CONFIRM_CLOSE) {
        ko_draw_mixed_string(x + 26, body_y + 13, t(STR_BEFORE_CLOSING), TH_TEXT);
    } else {
        ko_draw_mixed_string(x + 26, body_y + 13, t(STR_BEFORE_NEW), TH_TEXT);
    }
    }

    int yb = confirm_btn_y();
    int yesx = info ? x + (CONFIRM_W - CONFIRM_BTN_W) / 2 : confirm_yes_x(), nox = confirm_no_x();
    int yes_hover = in_rect(mx, my, yesx, yb, CONFIRM_BTN_W, CONFIRM_BTN_H);
    int no_hover  = in_rect(mx, my, nox,  yb, CONFIRM_BTN_W, CONFIRM_BTN_H);

    /* Yes/No labels are centered in their buttons via ko_string_width()
     * rather than hardcoded offsets, since "아니오" isn't the same pixel
     * width as "No" -- can't just reuse the English magic numbers. */
    const char *yes_label = info ? "OK" : t(active_np->confirm_mode == CONFIRM_SAVEAS ? STR_SAVE : STR_YES);
    const char *no_label = t(active_np->confirm_mode == CONFIRM_SAVEAS ? STR_CANCEL : STR_NO);
    int yes_tx = yesx + (CONFIRM_BTN_W - ko_string_width(yes_label)) / 2;
    int no_tx  = nox  + (CONFIRM_BTN_W - ko_string_width(no_label)) / 2;

    u32 yes_fg = ui_button_flat(yesx, yb, CONFIRM_BTN_W, CONFIRM_BTN_H, yes_hover);
    ko_draw_mixed_string(yes_tx, yb + 2, yes_label, yes_fg);

    if (!info) {
        u32 no_fg = ui_button_flat(nox, yb, CONFIRM_BTN_W, CONFIRM_BTN_H, no_hover);
        ko_draw_mixed_string(no_tx, yb + 2, no_label, no_fg);
    }
}

#endif
