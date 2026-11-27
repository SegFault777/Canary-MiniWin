/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_UI_INPUT_H
#define MW_UI_INPUT_H

/* ui/input.h -- the desktop's input handling: the mouse (clicks, drags, buttons, menus, icons) and the keyboard routing.
 * (Split out of kmain() in pre-29; still one translation unit -- see the module map in kernel.c.) */

/* ---- input state (these were locals of kmain's loop) ---- */
static int mx = 320, my = 240;                    /* (set to the middle of the real screen at boot: see kmain) */
static int awaiting_second_click = 0;   /* 1 = one icon click seen, waiting for a 2nd within the window */
static u32 last_icon_click_tick = 0;
static int awaiting_second_click_setting = 0;   /* same idea, but for the SETTING.MWP icon */
static u32 last_setting_click_tick = 0;
static int awaiting_second_click_web = 0;   /* same idea, but for the MINIWEB.MWP icon */
static u32 last_web_click_tick = 0;
static int awaiting_second_click_term = 0;  /* ...and for the MINISHELL.MWP icon */
static u32 last_term_click_tick = 0;
static int awaiting_second_click_files = 0; /* ...and for the MINIFILES.MWP icon */
static u32 last_files_click_tick = 0;
static int awaiting_second_click_install = 0; /* ...and for the live disc's INSTALL.MWP icon */
static u32 last_install_click_tick = 0;

/* The narrowest width a window may be resized to: MIN_WIN_W, or -- for a window with a menu bar -- enough for ALL its menu titles, so
 * the rightmost one can never be dragged out of sight (1.1-alpha-14). Korean titles are wider or narrower than English ones, so this
 * is asked at every drag step, not once. */
static int win_min_w(int id) {
    int w = 0;
    if (id == WIN_ID_SETTING)       w = mb_bar_min_w(setting_menu_list(), SETTING_MENU_N);
    else if (id == WIN_ID_WEB)      w = mb_bar_min_w(web_menus, WEB_MENU_N);
    else if (id == WIN_ID_TERMINAL) w = mb_bar_min_w(term_menus, TERM_MENU_N);
    else if (id == WIN_ID_FILES)    w = mb_bar_min_w(files_menus, FILES_MENU_N);
    else if (!PW_IS(id))            w = mb_bar_min_w(np_menus, NP_MENU_N);        /* a Notepad window */
    return w > MIN_WIN_W ? w : MIN_WIN_W;
}
static int awaiting_second_click_file_slot = -1;   /* which file icon (if any) saw a first click */
static u32 last_file_icon_click_tick = 0;
static u32 tick = 0;
static int np_mb_m, np_mb_i;                 /* the Notepad menu item just chosen (ui/menubar.h's mb_click fills them) */
static int setting_click_result = 0;         /* what setting_click() said about the last click (apps/setting.h) */
/* double_click_window (apps/setting.h) is the double-click time in ticks; Settings > Mouse picks it. */

/* Window-drag state: while dragging, we track which window id is
 * being dragged (-1 = none) and the offset from that window's
 * top-left corner to the point the user grabbed, so the window
 * follows the cursor without "snapping" its corner to it. One shared
 * mechanism now covers every Notepad window AND Setting, instead of
 * a separate pair of variables per window. */

