/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_UI_CTXMENU_ACTIONS_H
#define MW_UI_CTXMENU_ACTIONS_H

/* ui/ctxmenu_actions.h -- what the right-click menus hold and what their items do (the machinery is ui/ctxmenu.h).
 *
 * Three places have a menu: the desktop (empty part), a desktop icon, and MiniFiles (a file or folder row, or the empty part of its list).
 * They look like the Windows 95 ones. An item is only live when the thing it names really exists in MiniWin: Open, Cut/Copy/Paste, Delete,
 * Rename, New Folder, Refresh, the View choices (MiniFiles); Open, New > Shortcut / Text Document, Properties (desktop). Send To, Add to Zip,
 * Paste Shortcut, Undo Delete, Arrange / Line up Icons, ... are drawn greyed -- there is no feature behind them yet, and a menu that pretended
 * would be worse than one that says so. Needs MiniFiles (minifiles.h), the wizard (apps/startmenu_add.h) and the desktop icons. */

enum { PM_DESKTOP = 1, PM_ICON, PM_FILE_ITEM, PM_FILE_BG, PM_NOTEPAD, PM_PROGRAM, PM_TEXTFIELD, PM_WEB, PM_TERM, PM_SETTING };
enum { PMI_NONE = 0, PMI_OPEN, PMI_NEW_NOTE, PMI_NEW_SHORTCUT, PMI_PROPS_DISPLAY,
       PMI_FCUT, PMI_FCOPY, PMI_FPASTE, PMI_FDELETE, PMI_FRENAME, PMI_FNEWFOLDER, PMI_FREFRESH, PMI_FVIEW_FOLDERS, PMI_FVIEW_DISKS, PMI_FSORT0 = 40 /* +0..3 */,
       PMI_NUNDO = 50, PMI_NCUT, PMI_NCOPY, PMI_NPASTE, PMI_NDELETE, PMI_NSELALL,
       PMI_TCUT = 60, PMI_TCOPY, PMI_TPASTE, PMI_TDELETE, PMI_TSELALL,
       PMI_WOPEN = 70, PMI_WCOPYLINK, PMI_WBACK, PMI_WFORWARD, PMI_WREFRESH, PMI_WHOME,
       PMI_SCOPYALL = 80, PMI_SPASTE, PMI_SCLEAR,                          /* MiniShell's output */
       PMI_COPEN = 90, PMI_FNTOPEN, PMI_FNTUSE, PMI_FNTINSTALL };           /* Control Panel: an icon, a font, the Fonts folder */   /* MiniWeb's page */      /* the one-line text fields (ui/textfield.h) */

