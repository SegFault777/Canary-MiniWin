/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_UI_DIALOG_H
#define MW_UI_DIALOG_H

/* ui/dialog.h -- dialogs that are windows (1.1-alpha-14).
 *
 * Notepad's "Save changes?", Save As and Help boxes and MiniWeb's About box used to be drawn INSIDE their owner's window and swallow every
 * click and key until answered. They are real windows now. Each has a slot in dlg_wins[] (ui/window.h) and a window id from WIN_ID_DLG up
 * (apps/notepad/windows.h), so everything that works for any other window works for them:
 *   - a title bar you drag, and the _ [] X buttons (maximized, the contents stay their size and sit in the middle of the screen);
 *   - a place in the z-order: clicking one brings it to the front, and it never falls behind its owner;
 *   - a button on the taskbar, with the dialog's title and icon; minimize it and the button brings it back.
 * What makes it a DIALOG is the owner: while a question is open its owner window is disabled -- a click on it only brings the question
 * forward (dlg_activate), keys go to the question, nothing else in the owner reacts. Other windows stay fully usable.
 *
 * The questions themselves still live in their owners (notepad_t.confirm_mode, web_about_open): dlg_poll() looks at them once per frame and
 * opens or closes the window to match, so none of the many places that start or answer a question needed to change.
 * The contents are drawn and hit-tested by the owners' own code (apps/notepad/dialogs.h, apps/web/view.h), measured from dlg_ox/dlg_oy. */

static int dlg_kind_seen[DLG_MAX];                 /* what the window was last sized for (a change of question may change the size) */

/* Does this dialog have a question open right now? (The questions live where they always did: in their owners.) */
static int dlg_want(int slot) {
    if (slot < NOTEPAD_MAX) return notepads[slot].win.open && notepads[slot].confirm_mode != CONFIRM_NONE;
    switch (slot) {
    case DLG_WEB:     return web_win.open && web_about_open;
    case DLG_DISP:    return disp_confirm_open;
    case DLG_DPASK:   return setting.open && setting_page == SP_DISPLAY && dp_ask;
    case DLG_FNTASK:  return setting.open && setting_page == SP_TEXT && fnt_warn >= 0 && !afd_on;
    case DLG_ADDFONT: return setting.open && setting_page == SP_TEXT && afd_on && fnt_view == 0;
    }
    return 0;
}
/* A number per look of the dialog: the window is resized when it changes. */
static int dlg_kind_of(int slot) {
    if (slot < NOTEPAD_MAX) return notepads[slot].confirm_mode == CONFIRM_INFO ? 10 + notepads[slot].info_kind : notepads[slot].confirm_mode;
    if (slot == DLG_DISP) return disp_unavail ? 1 : 0;
    return 100 + slot;
}
/* The window around the contents: the contents' own size plus the title bar's room above them is already inside it (every dialog's
 * contents measure from the dialog's top-left corner, title bar included). 0 = it cannot be shown at all (Add Fonts on a tiny screen). */