/* One poll of the mouse: moves the pointer, then dispatches presses, drags and releases to whatever is under it. */
static void handle_mouse(void) {
    dlg_poll();                                   /* questions asked or answered since the last pass: their windows open or close (ui/dialog.h) */
    if (mouse_poll()) {
        if (mouse_abs_valid) {                    /* a USB tablet said exactly where the pointer is */
            mx = mouse_abs_x;
            my = mouse_abs_y;
            mouse_abs_valid = 0;
        }
        mx += mouse_dx;
        my += mouse_dy;
        if (mx < 0) mx = 0;
        if (my < 0) my = 0;
        if (mx > VGA_WIDTH - 1)  mx = VGA_WIDTH - 1;
        if (my > VGA_HEIGHT - 1) my = VGA_HEIGHT - 1;

        int left_now = mouse_left;
        int clicked = mouse_click_event; /* per-packet edge detection from the driver */
        if (oobe_open) { oobe_mouse(mx, my, clicked); return; }                      /* the first-run setup / the logon prompt owns the mouse ... */

        /* ---- a scroll bar thumb being dragged (ui/scrollbar.h) ---- */
        if (sb_dragging()) { if (left_now) sb_drag(mx, my); else sb_release(); }
        tf_poll_drag(mx, left_now);
        ms_poll_drag(mx, left_now); dp_poll_drag(mx, left_now);                                              /* a slider of the Mouse sheet being dragged */                                              /* extending a text field's selection (ui/textfield.h) */
        for (int ni = 0; ni < NOTEPAD_MAX; ni++) if (notepads[ni].sel_drag) {      /* extending a Notepad selection with the mouse */
            notepad_t *keep = active_np; active_np = &notepads[ni];
            if (left_now) np_mouse_drag(mx, my); else active_np->sel_drag = 0;
            active_np = keep;
        }

        /* ---- the mouse wheel scrolls the window under the pointer ---- */
        if (mouse_wheel) {
            int wh_id = -1;
            int notches = mouse_wheel;
            mouse_wheel = 0;
            (void)compute_hover_resize_zone(mx, my, &wh_id);
            if (wh_id == WIN_ID_FILES && files_in_tree(mx, my)) wh_id = SB_OWNER_FILES_TREE;     /* the wheel over the folder tree scrolls the tree */
            if (wh_id == WIN_ID_SETTING && (dt_wheel(notches) || ms_wheel(notches, mx, my) || dp_wheel(notches, mx, my))) { /* the zone list or the pointer list used it */ }
            else if (wh_id >= 0) sb_wheel(wh_id, notches);
        }

        /* ---- drag in progress: move whichever window is being
         * dragged with the cursor ---- */
        if (dragging_id >= 0) {
            if (left_now) {
                window_t *dw = win_ptr(dragging_id);
                if (!dw->maximized) {
                    int new_x = mx - drag_offset_x;
                    int new_y = my - drag_offset_y;
                    /* keep at least a sliver of the title bar on-screen
                     * so the window can never be dragged somewhere the
                     * user can't grab it again */
                    dw->x = clampi(new_x, -(dw->w - 20), VGA_WIDTH - 20);
                    dw->y = clampi(new_y, 0, VGA_HEIGHT - TASKBAR_H - TITLEBAR_H);
                }
            } else {
                dragging_id = -1; /* button released -> stop dragging */
            }
        }

        /* ---- resize in progress: grow/shrink from whichever
         * edge(s) were grabbed, anchored on the opposite edge(s) so
         * the corner/side you're NOT dragging stays put ---- */
        if (resizing_id >= 0) {
            if (left_now) {
                int dx = mx - resize_start_mx;
                int dy = my - resize_start_my;
                int nx = resize_start_x, ny = resize_start_y;
                int nw = resize_start_w, nh = resize_start_h;

                if (resizing_zone & RESIZE_E) nw = resize_start_w + dx;
                if (resizing_zone & RESIZE_S) nh = resize_start_h + dy;
                if (resizing_zone & RESIZE_W) { nx = resize_start_x + dx; nw = resize_start_w - dx; }
                if (resizing_zone & RESIZE_N) { ny = resize_start_y + dy; nh = resize_start_h - dy; }

                int minw = win_min_w(resizing_id);
                if (nw < minw) {
                    if (resizing_zone & RESIZE_W) nx -= (minw - nw);
                    nw = minw;
                }
                if (nh < MIN_WIN_H) {
                    if (resizing_zone & RESIZE_N) ny -= (MIN_WIN_H - nh);
                    nh = MIN_WIN_H;
                }
                if (nx < 0) nx = 0;
                if (ny < 0) ny = 0;

                window_t *rw = win_ptr(resizing_id);
                rw->x = nx; rw->y = ny; rw->w = nw; rw->h = nh;
                /* keep restore_* in sync too, so a later maximize+
                 * restore cycle snaps back to this size, not
                 * whatever size the window happened to be before
                 * this resize started */
                rw->restore_x = nx; rw->restore_y = ny; rw->restore_w = nw; rw->restore_h = nh;
            } else {
                resizing_id = -1;
                resizing_zone = RESIZE_NONE;
            }
        }

        /* ---- the right-click menu (ui/ctxmenu.h): the right button opens one; while it is open the next left click belongs to it ---- */
        if (mouse_rclick_event) { pm_context_at(mx, my); if (pm_status_msg) { status = pm_status_msg; pm_status_msg = 0; } }
        else if (clicked && pm_open) {
            pm_click(mx, my);
            clicked = 0;
            if (pm_status_msg) { status = pm_status_msg; pm_status_msg = 0; }
        }

        if (clicked) {
            tf_ime_flush(); web_ime_flush();                          /* a click commits the Hangul syllable in progress, wherever it is */
            /* Recomputed once per click since there are at most
             * WIN_ID_COUNT (5) windows -- cheap enough not to bother
             * caching, and it keeps this in sync with draw_taskbar()
             * by construction (same taskbar_layout() underneath). */
            int taskbar_btn_id = taskbar_button_hit(mx, my);

            if (clock_popup_open) {
                /* The popup (ui/clock_popup.h): a click on the clock face or the calendar opens Control Panel > Date/Time;
                 * a click anywhere else (including on the clock itself again) just closes it. */
                int inside = clockpop_hit(mx, my);
                clock_popup_open = 0;
                if (inside) {
                    setting.open = 1;
                    if (win_is_minimized(WIN_ID_SETTING)) win_restore(WIN_ID_SETTING);
                    win_z_raise(WIN_ID_SETTING);
                    setting_sel = SP_DATETIME; setting_open_item(SP_DATETIME);
                    status = t(STR_SETTING_OPENED);
                }
            } else if (power_menu_open) {
                /* Cascaded off the Start Menu's power item. Whatever
                 * this click was for -- an action or a miss -- both
                 * menus close afterward, same as clicking a Start
                 * Menu item normally would. */
                if (power_menu_item_hit(mx, my, 0)) {
                    system_shutdown(); /* does not return */
                } else if (power_menu_item_hit(mx, my, 1)) {
                    system_restart(); /* does not return */
                }
                power_menu_open = 0;
                start_menu_open = 0;
            } else if (start_menu_open) {
                /* Same "pick an item or dismiss" contract as the File
                 * dropdown below -- except the power item, which
                 * cascades into its own flyout instead of resolving
                 * immediately, so it deliberately does NOT close the
                 * Start Menu the way the other two items do. */
                int smc = sm_depth > 0 ? sm_click(mx, my) : 0;             /* a click on a Programs flyout comes first */
                if (smc == 1) {
                    /* a folder opened or the "(Empty)" row: the menus stay up */
                } else if (smc == 2) {
                    const char *why = sm_launch(sm_target);
                    if (why) status = why;
                    start_menu_open = 0; sm_depth = 0;
                } else if (smc == 3) {
                    const char *why = startmenu_add_open();
                    if (why) status = why;
                    start_menu_open = 0; sm_depth = 0;
                } else if (start_menu_item_hit(mx, my, STARTMENU_PROGRAMS_IDX)) {
                    if (sm_depth > 0) sm_depth = 0; else sm_open();
                    power_menu_open = 0;
                } else if (start_menu_item_hit(mx, my, 0)) {
                    int slot = find_free_notepad_slot();
                    if (slot < 0) {
                        status = t(STR_ALL_NOTEPAD_WINDOWS_OPEN);
                    } else {
                        active_np = &notepads[slot];
                        active_np->win.open = 1;
                        active_np->win.minimized = 0;
                        active_np->text_len = 0; active_np->text_buf[0] = 0;
                        active_np->bound_slot = -1;
                        active_np->file_menu_open = 0;
                        active_np->confirm_mode = CONFIRM_NONE;
                        ko_ime_reset();
                        win_z_raise(slot);
                        status = t(STR_NOTEPAD_OPENED);
                    }
                    start_menu_open = 0;
                } else if (start_menu_item_hit(mx, my, 1)) {
                    setting.open = 1;
                    win_z_raise(WIN_ID_SETTING);
                    status = t(STR_SETTING_OPENED);
                    start_menu_open = 0;
                } else if (start_menu_item_hit(mx, my, 2)) {
                    web_win.open = 1; web_opened();
                    win_z_raise(WIN_ID_WEB);
                    status = t(STR_WEB_OPENED);
                    start_menu_open = 0;
                } else if (start_menu_item_hit(mx, my, 3)) {
                    term_win.open = 1;
                    win_z_raise(WIN_ID_TERMINAL);
                    status = t(STR_TERMINAL_OPENED);
                    start_menu_open = 0;
                } else if (start_menu_item_hit(mx, my, 4)) {
                    files_open_window();
                    status = t(STR_FILES_OPENED);
                    start_menu_open = 0;
                } else if (start_menu_item_hit(mx, my, STARTMENU_POWER_IDX)) {
                    power_menu_open = 1; sm_depth = 0;
                } else {
                    start_menu_open = 0; /* click outside dismisses everything */
                }
            } else if (start_button_hit(mx, my)) {
                start_menu_open = 1; sm_depth = 0;
            } else if (clock_hit(mx, my)) {
                clock_popup_open = 1;
            } else if (taskbar_btn_id >= 0) {
                /* A taskbar button (Windows 95 style, ui/taskbar.h): the pressed one (the front window) minimizes its window; a
                 * minimized window's button restores it; the button of a window that is open but behind another brings it to the
                 * front -- and the pressed look follows, because the pressed button is always the front window's. */
                int id = taskbar_btn_id;
                if (win_is_minimized(id)) {
                    win_restore(id);
                    if (id == WIN_ID_SETTING) status = t(STR_SETTING_RESTORED);
                    else if (id == WIN_ID_WEB) status = t(STR_WEB_RESTORED);
                    else if (id == WIN_ID_TERMINAL) status = t(STR_TERMINAL_RESTORED);
                    else if (id == WIN_ID_FILES) status = t(STR_FILES_RESTORED);
                    else if (PW_IS(id)) status = "PROGRAM RESTORED";
                    else if (!DLG_IS(id)) status = t(STR_NOTEPAD_RESTORED);
                } else if (id == taskbar_active_id()) {
                    win_minimize(id);
                } else {
                    win_z_raise(id);
                }
                if (id < NOTEPAD_MAX) active_np = &notepads[id];        /* a Notepad that comes to the front also gets the keyboard */
            } else {
                /* Topmost open+visible window whose rect (or resize
                 * margin) contains the click, z-order back-to-front
                 * reversed so the FRONT-most window wins when two
                 * happen to overlap. */
                int hit_id;
                int zone = compute_hover_resize_zone(mx, my, &hit_id);
                if (mb_owner >= 0 && hit_id != mb_owner) mb_close();           /* a drop-down closes when you click anywhere but its own window */

                if (zone != RESIZE_NONE) {
                    /* Grabbed an edge or corner: start resizing
                     * instead of any of the normal window-content
                     * handling below. Brings the window to front
                     * too, same as any other interaction with it. */
                    win_z_raise(hit_id);
                    resizing_id = hit_id;
                    resizing_zone = zone;
                    resize_start_mx = mx;
                    resize_start_my = my;
                    window_t *rw = win_ptr(hit_id);
                    resize_start_x = rw->x;
                    resize_start_y = rw->y;
                    resize_start_w = rw->w;
                    resize_start_h = rw->h;
                } else if (hit_id >= 0 && (sb_press(hit_id, mx, my) || sb_press(SB_H_BASE + hit_id, mx, my) || (hit_id == WIN_ID_FILES && sb_press(SB_OWNER_FILES_TREE, mx, my)) )) {
                    win_z_raise(hit_id);                       /* a press on a scroll bar: arrow, page jump or the start of a thumb drag */
                } else if (DLG_IS(hit_id)) {
                    dlg_click(hit_id, mx, my, tick);                  /* a question's own window (ui/dialog.h): its buttons, its title bar, its contents */
                } else if (hit_id == WIN_ID_SETTING) {
                    /* Clicking anywhere on a window -- not just a
                     * control that does something -- brings it to
                     * the front, same as any real window manager. */
                    win_z_raise(WIN_ID_SETTING);
                    int mb_m, mb_i;
                    int sdlg = dlg_blocker(WIN_ID_SETTING);
                    if (sdlg >= 0) {                                                 /* one of its questions is open (a window of its own): Setting is disabled until it is answered */
                        dlg_activate(sdlg);
                    } else if (setting_min_hit(mx, my)) {
                        pressed_btn_kind = BTN_MIN;
                        pressed_btn_win = WIN_ID_SETTING;
                    } else if (setting_max_hit(mx, my)) {
                        pressed_btn_kind = BTN_MAX;
                        pressed_btn_win = WIN_ID_SETTING;
                    } else if (setting_close_hit(mx, my)) {
                        pressed_btn_kind = BTN_CLOSE;
                        pressed_btn_win = WIN_ID_SETTING;
                    } else if (mb_click(WIN_ID_SETTING, setting.x, setting.y, setting.w, setting_menu_list(), SETTING_MENU_N, mx, my, &mb_m, &mb_i)) {
                        if (mb_i >= 0) setting_menu_action(mb_m, mb_i);
                    } else if ((setting_click_result = setting_click(mx, my, tick)) != 0) {
                        if (setting_click_result == 2) status = t(STR_IME_MIN_ONE);
                        else if (setting_click_result == 3) status = t(STR_DEFAULT_HINT);
                    } else if (setting_titlebar_drag_hit(mx, my) && !setting.maximized) {
                        dragging_id = WIN_ID_SETTING;
                        drag_offset_x = mx - setting.x;
                        drag_offset_y = my - setting.y;
                    }
                } else if (hit_id == WIN_ID_WEB) {
                    int mb_m, mb_i;
                    win_z_raise(WIN_ID_WEB);
                    if (web_about_open) {                                           /* the About box is a window of its own: the browser is disabled until it is answered */
                        dlg_activate(DLG_WEB);
                    } else if (web_min_hit(mx, my)) {
                        pressed_btn_kind = BTN_MIN;
                        pressed_btn_win = WIN_ID_WEB;
                    } else if (web_max_hit(mx, my)) {
                        pressed_btn_kind = BTN_MAX;
                        pressed_btn_win = WIN_ID_WEB;
                    } else if (web_close_hit(mx, my)) {
                        pressed_btn_kind = BTN_CLOSE;
                        pressed_btn_win = WIN_ID_WEB;
                    } else if (web_links_item_at(mx, my) >= 0 && web_links_open) {  /* a site of the Links drop-down */
                        web_urlbar_focused = 0;
                        int site = web_links_item_at(mx, my);
                        web_links_open = 0;
                        web_go_link(site);
                    } else if (mb_click(WIN_ID_WEB, web_win.x, web_win.y, web_win.w, web_menus, WEB_MENU_N, mx, my, &mb_m, &mb_i)) {
                        web_links_open = 0;
                        if (mb_i >= 0) web_menu_action(mb_m, mb_i);
                    } else if (web_urlbar_hit(mx, my)) {
                        web_links_open = 0;
                        web_urlbar_focused = 1;
                        rd_focus = 0;
                        tf_mouse_down(&web_urlbar_tf, web_urlbar_buf, TF_ASCII, mx, keyboard_shift_held());     /* the caret goes where you click */
                    } else if ((web_tb_pressed = web_tb_hit(mx, my)) >= 0) {        /* a toolbar button (or Links): it fires on release */
                        web_links_was_open = web_links_open;
                        web_links_open = 0;
                        pressed_btn_kind = BTN_WEB_GO;
                        pressed_btn_win = WIN_ID_WEB;
                        web_urlbar_focused = 0;
                    } else if (web_page_click(mx, my)) {
                        web_links_open = 0;
                        web_urlbar_focused = 0;       /* a click on the page: follow a link / use the scrollbar */
                    } else if (web_titlebar_drag_hit(mx, my) && !web_win.maximized) {
                        web_urlbar_focused = 0;
                        dragging_id = WIN_ID_WEB;
                        drag_offset_x = mx - web_win.x;
                        drag_offset_y = my - web_win.y;
                    } else {
                        web_urlbar_focused = 0;
                    }
                } else if (PW_IS(hit_id)) {
                    pw_select(hit_id);
                    win_z_raise(hit_id);
                    if (pw_min_hit(mx, my)) {
                        pressed_btn_kind = BTN_MIN;
                        pressed_btn_win = hit_id;
                    } else if (pw_max_hit(mx, my)) {
                        pressed_btn_kind = BTN_MAX;
                        pressed_btn_win = hit_id;
                    } else if (pw_close_hit(mx, my)) {
                        pressed_btn_kind = BTN_CLOSE;
                        pressed_btn_win = hit_id;
                    } else if (pw_titlebar_drag_hit(mx, my) && !pw_win.maximized) {
                        dragging_id = hit_id;
                        drag_offset_x = mx - pw_win.x;
                        drag_offset_y = my - pw_win.y;
                    }                             /* a click in the client area: the program sees it through pw_step() */
                } else if (hit_id == WIN_ID_TERMINAL) {
                    int mb_m, mb_i;
                    win_z_raise(WIN_ID_TERMINAL);
                    if (term_min_hit(mx, my)) {
                        pressed_btn_kind = BTN_MIN;
                        pressed_btn_win = WIN_ID_TERMINAL;
                    } else if (term_max_hit(mx, my)) {
                        pressed_btn_kind = BTN_MAX;
                        pressed_btn_win = WIN_ID_TERMINAL;
                    } else if (term_close_hit(mx, my)) {
                        pressed_btn_kind = BTN_CLOSE;
                        pressed_btn_win = WIN_ID_TERMINAL;
                    } else if (mb_click(WIN_ID_TERMINAL, term_win.x, term_win.y, term_win.w, term_menus, TERM_MENU_N, mx, my, &mb_m, &mb_i)) {
                        if (mb_i >= 0) term_menu_action(mb_m, mb_i);
                    } else if (term_input_hit(mx, my)) {
                        term_input_focused = 1;
                        tf_mouse_down(&term_input_tf, term_input_buf, term_input_flags(), mx, keyboard_shift_held());
                    } else if (term_titlebar_drag_hit(mx, my) && !term_win.maximized) {
                        term_input_focused = 0;
                        dragging_id = WIN_ID_TERMINAL;
                        drag_offset_x = mx - term_win.x;
                        drag_offset_y = my - term_win.y;
                    } else {
                        term_input_focused = 0;
                    }
                } else if (hit_id == WIN_ID_FILES) {
                    int mb_m, mb_i;
                    win_z_raise(WIN_ID_FILES);
                    if (files_min_hit(mx, my)) {
                        pressed_btn_kind = BTN_MIN;
                        pressed_btn_win = WIN_ID_FILES;
                    } else if (files_max_hit(mx, my)) {
                        pressed_btn_kind = BTN_MAX;
                        pressed_btn_win = WIN_ID_FILES;
                    } else if (files_close_hit(mx, my)) {
                        pressed_btn_kind = BTN_CLOSE;
                        pressed_btn_win = WIN_ID_FILES;
                    } else if (mb_click(WIN_ID_FILES, files_win.x, files_win.y, files_win.w, files_menus, FILES_MENU_N, mx, my, &mb_m, &mb_i)) {
                        if (mb_i >= 0) files_menu_action(mb_m, mb_i);
                    } else if (files_titlebar_drag_hit(mx, my) && !files_win.maximized) {
                        dragging_id = WIN_ID_FILES;
                        drag_offset_x = mx - files_win.x;
                        drag_offset_y = my - files_win.y;
                    } else {
                        files_click(mx, my, tick, double_click_window);
                    }
                } else if (hit_id >= 0) {
                    active_np = &notepads[hit_id];
                    win_z_raise(hit_id);

                    if (active_np->confirm_mode != CONFIRM_NONE) {
                        /* Disabled while its question is open (the question is a window of its own, ui/dialog.h): a click here only
                         * brings the question forward. Other windows remain fully interactive. */
                        dlg_activate(hit_id);
                    } else if (in_rect(mx, my, btn_min_x(), btn_y(), BTN_W, BTN_H)) {
                        pressed_btn_kind = BTN_MIN;
                        pressed_btn_win = hit_id;
                    } else if (in_rect(mx, my, btn_max_x(), btn_y(), BTN_W, BTN_H)) {
                        pressed_btn_kind = BTN_MAX;
                        pressed_btn_win = hit_id;
                    } else if (in_rect(mx, my, btn_close_x(), btn_y(), BTN_W, BTN_H)) {
                        pressed_btn_kind = BTN_CLOSE;
                        pressed_btn_win = hit_id;
                    } else if (np_mb_m = -1, np_mb_i = -1, mb_click(hit_id, active_np->win.x, active_np->win.y, active_np->win.w, np_menus, NP_MENU_N, mx, my, &np_mb_m, &np_mb_i)) {
                        if (np_mb_i >= 0) np_menu_action(np_mb_m, np_mb_i);
                    } else if (np_edit_hit(mx, my)) {
                        np_mouse_down(mx, my, keyboard_shift_held());          /* a click puts the caret there; dragging selects */
                    } else if (titlebar_drag_hit(mx, my) && !active_np->win.maximized) {
                        /* start dragging: remember the grab offset so
                         * the window doesn't jump when the drag begins */
                        dragging_id = hit_id;
                        drag_offset_x = mx - active_np->win.x;
                        drag_offset_y = my - active_np->win.y;
                    }
                } else if (in_rect(mx, my, ICON_X, ICON_Y, ICON_W, ICON_H)) {
                    if (awaiting_second_click && (tick - last_icon_click_tick) < double_click_window) {
                        /* 2nd click of a double-click: open a brand
                         * new blank window in the first free slot. */
                        int slot = find_free_notepad_slot();
                        if (slot < 0) {
                            status = t(STR_ALL_NOTEPAD_WINDOWS_OPEN);
                        } else {
                            active_np = &notepads[slot];
                            active_np->win.open = 1;
                            active_np->win.minimized = 0;
                            active_np->text_len = 0; active_np->text_buf[0] = 0;
                            active_np->bound_slot = -1;
                            active_np->file_menu_open = 0;
                            active_np->confirm_mode = CONFIRM_NONE;
                            ko_ime_reset();
                            win_z_raise(slot);
                            status = t(STR_NOTEPAD_OPENED);
                        }
                        awaiting_second_click = 0;
                    } else {
                        awaiting_second_click = 1;
                        last_icon_click_tick = tick;
                    }
                } else if (in_rect(mx, my, ICON2_X, ICON2_Y, ICON2_W, ICON2_H)) {
                    if (awaiting_second_click_setting && (tick - last_setting_click_tick) < double_click_window) {
                        setting.open = 1;
                        win_z_raise(WIN_ID_SETTING);
                        status = t(STR_SETTING_OPENED);
                        awaiting_second_click_setting = 0;
                    } else {
                        awaiting_second_click_setting = 1;
                        last_setting_click_tick = tick;
                    }
                } else if (in_rect(mx, my, ICON3_X, ICON3_Y, ICON3_W, ICON3_H)) {
                    if (awaiting_second_click_web && (tick - last_web_click_tick) < double_click_window) {
                        web_win.open = 1; web_opened();
                        win_z_raise(WIN_ID_WEB);
                        status = t(STR_WEB_OPENED);
                        awaiting_second_click_web = 0;
                    } else {
                        awaiting_second_click_web = 1;
                        last_web_click_tick = tick;
                    }
                } else if (in_rect(mx, my, ICON4_X, ICON4_Y, ICON4_W, ICON4_H)) {
                    if (awaiting_second_click_term && (tick - last_term_click_tick) < double_click_window) {
                        term_win.open = 1;
                        win_z_raise(WIN_ID_TERMINAL);
                        status = t(STR_TERMINAL_OPENED);
                        awaiting_second_click_term = 0;
                    } else {
                        awaiting_second_click_term = 1;
                        last_term_click_tick = tick;
                    }
                } else if (in_rect(mx, my, ICON5_X, ICON5_Y, ICON5_W, ICON5_H)) {
                    if (awaiting_second_click_files && (tick - last_files_click_tick) < double_click_window) {
                        files_open_window();
                        status = t(STR_FILES_OPENED);
                        awaiting_second_click_files = 0;
                    } else {
                        awaiting_second_click_files = 1;
                        last_files_click_tick = tick;
                    }
                } else if (live_boot && in_rect(mx, my, ICONI_X, ICONI_Y, ICON_SLOT_W, ICON_H)) {
                    /* INSTALL.MWP, bottom right: only on a live disc (it is not even drawn anywhere else) */
                    if (awaiting_second_click_install && (tick - last_install_click_tick) < double_click_window) {
                        const char *why = installer_open();
                        if (why) status = why;
                        awaiting_second_click_install = 0;
                    } else {
                        awaiting_second_click_install = 1;
                        last_install_click_tick = tick;
                    }
                } else {
                    /* Check the desktop shortcuts last: documents (open in Notepad) and programs (run). The double-click
                     * memory is one number per shortcut: a document's slot, or 100 + a program's slot. */
                    int kind, slot;
                    for (int n = 0; desk_item(n, &kind, &slot); n++) {
                        if (!in_rect(mx, my, desk_item_x(n), desk_item_y(n), ICON_W, ICON_H)) continue;
                        int key = kind == DESK_KIND_DOC ? slot : 100 + slot;

                        if (awaiting_second_click_file_slot == key &&
                            (tick - last_file_icon_click_tick) < double_click_window) {
                            awaiting_second_click_file_slot = -1;
                            if (kind == DESK_KIND_DOC) open_document_slot(slot);
                            else { const char *why = pw_launch(slot, prog_name_of(slot)); if (why) status = why; }
                        } else {
                            awaiting_second_click_file_slot = key;
                            last_file_icon_click_tick = tick;
                        }
                        break;
                    }
                }
            }
        }

        /* ---- button release: fire the action IF the cursor is
         * still over the exact button that was pressed, then clear
         * the press state either way. Dragging off before letting
         * go cancels it -- same contract as any real button. ---- */
        if (mouse_release_event) files_release(mx, my);   /* MiniFiles' own buttons (OPEN/DELETE/REFRESH) fire on release */
        if (mouse_release_event) dt_release(mx, my);      /* so do the Date/Time sheet's OK / Cancel / Apply and arrows */
        if (mouse_release_event) ms_release(mx, my);      /* ... and the Mouse sheet's */
        if (mouse_release_event) dp_release(mx, my);      /* ... and the Display sheet's */
        if (mouse_release_event) fnt_release(mx, my);     /* ... and the Fonts folder's */

        if (mouse_release_event && pressed_btn_kind != BTN_NONE) {
            int kind = pressed_btn_kind, win = pressed_btn_win;
            int still_over = 0;

            if (DLG_IS(win)) {
                still_over = dlg_button_still_over(win, kind, mx, my);
            } else if (win == WIN_ID_SETTING) {
                if (kind == BTN_MIN) still_over = setting_min_hit(mx, my);
                else if (kind == BTN_MAX) still_over = setting_max_hit(mx, my);
                else if (kind == BTN_CLOSE) still_over = setting_close_hit(mx, my);
            } else if (win == WIN_ID_WEB) {
                if (kind == BTN_MIN) still_over = web_min_hit(mx, my);
                else if (kind == BTN_MAX) still_over = web_max_hit(mx, my);
                else if (kind == BTN_CLOSE) still_over = web_close_hit(mx, my);
                else if (kind == BTN_WEB_GO) still_over = web_about_open ? web_about_ok_hit(mx, my) : web_tb_hit(mx, my) == web_tb_pressed;
            } else if (win == WIN_ID_FILES) {
                if (kind == BTN_MIN) still_over = files_min_hit(mx, my);
                else if (kind == BTN_MAX) still_over = files_max_hit(mx, my);
                else if (kind == BTN_CLOSE) still_over = files_close_hit(mx, my);
            } else if (PW_IS(win)) {
                pw_select(win);
                if (kind == BTN_MIN) still_over = pw_min_hit(mx, my);
                else if (kind == BTN_MAX) still_over = pw_max_hit(mx, my);
                else if (kind == BTN_CLOSE) still_over = pw_close_hit(mx, my);
            } else if (win == WIN_ID_TERMINAL) {
                if (kind == BTN_MIN) still_over = term_min_hit(mx, my);
                else if (kind == BTN_MAX) still_over = term_max_hit(mx, my);
                else if (kind == BTN_CLOSE) still_over = term_close_hit(mx, my);
            } else if (win >= 0 && win < NOTEPAD_MAX) {
                active_np = &notepads[win];
                if (kind == BTN_MIN) still_over = in_rect(mx, my, btn_min_x(), btn_y(), BTN_W, BTN_H);
                else if (kind == BTN_MAX) still_over = in_rect(mx, my, btn_max_x(), btn_y(), BTN_W, BTN_H);
                else if (kind == BTN_CLOSE) still_over = in_rect(mx, my, btn_close_x(), btn_y(), BTN_W, BTN_H);
            }

            if (still_over) {
                if (DLG_IS(win)) {
                    dlg_button(win, kind);                          /* _ [] X of a question's window (ui/dialog.h) */
                } else if (win == WIN_ID_SETTING) {
                    if (kind == BTN_MIN) {
                        win_minimize(WIN_ID_SETTING);
                        status = t(STR_SETTING_MINIMIZED);
                    } else if (kind == BTN_MAX) {
                        if (setting.maximized) {
                            unmaximize_window(&setting);
                            status = t(STR_SETTING_RESTORED);
                        } else {
                            maximize_window(&setting);
                            status = t(STR_SETTING_MAXIMIZED);
                        }
                    } else if (kind == BTN_CLOSE) {
                        setting.open = 0;
                        win_z_remove(WIN_ID_SETTING);
                    }
                } else if (win == WIN_ID_WEB) {
                    if (kind == BTN_MIN) {
                        win_minimize(WIN_ID_WEB);
                        status = t(STR_WEB_MINIMIZED);
                    } else if (kind == BTN_MAX) {
                        if (web_win.maximized) {
                            unmaximize_window(&web_win);
                            status = t(STR_WEB_RESTORED);
                        } else {
                            maximize_window(&web_win);
                            status = t(STR_WEB_MAXIMIZED);
                        }
                    } else if (kind == BTN_CLOSE) {
                        /* No unsaved-changes concept here either --
                         * a fetched page isn't a document, so this
                         * closes immediately, same reasoning as
                         * Settings' own close button. */
                        web_win.open = 0;
                        win_z_remove(WIN_ID_WEB);
                    } else if (kind == BTN_WEB_GO) {
                        if (web_about_open) { if (web_tb_pressed == 99) web_about_open = 0; }
                        else web_tb_fire(web_tb_pressed);
                        web_tb_pressed = -1;
                    }
                } else if (win == WIN_ID_FILES) {
                    if (kind == BTN_MIN) {
                        win_minimize(WIN_ID_FILES);
                        status = t(STR_FILES_MINIMIZED);
                    } else if (kind == BTN_MAX) {
                        if (files_win.maximized) {
                            unmaximize_window(&files_win);
                            status = t(STR_FILES_RESTORED);
                        } else {
                            maximize_window(&files_win);
                            status = t(STR_FILES_MAXIMIZED);
                        }
                    } else if (kind == BTN_CLOSE) {
                        files_win.open = 0;
                        win_z_remove(WIN_ID_FILES);
                    }
                } else if (PW_IS(win)) {
                    pw_select(win);
                    if (kind == BTN_MIN) {
                        win_minimize(win);
                        status = "PROGRAM MINIMIZED";
                    } else if (kind == BTN_MAX) {
                        if (pw_win.maximized) { unmaximize_window(&pw_win); status = "PROGRAM RESTORED"; }
                        else { maximize_window(&pw_win); status = "PROGRAM MAXIMIZED"; }
                    } else if (kind == BTN_CLOSE) {
                        pw_close();               /* stops the program wherever it was waiting */
                    }
                } else if (win == WIN_ID_TERMINAL) {
                    if (kind == BTN_MIN) {
                        win_minimize(WIN_ID_TERMINAL);
                        status = t(STR_TERMINAL_MINIMIZED);
                    } else if (kind == BTN_MAX) {
                        if (term_win.maximized) {
                            unmaximize_window(&term_win);
                            status = t(STR_TERMINAL_RESTORED);
                        } else {
                            maximize_window(&term_win);
                            status = t(STR_TERMINAL_MAXIMIZED);
                        }
                    } else if (kind == BTN_CLOSE) {
                        /* Same as Setting/Web: no unsaved-changes
                         * concept for a scrollback log, so this
                         * closes immediately. */
                        term_win.open = 0;
                        term_session_end();
                        win_z_remove(WIN_ID_TERMINAL);
                    }
                } else {
                    if (kind == BTN_MIN) {
                        win_minimize(win);
                        status = t(STR_NOTEPAD_MINIMIZED);
                    } else if (kind == BTN_MAX) {
                        if (active_np->win.maximized) {
                            unmaximize_window(&active_np->win);
                            status = t(STR_NOTEPAD_RESTORED);
                        } else {
                            maximize_window(&active_np->win);
                            status = t(STR_NOTEPAD_MAXIMIZED);
                        }
                    } else if (kind == BTN_CLOSE) {
                        /* Ask before closing, same Yes/No pattern as New. */
                        active_np->confirm_mode = CONFIRM_CLOSE;
                        beep_warning();
                    }
                }
            }
            pressed_btn_kind = BTN_NONE;
            pressed_btn_win = -1;
        }
    }

}