/* ---- the desktop ---- */
static const pm_item_t pmd_new[] = {
    { "Folder", "폴더", MBF_OFF, 0, "Creates a new folder.", 0, 0 },
    { "Shortcut", "바로 가기", 0, PMI_NEW_SHORTCUT, "Creates a shortcut in the Start menu.", 0, 0 },
    { "Text Document", "텍스트 문서", 0, PMI_NEW_NOTE, "Opens a new, empty Notepad document.", 0, 0 },
};
static const pm_item_t pm_desk_bg[] = {
    { "Arrange Icons", "아이콘 정렬", MBF_OFF, 0, "Arranges the icons on the desktop.", 0, 0 },
    { "Line up Icons", "아이콘 줄 맞추기", MBF_OFF, 0, "Lines the icons up on the grid.", 0, 0 },
    { "Paste", "붙여넣기", MBF_SEP | MBF_OFF, 0, "Inserts the contents of the Clipboard.", 0, 0 },
    { "Paste Shortcut", "바로 가기 붙여넣기", MBF_OFF, 0, "Inserts a shortcut to the copied item.", 0, 0 },
    { "Undo Delete", "삭제 취소", MBF_OFF, 0, "Undoes the last delete.", 0, 0 },
    { "New", "새로 만들기", MBF_SEP | PMF_SUB, 0, "Creates a new item.", pmd_new, 3 },
    { "Properties", "속성", MBF_SEP, PMI_PROPS_DISPLAY, "Shows the properties of the display.", 0, 0 },
};
static const pm_item_t pm_desk_icon[] = {
    { "Open", "열기", MBF_BOLD, PMI_OPEN, "Opens the selected item.", 0, 0 },
    { "Add to Zip", "압축하기", MBF_OFF, 0, "Adds the item to a Zip file.", 0, 0 },
    { "Send To", "보내기", MBF_SEP | MBF_OFF, 0, "Sends the item to another place.", 0, 0 },
    { "Cut", "잘라내기", MBF_SEP | MBF_OFF, 0, "Cuts the selected item to the Clipboard.", 0, 0 },
    { "Copy", "복사", MBF_OFF, 0, "Copies the selected item to the Clipboard.", 0, 0 },
    { "Create Shortcut", "바로 가기 만들기", MBF_SEP | MBF_OFF, 0, "Creates a shortcut to the selected item.", 0, 0 },
    { "Delete", "삭제", MBF_OFF, 0, "Deletes the selected item.", 0, 0 },
    { "Rename", "이름 바꾸기", MBF_OFF, 0, "Renames the selected item.", 0, 0 },
    { "Properties", "속성", MBF_SEP | MBF_OFF, 0, "Shows the properties of the selected item.", 0, 0 },
};

/* ---- MiniFiles ---- */
static const pm_item_t pmf_view[] = {
    { "Folders", "폴더", 0, PMI_FVIEW_FOLDERS, "Shows the folders and files.", 0, 0 },
    { "Disks", "디스크", 0, PMI_FVIEW_DISKS, "Shows the disks and their partitions.", 0, 0 },
    { "by Name", "이름순", MBF_SEP, PMI_FSORT0 + 0, "Sorts the list by name.", 0, 0 },
    { "by Size", "크기순", 0, PMI_FSORT0 + 1, "Sorts the list by size.", 0, 0 },
    { "by Type", "종류순", 0, PMI_FSORT0 + 2, "Sorts the list by type.", 0, 0 },
    { "by Date", "날짜순", 0, PMI_FSORT0 + 3, "Sorts the list by date.", 0, 0 },
};
static const pm_item_t pmf_new[] = {
    { "Folder", "폴더", 0, PMI_FNEWFOLDER, "Creates a new folder in this folder.", 0, 0 },
};
static const pm_item_t pm_file_item[] = {
    { "Open", "열기", MBF_BOLD, PMI_OPEN, "Opens the selected item.", 0, 0 },
    { "Send To", "보내기", MBF_SEP | MBF_OFF, 0, "Sends the selected item to another place.", 0, 0 },
    { "Cut", "잘라내기", MBF_SEP, PMI_FCUT, "Cuts the selected items to the Clipboard.", 0, 0 },
    { "Copy", "복사", 0, PMI_FCOPY, "Copies the selected items to the Clipboard. To put them in the new location, use the Paste command.", 0, 0 },
    { "Create Shortcut", "바로 가기 만들기", MBF_SEP | MBF_OFF, 0, "Creates a shortcut to the selected item.", 0, 0 },
    { "Delete", "삭제", 0, PMI_FDELETE, "Deletes the selected items.", 0, 0 },
    { "Rename", "이름 바꾸기", 0, PMI_FRENAME, "Renames the selected item.", 0, 0 },
    { "Properties", "속성", MBF_SEP | MBF_OFF, 0, "Shows the properties of the selected item.", 0, 0 },
};
static const pm_item_t pm_file_bg[] = {
    { "View", "보기", PMF_SUB, 0, "Changes how the list is shown.", pmf_view, 6 },
    { "Refresh", "새로 고침", MBF_SEP, PMI_FREFRESH, "Reads this folder again.", 0, 0 },
    { "Paste", "붙여넣기", MBF_SEP, PMI_FPASTE, "Inserts the contents of the Clipboard.", 0, 0 },
    { "New", "새로 만들기", MBF_SEP | PMF_SUB, 0, "Creates a new item.", pmf_new, 1 },
    { "Properties", "속성", MBF_SEP | MBF_OFF, 0, "Shows the properties of this folder.", 0, 0 },
};