static int dlg_size_of(int slot, int *w, int *h) {
    if (slot < NOTEPAD_MAX) { *w = CONFIRM_W; *h = notepads[slot].confirm_mode == CONFIRM_INFO ? CONFIRM_INFO_H : CONFIRM_H; return 1; }
    switch (slot) {
    case DLG_WEB:     *w = WEB_ABOUT_W; *h = WEB_ABOUT_H; return 1;
    case DLG_DISP:    *w = DISP_DLG_W;  *h = DISP_DLG_H;  return 1;
    case DLG_DPASK:   *w = DP_ASK_W;    *h = DP_ASK_H;    return 1;
    case DLG_FNTASK:  *w = FNT_ASK_W;   *h = FNT_ASK_H;   return 1;
    case DLG_ADDFONT: return afd_dims(w, h);
    }
    return 0;
}
static const char *dlg_title(int slot) {
    if (slot < NOTEPAD_MAX) {
        const notepad_t *n = &notepads[slot];
        if (n->confirm_mode == CONFIRM_SAVEAS) return "Save As";
        if (n->confirm_mode == CONFIRM_INFO) return n->info_kind == 1 ? "Keys" : "About";
        return "Warning!";
    }
    switch (slot) {
    case DLG_WEB:     return SKO("About MiniWeb", "MiniWeb 정보");
    case DLG_DISP:
    case DLG_DPASK:   return SKO("Display Properties", "화면 속성");
    case DLG_FNTASK:  return SKO("Font", "글꼴");
    case DLG_ADDFONT: return SKO("Add Fonts", "글꼴 추가");
    }
    return "";
}
static int dlg_icon(int slot) {
    if (slot < NOTEPAD_MAX) {
        const notepad_t *n = &notepads[slot];
        if (n->confirm_mode == CONFIRM_SAVEAS) return IC_DOC;
        if (n->confirm_mode == CONFIRM_INFO) return n->info_kind == 1 ? IC_HELP : IC_NOTEPAD;
        return IC_WARNING;
    }
    return slot == DLG_WEB ? IC_WEB : slot == DLG_DISP && disp_unavail ? IC_WARNING : IC_SETTING;
}

/* Once per pass of the main loop (and at the start of every frame): open the windows of the questions that were asked, close the ones that
 * were answered. A new dialog appears in the middle of its owner (a system question: of the screen) and in front. */
static void dlg_poll(void) {
    for (int s = 0; s < DLG_MAX; s++) {
        window_t *w = &dlg_wins[s];
        int id = WIN_ID_DLG + s;
        int nw = 0, nh = 0, kind = 0;
        int want = dlg_want(s);
        if (want && !dlg_size_of(s, &nw, &nh)) { if (s == DLG_ADDFONT) afd_on = 0; want = 0; }      /* (cannot be shown: the question is dropped) */
        if (!want) {
            if (w->open) { w->open = 0; w->minimized = 0; w->maximized = 0; win_z_remove(id); }
            continue;
        }
        kind = dlg_kind_of(s);
        if (!w->open) {
            int ow = dlg_owner_id(s);
            int ox = 0, oy = 0, ow_w = VGA_WIDTH, oh = VGA_HEIGHT - TASKBAR_H;
            if (ow >= 0) { const window_t *o = win_ptr(ow); ox = o->x; oy = o->y; ow_w = o->w; oh = o->h; }
            w->open = 1; w->minimized = 0; w->maximized = 0;
            w->w = w->restore_w = nw; w->h = w->restore_h = nh;
            w->x = w->restore_x = ox + (ow_w - nw) / 2;
            w->y = w->restore_y = oy + (oh - nh) / 2;
            dlg_nat_w[s] = nw; dlg_nat_h[s] = nh; dlg_kind_seen[s] = kind;
            win_z_raise(id);                                    /* (also keeps it on the screen) */
        } else if (kind != dlg_kind_seen[s] || nw != dlg_nat_w[s] || nh != dlg_nat_h[s]) {   /* the same window asked something else: it grows or shrinks, its corner stays */
            dlg_nat_w[s] = nw; dlg_nat_h[s] = nh; dlg_kind_seen[s] = kind;
            w->restore_w = nw; w->restore_h = nh;
            if (!w->maximized) { w->w = nw; w->h = nh; win_fit_screen(w); }
        }
    }
}

/* The open question that disables window `owner` (-1 = none): while there is one, a click or a key meant for the owner goes to it instead. */
static int dlg_blocker(int owner) {
    for (int s = 0; s < DLG_MAX; s++) if (dlg_wins[s].open && dlg_owner_id(s) == owner) return s;
    return -1;
}

/* Bring a question forward: what a click on its disabled owner does. A minimized question comes back first. */
static void dlg_activate(int slot) {
    int id = WIN_ID_DLG + slot;
    if (!dlg_wins[slot].open) return;
    if (dlg_wins[slot].minimized) win_restore(id); else win_z_raise(id);
}

