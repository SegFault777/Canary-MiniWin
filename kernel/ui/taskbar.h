/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_UI_TASKBAR_H
#define MW_UI_TASKBAR_H

/* ui/taskbar.h -- taskbar drawing and hit-testing (pills, glyphs, Start button, clock slot).
 * (Split out of kernel.c in pre-29; it is still one translation unit, so modules are included by
 * kernel.c in dependency order -- see the module map there.) */

/* ============================================================
 * Taskbar drawing + hit-testing  (1.1-alpha-14: the Windows 95 way)
 * ============================================================ */
/* EVERY open window has a button on the taskbar, in the order the windows were opened (a button keeps its place when its window is
 * minimized and restored). Buttons are plain buttons -- icon and title, no minimise / restore / close glyphs on them:
 *   - the window in front (open, not minimized) is the PRESSED one: sunken, with the dithered face of a pressed 95 button;
 *   - clicking the pressed button minimizes its window (the button pops up);
 *   - clicking a button whose window is minimized restores it and brings it to the front (it is now the pressed one);
 *   - clicking the button of a window that is open but behind another brings it to the front -- the pressed look moves with it:
 *     the button that was pressed pops up, this one goes down.
 * The buttons shrink to fit as more windows pile up, the same way real taskbars do. */
#define TASKBTN_GAP    2
#define TASKBTN_MAXW   124
#define TASKBTN_MINW   26

static u32 tb_seq[WIN_SLOTS];             /* when each window's button first appeared (0 = no button); indexed by win_slot(id) */
static u32 tb_next = 1;

/* Single source of truth for button layout, shared by draw_taskbar() and every taskbar hit-test below -- computing it twice with two
 * different formulas would be exactly how these things quietly drift out of sync. */
static void taskbar_layout(int *ids_out, int *count_out, int *pill_w_out) {
    int n = 0;
    for (int s = 0; s < WIN_SLOTS; s++) {                 /* the windows, then the dialogs (a dialog is a window with a button of its own) */
        int id = win_id_at(s);
        if (win_is_open(id)) {
            if (tb_seq[s] == 0) tb_seq[s] = tb_next++;
            ids_out[n++] = id;
        } else {
            tb_seq[s] = 0;
        }
    }
    /* insertion sort by tb_seq ascending -- earliest-opened first. n is at most WIN_SLOTS, so this is plenty fast. */
    for (int i = 1; i < n; i++) {
        int key = ids_out[i];
        u32 kseq = tb_seq[win_slot(key)];
        int j = i - 1;
        while (j >= 0 && tb_seq[win_slot(ids_out[j])] > kseq) {
            ids_out[j + 1] = ids_out[j];
            j--;
        }
        ids_out[j + 1] = key;
    }
    int avail_w = VGA_WIDTH - TASKBTN_X - 4 - CLOCK_W - 4; /* leave room for the clock */
    int pw = (n > 0) ? (avail_w - (n - 1) * TASKBTN_GAP) / n : TASKBTN_MAXW;
    if (pw > TASKBTN_MAXW) pw = TASKBTN_MAXW;
    if (pw < TASKBTN_MINW) pw = TASKBTN_MINW;
    *count_out = n;
    *pill_w_out = pw;
}

/* The window whose button is pressed: the front-most one that is open and not minimized (-1 if every window is minimized or closed). */
static int taskbar_active_id(void) {
    for (int i = z_count - 1; i >= 0; i--) {
        int id = z_order[i];
        if (win_is_open(id) && !win_is_minimized(id)) return id;
    }
    return -1;
}

/* A window's title as its button shows it (the same words as its title bar). */
static void taskbar_title(int id, char *out, u32 outsz) {
    const char *s = "NOTEPAD.MWP";
    if (id == WIN_ID_SETTING) s = "SETTING.MWP";
    else if (id == WIN_ID_WEB) s = "MINIWEB.MWP";
    else if (id == WIN_ID_TERMINAL) s = "MINISHELL.MWP";
    else if (id == WIN_ID_FILES) s = "MINIFILES.MWP";
    else if (PW_IS(id)) { pw_select(id); s = pw_title(); }
    else if (DLG_IS(id)) s = dlg_title(id - WIN_ID_DLG);
    u32 len = 0;
    out[0] = 0;
    while (*s && len + 1 < outsz) kstrcpy_append(out, &len, outsz, *s++);
}

/* Which icon a window shows on its taskbar button. */
static int taskbar_pill_icon(int id) {
    if (id == WIN_ID_SETTING) return IC_SETTING;
    if (id == WIN_ID_WEB) return IC_WEB;
    if (id == WIN_ID_TERMINAL) return IC_TERM;
    if (id == WIN_ID_FILES) return IC_FILES;
    if (PW_IS(id)) { pw_select(id); return pw_icon_id(); }
    if (DLG_IS(id)) return dlg_icon(id - WIN_ID_DLG);
    return IC_NOTEPAD;
}