/* ---- Notepad's text area: what is greyed depends on the selection, the Clipboard and the undo snapshot ---- */
static const pm_item_t pm_notepad[] = {
    { "Undo", "실행 취소", 0, PMI_NUNDO, "Undoes the last edit (again: redoes it).", 0, 0 },
    { "Cut", "잘라내기", MBF_SEP, PMI_NCUT, "Cuts the selected text to the Clipboard.", 0, 0 },
    { "Copy", "복사", 0, PMI_NCOPY, "Copies the selected text to the Clipboard.", 0, 0 },
    { "Paste", "붙여넣기", 0, PMI_NPASTE, "Inserts the Clipboard at the cursor (or over the selection).", 0, 0 },
    { "Delete", "삭제", 0, PMI_NDELETE, "Deletes the selected text.", 0, 0 },
    { "Select All", "모두 선택", MBF_SEP, PMI_NSELALL, "Selects all the text.", 0, 0 },
};

/* A one-line text field (MiniFiles' rename box, MiniWeb's address bar, MiniShell's command line, Notepad's Save As). */
static const pm_item_t pm_textfield[] = {
    { "Cut", "잘라내기", 0, PMI_TCUT, "Cuts the selected text to the Clipboard.", 0, 0 },
    { "Copy", "복사", 0, PMI_TCOPY, "Copies the selected text to the Clipboard.", 0, 0 },
    { "Paste", "붙여넣기", 0, PMI_TPASTE, "Inserts the Clipboard at the cursor (or over the selection).", 0, 0 },
    { "Delete", "삭제", 0, PMI_TDELETE, "Deletes the selected text.", 0, 0 },
    { "Select All", "모두 선택", MBF_SEP, PMI_TSELALL, "Selects all the text.", 0, 0 },
};
/* ---- MiniWeb's page: on a link, and on the bare page ---- */
static const pm_item_t pm_web_link[] = {
    { "Open Link", "링크 열기", MBF_BOLD, PMI_WOPEN, "Goes to the address this link points to.", 0, 0 },
    { "Copy Link Address", "링크 주소 복사", 0, PMI_WCOPYLINK, "Copies the full address of the link to the Clipboard.", 0, 0 },
    { "Back", "뒤로", MBF_SEP, PMI_WBACK, "Goes back to the previous page.", 0, 0 },
    { "Forward", "앞으로", 0, PMI_WFORWARD, "Goes forward to the next page.", 0, 0 },
    { "Refresh", "새로 고침", 0, PMI_WREFRESH, "Loads this page again.", 0, 0 },
};
/* ---- MiniShell's output (the command line has the text-field menu) ---- */
static const pm_item_t pm_term[] = {
    { "Copy All", "모두 복사", 0, PMI_SCOPYALL, "Copies everything in this window to the Clipboard (the newest 4096 bytes if there is more).", 0, 0 },
    { "Paste", "붙여넣기", 0, PMI_SPASTE, "Inserts the Clipboard into the command line.", 0, 0 },
    { "Clear", "지우기", MBF_SEP, PMI_SCLEAR, "Empties this window.", 0, 0 },
};
/* ---- Control Panel: an icon of the grid, a font of the Fonts folder, the bare Fonts folder ---- */
static const pm_item_t pm_cp_icon[] = {
    { "Open", "열기", MBF_BOLD, PMI_COPEN, "Opens this item.", 0, 0 },
};
static const pm_item_t pm_fonts_item[] = {
    { "Open", "열기", MBF_BOLD, PMI_FNTOPEN, "Shows this font.", 0, 0 },
    { "Use as System Font", "시스템 글꼴로 사용", 0, PMI_FNTUSE, "Makes this font the font of the whole desktop.", 0, 0 },
    { "Install New Font...", "새 글꼴 설치...", MBF_SEP, PMI_FNTINSTALL, "Adds TrueType fonts from a folder or a disk.", 0, 0 },
};
static const pm_item_t pm_fonts_bg[] = {
    { "Install New Font...", "새 글꼴 설치...", 0, PMI_FNTINSTALL, "Adds TrueType fonts from a folder or a disk.", 0, 0 },
};
static const pm_item_t pm_web_page[] = {
    { "Back", "뒤로", 0, PMI_WBACK, "Goes back to the previous page.", 0, 0 },
    { "Forward", "앞으로", 0, PMI_WFORWARD, "Goes forward to the next page.", 0, 0 },
    { "Refresh", "새로 고침", MBF_SEP, PMI_WREFRESH, "Loads this page again.", 0, 0 },
    { "Home", "홈", 0, PMI_WHOME, "Goes to the home page.", 0, 0 },
};
static char pm_web_href[WEB_PATH_MAX];                                  /* the link the menu was opened on */
enum { TFT_NONE = 0, TFT_WEB, TFT_TERM, TFT_SAVEAS, TFT_FILES };
static int pm_tf_kind = TFT_NONE;                                       /* which field the open menu belongs to */
static notepad_t *pm_tf_np = 0;                                         /* ... and, for Save As, which Notepad */
/* The field, its buffer, capacity and flags for the menu that is open. */
static tf_t *pm_tf_target(char **buf, u32 *cap, int *fl) {
    switch (pm_tf_kind) {
    case TFT_WEB:    *buf = web_urlbar_buf; *cap = sizeof web_urlbar_buf; *fl = TF_ASCII; return &web_urlbar_tf;
    case TFT_TERM:   *buf = term_input_buf; *cap = sizeof term_input_buf; *fl = term_input_flags(); return &term_input_tf;
    case TFT_SAVEAS: if (!pm_tf_np) return 0; *buf = pm_tf_np->saveas_name; *cap = sizeof pm_tf_np->saveas_name; *fl = TF_NAME; return &pm_tf_np->saveas_tf;
    case TFT_FILES:  *buf = fx_edit_buf; *cap = sizeof fx_edit_buf; *fl = TF_NAME; return &fx_tf;
    }
    return 0;
}
/* The owners keep a separate length next to the buffer: bring it back in step after the menu changed the text. */
static void pm_tf_sync(void) {
    if (pm_tf_kind == TFT_WEB) web_urlbar_len = tf_len(web_urlbar_buf);
    else if (pm_tf_kind == TFT_TERM) term_input_len = tf_len(term_input_buf);
    else if (pm_tf_kind == TFT_SAVEAS && pm_tf_np) pm_tf_np->saveas_len = (int)tf_len(pm_tf_np->saveas_name);
    else if (pm_tf_kind == TFT_FILES) fx_edit_len = (int)tf_len(fx_edit_buf);
}
static void pm_tf_run(int id) {
    char *buf; u32 cap; int fl;
    tf_t *t = pm_tf_target(&buf, &cap, &fl);
    if (!t) return;
    tf_fix(t, buf);
    if (id == PMI_TCUT) tf_cut(t, buf, fl);
    else if (id == PMI_TCOPY) tf_copy(t, buf, fl);
    else if (id == PMI_TPASTE) tf_paste(t, buf, cap, fl);
    else if (id == PMI_TDELETE) tf_del_sel(t, buf);
    else if (id == PMI_TSELALL) tf_select_all(t, buf);
    pm_tf_sync();
}
static void pm_tf_open(int kind, int px, int py) {
    tf_ime_flush();                                         /* the syllable in progress is committed before the menu acts on the text */
    pm_tf_kind = kind;
    char *buf; u32 cap; int fl;
    tf_t *t = pm_tf_target(&buf, &cap, &fl);
    if (!t) return;
    tf_fix(t, buf);
    int sel = tf_has_sel(t), pass = (fl & TF_PASS) != 0;
    pm_begin(pm_textfield, 5, px, py, PM_TEXTFIELD, 0);
    pm_set_off(0, !sel || pass); pm_set_off(1, !sel || pass);           /* a password cannot be cut or copied */
    pm_set_off(2, !clip_len); pm_set_off(3, !sel); pm_set_off(4, !tf_len(buf));
}