/* ---- the three title bar buttons (the same places every window has them) ---- */
static inline int dlg_min_hit(int s, int px, int py)   { const window_t *w = &dlg_wins[s]; return in_rect(px, py, win_btn_min_x(w, 2),   win_btn_y(w, 1), BTN_W, BTN_H); }
static inline int dlg_max_hit(int s, int px, int py)   { const window_t *w = &dlg_wins[s]; return in_rect(px, py, win_btn_max_x(w, 2),   win_btn_y(w, 1), BTN_W, BTN_H); }
static inline int dlg_close_hit(int s, int px, int py) { const window_t *w = &dlg_wins[s]; return in_rect(px, py, win_btn_close_x(w, 2), win_btn_y(w, 1), BTN_W, BTN_H); }
static int dlg_titlebar_hit(int s, int px, int py) {
    const window_t *w = &dlg_wins[s];
    if (!in_rect(px, py, w->x + 1, w->y + 1, w->w - 2, TITLEBAR_H)) return 0;
    return !dlg_min_hit(s, px, py) && !dlg_max_hit(s, px, py) && !dlg_close_hit(s, px, py);
}

/* The X (or Alt-less Esc): bail out of the question and leave everything as it was -- the same as the Cancel it never had. */
static void dlg_cancel(int slot) {
    if (slot < NOTEPAD_MAX) notepads[slot].confirm_mode = CONFIRM_NONE;
    else if (slot == DLG_WEB) web_about_open = 0;
    else if (slot == DLG_DISP) { if (disp_unavail) disp_keep(); else disp_revert(); }      /* the X of "Keep this screen size?" is a No: the safe answer */
    else if (slot == DLG_DPASK) dp_fire(12);
    else if (slot == DLG_FNTASK) fnt_fire(4);
    else if (slot == DLG_ADDFONT) afd_fire(2);
}

/* ---- drawing: the window's frame, title bar and buttons; then the owner's own drawing of what is inside ---- */
static void dlg_draw(int slot, int mx, int my) {
    const window_t *w = &dlg_wins[slot];
    int id = WIN_ID_DLG + slot;
    ui_window_frame_ic(w->x, w->y, w->w, w->h, w->maximized, dlg_title(slot), dlg_icon(slot));
    ui_titlebar_buttons(win_btn_min_x(w, 2), win_btn_max_x(w, 2), win_btn_close_x(w, 2), win_btn_y(w, 1),
                        pressed_btn_kind == BTN_MIN && pressed_btn_win == id,
                        pressed_btn_kind == BTN_MAX && pressed_btn_win == id,
                        pressed_btn_kind == BTN_CLOSE && pressed_btn_win == id);
    if (slot < NOTEPAD_MAX) {
        notepad_t *keep = active_np;
        active_np = &notepads[slot];
        draw_confirm_dialog(mx, my);
        active_np = keep;
    } else if (slot == DLG_WEB) web_draw_about();
    else if (slot == DLG_DISP) disp_confirm_draw();
    else if (slot == DLG_DPASK) dp_ask_draw();
    else if (slot == DLG_FNTASK) fnt_ask_draw();
    else if (slot == DLG_ADDFONT) afd_draw();
}