static void draw_taskbar(void) {
    bb_fillrect(0, TASKBAR_Y, VGA_WIDTH, TASKBAR_H, TH_FACE);
    ui_hline(0, TASKBAR_Y, VGA_WIDTH, TH_LIGHT);

    /* Start button. The classic "raised" 3D bevel is just a light stripe on the top/left edges and a dark stripe on the bottom/right --
     * and flipping which stripe goes where makes it look "pressed in" while the menu is open. This exact four-line trick single-handedly
     * carried the entire aesthetic of 16-bit UI toolkits. We salute it. */
    ui_bevel(STARTBTN_X, STARTBTN_Y, STARTBTN_W, STARTBTN_H, start_menu_open);
    /* nudge the label a pixel down-right while "pressed", like it's physically sinking into the taskbar under the weight of your click */
    int press = start_menu_open ? 1 : 0;
    ic16_blit12(STARTBTN_X + 3 + press, STARTBTN_Y + 1 + press, IC_MINIWIN);       /* the MiniWin logo, shrunk to fit the 14-pixel button */
    font_draw_k(STARTBTN_X + 3 + 12 + 3 + press, STARTBTN_Y + 2 + press, "AM", TH_TEXT);
    /* the grip after the Start button: a raised double line, like the one that sets the quick-launch area apart in a 95 taskbar */
    ui_vline(STARTBTN_X + STARTBTN_W + 2, STARTBTN_Y, STARTBTN_H, TH_SHADOW);
    ui_vline(STARTBTN_X + STARTBTN_W + 3, STARTBTN_Y, STARTBTN_H, TH_LIGHT);

    int ids[WIN_SLOTS], count, pill_w;
    taskbar_layout(ids, &count, &pill_w);
    int active = taskbar_active_id();
    for (int i = 0; i < count; i++) {
        int id = ids[i];
        int px = TASKBTN_X + i * (pill_w + TASKBTN_GAP);
        int py = TASKBTN_Y;
        int down = (id == active);

        ui_bevel(px, py, pill_w, TASKBTN_H, down);
        if (down) {                                           /* the dithered face of a pressed button: every other pixel lighter */
            for (int yy = py + 2; yy < py + TASKBTN_H - 1; yy++)
                for (int xx = px + 2; xx < px + pill_w - 1; xx++)
                    if (((xx + yy) & 1) == 0) bb_putpixel(xx, yy, TH_LIGHT);
        }
        int o = down ? 1 : 0;                                 /* a pressed button's contents sink a pixel, like the Start button's */
        ic16_blit12(px + 3 + o, py + 1 + o, taskbar_pill_icon(id));
        if (pill_w >= 44) {
            char label[40];
            taskbar_title(id, label, sizeof(label));
            int room = pill_w - 3 - 14 - 3;                   /* what is left of the button after the icon and its margins */
            int n = 0; while (label[n]) n++;
            while (n > 0 && font_width_k(label) > room) label[--n] = 0;
            font_draw_k(px + 3 + 14 + o, py + 2 + o, label, TH_TEXT);
        }
    }

    /* Clock, bottom-right corner. A sunken (rather than raised) look -- opposite bevel from the Start button -- since it's a readout, not
     * a button; pressed-looking while its popup is open, same "flip which edge is light" trick as everywhere else in this UI. */
    ui_readout(CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H, clock_popup_open);
    {
        rtc_time_t now;
        rtc_read(&now);
        rtc_time_t local = tz_local(now);
        char clock_str[24];
        format_clock_time(&local, clock_str, sizeof(clock_str));
        int tw = ko_string_width(clock_str);
        draw_tray_speaker(CLOCK_X + 4, CLOCK_Y + (CLOCK_H - 8) / 2);                    /* the notification area: speaker, then the time */
        ko_draw_mixed_string(CLOCK_X + CLOCK_W - tw - 5, CLOCK_Y + 1, clock_str, TH_TEXT);
    }
}

static int start_button_hit(int px, int py) {
    return in_rect(px, py, STARTBTN_X, STARTBTN_Y, STARTBTN_W, STARTBTN_H);
}

/* The id of the window whose taskbar button contains (px,py), using the exact same layout draw_taskbar() just drew -- or -1. */
static int taskbar_button_hit(int px, int py) {
    int ids[WIN_SLOTS], count, pill_w;
    taskbar_layout(ids, &count, &pill_w);
    for (int i = 0; i < count; i++) {
        int bx = TASKBTN_X + i * (pill_w + TASKBTN_GAP);
        if (in_rect(px, py, bx, TASKBTN_Y, pill_w, TASKBTN_H)) return ids[i];
    }
    return -1;
}

#endif