/* ---- the actions ---- */
static const char *pm_new_notepad(void) {
    int slot = find_free_notepad_slot();
    if (slot < 0) return t(STR_ALL_NOTEPAD_WINDOWS_OPEN);
    active_np = &notepads[slot];
    active_np->win.open = 1; active_np->win.minimized = 0;
    active_np->text_len = 0; active_np->text_buf[0] = 0;
    active_np->bound_slot = -1; active_np->file_menu_open = 0; active_np->confirm_mode = CONFIRM_NONE;
    ko_ime_reset();
    win_z_raise(slot);
    return t(STR_NOTEPAD_OPENED);
}
static void pm_show_setting(int page) {
    setting.open = 1;
    if (win_is_minimized(WIN_ID_SETTING)) win_restore(WIN_ID_SETTING);
    win_z_raise(WIN_ID_SETTING);
    setting_sel = page; setting_open_item(page);
}
/* Opens a desktop icon: 0..4 the built-in ones (Notepad, Setting, MiniWeb, MiniShell, MiniFiles), 5 INSTALL, 100+n the n-th shortcut. */
static void pm_open_icon(int a) {
    if (a == 0) pm_status_msg = pm_new_notepad();
    else if (a == 1) { setting.open = 1; win_z_raise(WIN_ID_SETTING); pm_status_msg = t(STR_SETTING_OPENED); }
    else if (a == 2) { web_win.open = 1; web_opened(); win_z_raise(WIN_ID_WEB); pm_status_msg = t(STR_WEB_OPENED); }
    else if (a == 3) { term_win.open = 1; win_z_raise(WIN_ID_TERMINAL); pm_status_msg = t(STR_TERMINAL_OPENED); }
    else if (a == 4) { files_open_window(); pm_status_msg = t(STR_FILES_OPENED); }
    else if (a == 5) { const char *why = installer_open(); if (why) pm_status_msg = why; }
    else if (a >= 100) {
        int kind, slot;
        if (!desk_item(a - 100, &kind, &slot)) return;
        if (kind == DESK_KIND_DOC) open_document_slot(slot);
        else { const char *why = pw_launch(slot, prog_name_of(slot)); if (why) pm_status_msg = why; }
    }
}