/* ---- the mouse: a left click on the dialog's window ---- */
static void dlg_click(int id, int px, int py, u32 now) {
    int s = id - WIN_ID_DLG;
    window_t *w = &dlg_wins[s];
    if (dlg_owner_id(s) >= 0) win_z_raise(dlg_owner_id(s));   /* the owner comes up with its question (and the question stays on top of it) ... */
    win_z_raise(id);                                    /* ... and this one is the front window */
    if (dlg_min_hit(s, px, py))        { pressed_btn_kind = BTN_MIN;   pressed_btn_win = id; }
    else if (dlg_max_hit(s, px, py))   { pressed_btn_kind = BTN_MAX;   pressed_btn_win = id; }
    else if (dlg_close_hit(s, px, py)) { pressed_btn_kind = BTN_CLOSE; pressed_btn_win = id; }
    else if (dlg_titlebar_hit(s, px, py)) {
        if (!w->maximized) { dragging_id = id; drag_offset_x = px - w->x; drag_offset_y = py - w->y; }
    } else if (s == DLG_WEB) {
        if (web_about_ok_hit(px, py)) { pressed_btn_kind = BTN_WEB_GO; pressed_btn_win = WIN_ID_WEB; web_tb_pressed = 99; }     /* OK: fires on release (apps/web, ui/input.h) */
    } else if (s == DLG_DISP) {
        disp_confirm_mouse(px, py, 1);
    } else if (s == DLG_DPASK) {
        dp_click(px, py);                                   /* the sheets' own click code answers: it hits its buttons first, and fires them on release (dp_release) */
    } else if (s == DLG_FNTASK) {
        fnt_click(px, py, now);
    } else if (s == DLG_ADDFONT) {
        afd_click(px, py, now);
    } else {
        active_np = &notepads[s];
        if (confirm_yes_hit(px, py)) confirm_yes_action();
        else if (confirm_no_hit(px, py)) confirm_no_action();
        else if (active_np->confirm_mode == CONFIRM_SAVEAS && saveas_field_hit(px, py))
            tf_mouse_down(&active_np->saveas_tf, active_np->saveas_name, TF_NAME, px, keyboard_shift_held());
    }
}

/* A title bar button was pressed and let go over the same button: do what it says. */
static void dlg_button(int id, int kind) {
    int s = id - WIN_ID_DLG;
    window_t *w = &dlg_wins[s];
    if (kind == BTN_MIN) win_minimize(id);
    else if (kind == BTN_MAX) { if (w->maximized) unmaximize_window(w); else maximize_window(w); }
    else if (kind == BTN_CLOSE) dlg_cancel(s);
}
static int dlg_button_still_over(int id, int kind, int px, int py) {
    int s = id - WIN_ID_DLG;
    if (kind == BTN_MIN) return dlg_min_hit(s, px, py);
    if (kind == BTN_MAX) return dlg_max_hit(s, px, py);
    if (kind == BTN_CLOSE) return dlg_close_hit(s, px, py);
    return 0;
}

/* ---- the keyboard: Notepad's question (Y / N, Enter and Esc in Save As, any of them for the Help boxes) ---- */
static void np_confirm_key(int k) {
    if (active_np->confirm_mode == CONFIRM_SAVEAS) {
        /* the Save As dialog is a text field: letters go INTO the name, so Y/N are not shortcuts here */
        if (k == '\n') { tf_ime_flush(); saveas_do(); }          /* (the Hangul syllable in progress is committed first) */
        else if (k == 27) { tf_ime_flush(); confirm_no_action(); }   /* Esc */
        else if (k != KEY_NONE) tf_key(&active_np->saveas_tf, active_np->saveas_name, sizeof active_np->saveas_name, TF_NAME, k, saveas_sync);   /* caret, selection, clipboard, Hangul IME: ui/textfield.h */
    } else if (active_np->confirm_mode == CONFIRM_INFO) {
        if (k == '\n' || k == 27 || k == ' ' || k == 'y' || k == 'Y' || k == 'n' || k == 'N') confirm_yes_action();   /* any of these closes the Help dialog */
    } else if (k == 'y' || k == 'Y') {
        confirm_yes_action();
    } else if (k == 'n' || k == 'N') {
        confirm_no_action();
    }
}
static void dlg_key(int id, int k) {
    int s = id - WIN_ID_DLG;
    if (s == DLG_WEB) { if (k == '\n' || k == 27 || k == ' ') web_about_open = 0; return; }
    if (s == DLG_DISP) { if (k == '\n') disp_keep(); else if (k == 27) disp_revert(); return; }
    if (s == DLG_DPASK) { dp_key(k); return; }
    if (s == DLG_FNTASK) { fnt_key(k); return; }
    if (s == DLG_ADDFONT) { afd_key(k); return; }
    active_np = &notepads[s];                           /* the owner, which now has the keyboard focus like any front Notepad */
    np_confirm_key(k);
}

#endif