/* One poll of the keyboard: routes the key to the topmost window (page scrolling, text entry, dialogs, shortcuts). */
static void handle_keyboard(void) {
    dlg_poll();                                                    /* (a question that was just asked has its window -- and the keyboard -- before the key is routed) */
    /* ---- keyboard: routed to whichever window is topmost in
     * z-order, if (and only if) that's a Notepad window -- Setting
     * has no text fields, and if the desktop itself is topmost (or
     * nothing is open at all), keystrokes just go nowhere. This is
     * also why raising a window on click matters beyond visuals:
     * the topmost window IS the keyboard focus. ---- */
    int k = keyboard_poll_key();
    if (oobe_open) { oobe_key(k); k = KEY_NONE; }                                                  /* ... and the keyboard */
    int focused_id = -1;
    for (int zi = z_count - 1; zi >= 0; zi--) {
        int id = z_order[zi];
        if (win_is_open(id) && !win_is_minimized(id)) { focused_id = id; break; }
    }

    /* Right Alt (the 한/영 key) switches the input method for EVERY text box, not just Notepad's: a syllable in progress is committed
     * first (Notepad's own composition too), then the next enabled IME is selected. */
    if (k == KEY_RALT && focused_id >= 0) {
        if (active_np && focused_id == active_np->id) np_commit_ime();      /* Notepad: the syllable goes in at the caret ... */
        tf_ime_flush(); web_ime_flush();                                    /* ... a text box's or a page field's is already in its text */
        ime_cycle_next();
        status = (current_ime == IME_KOREAN) ? t(STR_HANGUL_MODE_ON) : t(STR_ENGLISH_MODE_ON);
        k = KEY_NONE;
    }

    /* Scrolling the web page: Up/Down move three lines; with the URL bar NOT focused, Space pages
     * down and B pages up (the same keys every text-mode browser uses). The keys are consumed here
     * so they don't also reach the URL bar's text entry below. */
    if (focused_id == WIN_ID_WEB && k != KEY_NONE) {
        int page = web_view_h() - 28;
        if (page < 14) page = 14;
        if (!web_urlbar_focused && rd_focus && k > 0) {       /* a text field on the page has the keyboard */
            u32 typed_in = rd_focus;
            int r = web_ctl_key(k);                       /* (the Hangul IME in a page's text field: apps/web/view.h) */
            if (r == 1) js_dom_input(typed_in);          /* the field's text changed: fire "input" */
            if (r == 2) { u32 f = rd_find_form(rd_focus); if (f) web_submit_form(f, 0); }
            k = KEY_NONE;
        }
        else if (k == KEY_UP)   { web_scroll_by(-WEB_SCROLL_STEP); k = KEY_NONE; }
        else if (k == KEY_DOWN) { web_scroll_by(WEB_SCROLL_STEP);  k = KEY_NONE; }
        else if (!web_urlbar_focused && k == ' ')                { web_scroll_by(page);  k = KEY_NONE; }
        else if (!web_urlbar_focused && (k == 'b' || k == 'B'))  { web_scroll_by(-page); k = KEY_NONE; }
    }

    /* Settings (the Control Panel): arrows move between the icons, Enter opens one, Esc goes back from a page */
    if (focused_id == WIN_ID_SETTING) {
        if (dlg_blocker(WIN_ID_SETTING) < 0) setting_key(k);                   /* (disabled while one of its questions is open) */
        k = KEY_NONE;
    }

    /* MiniFiles: arrows select, Left/Right switch tabs, Enter opens/runs/installs */
    if (focused_id == WIN_ID_FILES) {
        files_key(k);
        k = KEY_NONE;
    }

    /* A program running in a window gets the keys while its window is in front (it reads them through sys.key_poll) */
    if (PW_IS(focused_id)) {
        pw_select(focused_id);
        pw_push_key(k);
        k = KEY_NONE;
    }

    /* A question (ui/dialog.h) in front: Notepad's Save changes? / Save As / Help boxes and MiniWeb's About box take the keys */
    if (DLG_IS(focused_id)) {
        dlg_key(focused_id, k);
        k = KEY_NONE;
    }

    /* Terminal scrollback: Up/Down move the view three rows back/forward (consumed here, like the web page above) */
    if (focused_id == WIN_ID_TERMINAL && (k == KEY_UP || k == KEY_DOWN)) {
        term_scroll_by(k == KEY_UP ? 3 : -3);
        k = KEY_NONE;
    }

    if (focused_id >= 0 && focused_id != WIN_ID_SETTING && focused_id != WIN_ID_WEB && focused_id != WIN_ID_TERMINAL && focused_id != WIN_ID_FILES && !PW_IS(focused_id) && !DLG_IS(focused_id)) {
        active_np = &notepads[focused_id];

    if (active_np->confirm_mode != CONFIRM_NONE) {
        /* disabled while its question is open (the question is a window of its own, taken care of above): its dialog is minimized, so the keys wait */
        (void)k;
    } else if (k == KEY_RALT && active_np->win.open && !active_np->win.minimized && mb_owner != active_np->id) {
        /* Right Alt cycles to the next ENABLED input method (see
         * SETTING.MWP > SYSTEM > IME) -- matches the 한/영 key
         * position on real Korean keyboards. F7 used to do this too,
         * but that's gone now that IME selection lives in Settings. */
        ime_cycle_next();
        status = (current_ime == IME_KOREAN) ? t(STR_HANGUL_MODE_ON) : t(STR_ENGLISH_MODE_ON);
    } else if (k < 0 && active_np->win.open && !active_np->win.minimized && mb_owner != active_np->id && np_key_nav(k)) {
        /* the caret keys: arrows, Home, End (Shift extends the selection), Delete -- handled in apps/notepad/menus.h */
    } else if (k > 0 && active_np->win.open && !active_np->win.minimized && mb_owner != active_np->id) {
        char c = (char)k;
        if (!keyboard_ctrl_held() && (c == '\n' || c == '\b' || (c >= 32 && c < 127) || current_ime == IME_KOREAN) && np_before_typing(c)) c = 0;   /* typing replaces a selection; Backspace over one just deletes it */
        if (c == 0) { /* handled */ } else
        if (keyboard_ctrl_held()) {
            /* Ctrl+S / Ctrl+N / Ctrl+W accelerators, mirroring the
             * File menu and the title bar's close button */
            np_commit_ime();
            if (np_ctrl_key(c)) {
                /* Ctrl+Z/X/C/V/A: Edit menu accelerators (apps/notepad/menus.h) */
            } else if (c == 's' || c == 'S') {
                int saved_slot = save_current_document();
                status = saved_slot >= 0 ? format_saved_status(saved_slot) : save_error_status(saved_slot);
            } else if (c == 'n' || c == 'N') {
                active_np->confirm_mode = CONFIRM_NEW;
                beep_warning();
            } else if (c == 'w' || c == 'W') {
                active_np->confirm_mode = CONFIRM_CLOSE;
                beep_warning();
            }
        } else if (c == '\b') {
            if (current_ime == IME_KOREAN && ko_ime_backspace()) {
                /* consumed by the IME: undid one step of the syllable
                 * currently being composed, nothing else to do */
            } else if (active_np->caret > 0) {
                np_del_range(np_char_back(active_np->caret), active_np->caret);          /* Backspace: the character before the caret */
            }
        } else {
            u32 nb = active_np->text_len;               /* typing appends at the end, then np_rotate_new() moves it to the caret */
            if (current_ime == IME_KOREAN && ko_ime_feed_key(c, active_np->text_buf, &active_np->text_len, sizeof(active_np->text_buf))) {
                /* consumed as a jamo keystroke -- composition state updated (and/or a completed syllable appended) inside ko_ime_feed_key() */
            } else if (c == '\n' || c == ' ' || (c >= 32 && c < 127)) {
                /* a non-jamo key (space, enter, punctuation) always flushes an in-progress syllable first, like every real Hangul IME */
                if (current_ime == IME_KOREAN && ko_ime_is_composing()) ko_ime_commit(active_np->text_buf, &active_np->text_len, sizeof(active_np->text_buf));
                kstrcpy_append(active_np->text_buf, &active_np->text_len, sizeof(active_np->text_buf), c);
            }
            np_rotate_new(nb);
        }
    }
    } else if (focused_id == WIN_ID_WEB && web_urlbar_focused && k != KEY_NONE) {
        /* URL bar text entry (ui/textfield.h): caret, selection, clipboard and the Hangul IME -- a Hangul search phrase is typed right
         * here and goes out percent-encoded (web_urlencode). Enter submits, exactly like every browser's address bar. */
        if (k == '\n') {
            tf_ime_flush();
            web_go_url();
        } else if (k != KEY_NONE) {
            tf_key(&web_urlbar_tf, web_urlbar_buf, sizeof web_urlbar_buf, TF_ASCII, k, web_urlbar_sync);
        }
    } else if (focused_id == WIN_ID_TERMINAL && term_input_focused && k != KEY_NONE) {
        /* Command-line text entry (ui/textfield.h): caret, selection, clipboard and the Hangul IME. Enter runs whatever is in
         * term_input_buf (term_run_input() itself clears the buffer once it's done). */
        if (k == '\n') {
            tf_ime_flush();
            term_run_input();
        } else {
            tf_key(&term_input_tf, term_input_buf, sizeof term_input_buf, term_input_flags(), k, term_input_sync);
        }
    }

}

#endif