static void pm_run(int kind, int arg, int id) {
    if (kind == PM_PROGRAM) { pm_prog_result = id; return; }                 /* a program's own menu: it is waiting in menu_popup() for this */
    switch (id) {
    case PMI_OPEN:
        if (kind == PM_ICON) pm_open_icon(arg);
        else if (kind == PM_FILE_ITEM) fx_open_row(arg);
        break;
    case PMI_NEW_NOTE: pm_status_msg = pm_new_notepad(); break;
    case PMI_NEW_SHORTCUT: { const char *why = startmenu_add_open(); if (why) pm_status_msg = why; break; }
    case PMI_PROPS_DISPLAY: pm_show_setting(SP_DISPLAY); break;
    case PMI_FCUT: fx_copy_action(1); break;
    case PMI_FCOPY: fx_copy_action(0); break;
    case PMI_FPASTE: fx_paste_action(); break;
    case PMI_FDELETE: fx_delete_action(); break;
    case PMI_FRENAME: fx_begin_rename(); break;
    case PMI_FNEWFOLDER: fx_new_folder(); break;
    case PMI_TCUT: case PMI_TCOPY: case PMI_TPASTE: case PMI_TDELETE: case PMI_TSELALL: pm_tf_run(id); break;
    case PMI_NUNDO: case PMI_NCUT: case PMI_NCOPY: case PMI_NPASTE: case PMI_NDELETE: case PMI_NSELALL:
        active_np = &notepads[arg];
        if (id == PMI_NUNDO) np_edit_undo(); else if (id == PMI_NCUT) np_edit_copy(1); else if (id == PMI_NCOPY) np_edit_copy(0);
        else if (id == PMI_NPASTE) np_edit_paste(); else if (id == PMI_NDELETE) np_edit_delete(); else np_edit_select_all();
        break;
    case PMI_SCOPYALL: term_copy_all(); break;
    case PMI_SPASTE: pm_tf_kind = TFT_TERM; term_input_focused = 1; pm_tf_run(PMI_TPASTE); break;
    case PMI_SCLEAR: term_clear_output(); break;
    case PMI_COPEN: setting_sel = arg; setting_open_item(arg); break;
    case PMI_FNTOPEN: fnt_sel = arg; fnt_msg = 0; fnt_open_preview(); break;
    case PMI_FNTUSE: fnt_sel = arg; fnt_msg = 0; if (fnt_can_use()) fnt_apply(fnt_idx()); break;
    case PMI_FNTINSTALL: { sheet_t g; fnt_geo(&g); if (g.ok) afd_open_dialog(&g); break; }
    case PMI_WOPEN: web_urlbar_focused = 0; web_navigate_ref(pm_web_href); break;
    case PMI_WCOPYLINK: { char u[WEB_PATH_MAX + 120]; if (web_ref_to_url(pm_web_href, u, sizeof(u))) clip_set(u, tf_len(u)); break; }
    case PMI_WBACK: web_tb_fire(0); break;
    case PMI_WFORWARD: web_tb_fire(1); break;
    case PMI_WREFRESH: web_tb_fire(3); break;
    case PMI_WHOME: web_tb_fire(4); break;
    case PMI_FREFRESH: fx_refresh_all(); files_set_msg("Refreshed.");  break;
    case PMI_FVIEW_FOLDERS: fx_set_view(0); break;
    case PMI_FVIEW_DISKS: fx_set_view(1); break;
    default:
        if (id >= PMI_FSORT0 && id < PMI_FSORT0 + 4) fx_sort_by(id - PMI_FSORT0);
        break;
    }
}

/* ---- a program's own pop-up menu (API version 4, table entry menu_popup) ---- */
static char pm_prog_text[424];
static pm_item_t pm_prog_items[PM_MAX];
/* items: entries separated by '\n'; leading '-' = a separator above, '!' = greyed, '*' = bold. (x, y) is in the program window's drawing area. Waits like
 * present(): the desktop runs on while the menu is open. Returns the chosen entry's index, or -1. */
static int mwp_sys_menu_popup(int x, int y, const char *items) {
    if (!pw_cur || !items) return -1;
    u32 len = 0;
    while (items[len] && len < sizeof pm_prog_text - 1) { pm_prog_text[len] = items[len]; len++; }
    pm_prog_text[len] = 0;
    int n = 0;
    for (char *p = pm_prog_text; *p && n < PM_MAX; n++) {
        int fl = 0;
        for (;; p++) { if (*p == '-') fl |= MBF_SEP; else if (*p == '!') fl |= MBF_OFF; else if (*p == '*') fl |= MBF_BOLD; else break; }
        char *label = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') *p++ = 0;
        pm_prog_items[n].en = label; pm_prog_items[n].ko = 0; pm_prog_items[n].fl = fl; pm_prog_items[n].id = n + 1;
        pm_prog_items[n].help = 0; pm_prog_items[n].sub = 0; pm_prog_items[n].sub_n = 0;
    }
    if (!n) return -1;
    pm_prog_result = 0;
    int me = pw_sel;
    pm_begin(pm_prog_items, n, pw_cx() + x, pw_cy() + y, PM_PROGRAM, 0);
    pm_prog_owner = me;
    while (pm_open && pm_kind == PM_PROGRAM && pm_prog_owner == me) pw_yield();      /* (another program's menu replacing this one ends the wait) */
    return (pm_open && pm_kind == PM_PROGRAM && pm_prog_owner != me) ? -1 : pm_prog_result - 1;
}

/* ---- opening a menu: the right mouse button went down at (px,py) ---- */
static void pm_files_context(int px, int py) {
    win_z_raise(WIN_ID_FILES);
    if (fx_edit && fx_rename_hit(px, py)) { mb_close(); pm_tf_open(TFT_FILES, px, py); return; }   /* on the rename box: its own menu */
    if (fx_edit) fx_commit_rename();
    mb_close();
    if (in_rect(px, py, files_list_x(), files_list_y(), files_list_w(), files_list_h())) {
        int row = (py - (files_list_y() + 2)) / FX_ROW_H + files_scroll;
        if (py >= files_list_y() + 2 && row >= 0 && row < files_row_count) {
            fx_select(row);
            pm_begin(pm_file_item, 8, px, py, PM_FILE_ITEM, row);
            int can = fx_btn_enabled(FB_DELETE);                      /* Delete and Rename follow the same rule: not on protected folders or disks */
            pm_set_off(2, !fx_btn_enabled(FB_CUT)); pm_set_off(3, !fx_btn_enabled(FB_COPY));
            pm_set_off(5, !can); pm_set_off(6, !can);
            return;
        }
        pm_begin(pm_file_bg, 5, px, py, PM_FILE_BG, 0);
        pm_set_off(2, !fx_btn_enabled(FB_PASTE));
        pm_set_off(3, fx_view_disks);                                   /* no new folder in the list of disks */
    }
}

static void pm_notepad_context(int px, int py, int id) {
    active_np = &notepads[id];
    win_z_raise(id);
    if (active_np->confirm_mode == CONFIRM_SAVEAS && saveas_field_hit(px, py)) { mb_close(); pm_tf_np = active_np; pm_tf_open(TFT_SAVEAS, px, py); return; }
    if (!np_edit_hit(px, py) || active_np->confirm_mode != CONFIRM_NONE) return;
    mb_close();
    pm_begin(pm_notepad, 6, px, py, PM_NOTEPAD, id);
    pm_set_off(0, !active_np->undo_valid); pm_set_off(1, !np_has_sel()); pm_set_off(2, !np_has_sel());
    pm_set_off(3, !clip_len); pm_set_off(4, !np_has_sel()); pm_set_off(5, !active_np->text_len);
}

/* A right click on MiniWeb's page: a link has its own menu, the bare page a short one. (Elsewhere in the window: none.) */
static void pm_web_context(int px, int py) {
    int bx, by, bw, bh;
    web_body_geom(&bx, &by, &bw, &bh);
    if (!in_rect(px, py, bx, by, bw, bh)) return;
    win_z_raise(WIN_ID_WEB); mb_close(); web_urlbar_focused = 0;
    if (web_link_at(px, py, pm_web_href, sizeof(pm_web_href))) {
        pm_begin(pm_web_link, 5, px, py, PM_WEB, 0);
        pm_set_off(2, !web_tb_enabled(0)); pm_set_off(3, !web_tb_enabled(1)); pm_set_off(4, !web_tb_enabled(3));
    } else {
        pm_begin(pm_web_page, 4, px, py, PM_WEB, 0);
        pm_set_off(0, !web_tb_enabled(0)); pm_set_off(1, !web_tb_enabled(1)); pm_set_off(2, !web_tb_enabled(3));
    }
}

/* MiniShell: the command line has the text-field menu; the black output area its own (Copy All, Paste, Clear). */
static void pm_term_context(int px, int py) {
    win_z_raise(WIN_ID_TERMINAL); mb_close();
    if (!term_output_hit(px, py)) return;
    pm_begin(pm_term, 3, px, py, PM_TERM, 0);
    pm_set_off(0, term_scroll_count <= 0); pm_set_off(1, !clip_len); pm_set_off(2, term_scroll_count <= 0);
}
/* Control Panel: on an icon of the grid, on a font (or the empty part) of the Fonts folder. Open pages have no menu. */
static void pm_setting_context(int px, int py) {
    win_z_raise(WIN_ID_SETTING); mb_close();
    if (afd_on) return;                                                 /* the Add Fonts dialog is modal */
    if (setting_page < 0) {
        cp_grid_t g; cp_grid(&g);
        int i = cp_cell_at(&g, px, py);
        if (i >= 0) { setting_sel = i; pm_begin(pm_cp_icon, 1, px, py, PM_SETTING, i); }
        return;
    }
    if (setting_page == SP_TEXT && fnt_view == 0) {
        int i = fnt_item_at(px, py);
        if (i >= 0) { fnt_sel = i; fnt_msg = 0; pm_begin(pm_fonts_item, 3, px, py, PM_SETTING, i); pm_set_off(1, !fnt_can_use()); }
        else if (fnt_folder_hit(px, py)) pm_begin(pm_fonts_bg, 1, px, py, PM_SETTING, -1);
    }
}

static void pm_context_at(int px, int py) {
    pm_close();
    if (start_menu_open || clock_popup_open || py >= TASKBAR_Y) return;
    int hit_id; (void)compute_hover_resize_zone(px, py, &hit_id);
    if (hit_id == WIN_ID_FILES) { pm_files_context(px, py); return; }
    if (hit_id >= 0 && hit_id < NOTEPAD_MAX) { if (notepads[hit_id].confirm_mode == CONFIRM_NONE) pm_notepad_context(px, py, hit_id); return; }       /* (disabled while its question is open) */
    if (hit_id == WIN_ID_WEB && web_about_open) return;                 /* the About box is open: the browser is disabled */
    if (hit_id == WIN_ID_WEB && web_urlbar_hit(px, py)) { win_z_raise(WIN_ID_WEB); mb_close(); web_urlbar_focused = 1; rd_focus = 0; pm_tf_open(TFT_WEB, px, py); return; }
    if (hit_id == WIN_ID_WEB) { pm_web_context(px, py); return; }
    if (hit_id == WIN_ID_TERMINAL && term_input_hit(px, py)) { win_z_raise(WIN_ID_TERMINAL); mb_close(); term_input_focused = 1; pm_tf_open(TFT_TERM, px, py); return; }
    if (hit_id == WIN_ID_TERMINAL) { pm_term_context(px, py); return; }
    if (hit_id == WIN_ID_SETTING) { pm_setting_context(px, py); return; }
    if (hit_id >= 0) return;                                            /* the other windows have no menu yet */

    int icon = -1;                                                      /* the desktop: first an icon... */
    if (in_rect(px, py, ICON_X, ICON_Y, ICON_W, ICON_H)) icon = 0;
    else if (in_rect(px, py, ICON2_X, ICON2_Y, ICON2_W, ICON2_H)) icon = 1;
    else if (in_rect(px, py, ICON3_X, ICON3_Y, ICON3_W, ICON3_H)) icon = 2;
    else if (in_rect(px, py, ICON4_X, ICON4_Y, ICON4_W, ICON4_H)) icon = 3;
    else if (in_rect(px, py, ICON5_X, ICON5_Y, ICON5_W, ICON5_H)) icon = 4;
    else if (live_boot && in_rect(px, py, ICONI_X, ICONI_Y, ICON_SLOT_W, ICON_H)) icon = 5;
    else {
        int kind, slot;
        for (int n = 0; desk_item(n, &kind, &slot); n++)
            if (in_rect(px, py, desk_item_x(n), desk_item_y(n), ICON_W, ICON_H)) { icon = 100 + n; break; }
    }
    if (icon >= 0) pm_begin(pm_desk_icon, 9, px, py, PM_ICON, icon);
    else pm_begin(pm_desk_bg, 7, px, py, PM_DESKTOP, 0);                /* ... otherwise the bare desktop */
}

#endif
