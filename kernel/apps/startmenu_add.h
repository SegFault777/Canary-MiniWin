/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_APPS_STARTMENU_ADD_H
#define MW_APPS_STARTMENU_ADD_H

/* apps/startmenu_add.h -- start-menu shortcuts and folders (1.1-alpha-14): running a shortcut, and the "Add..." wizard.
 *
 * A shortcut is a small text file NAME.LNK somewhere under /home/start on the root volume: "app:NOTEPAD" (also SETTING, WEB, TERM, FILES)
 * for a built-in program, "prog:NAME.MWP" for a file in /progs. A folder of the Programs menu is a directory under /home/start. The
 * menus themselves (ui/startmenu.h) only read them.
 *
 * The wizard follows the Windows 95 "Select Program Folder" dialog: step 1 picks the program, step 2 picks the folder -- a tree
 * that starts at "Start Menu", with a New Folder... button -- and Finish writes the shortcut. Folders may be nested two deep (that is
 * how many flyouts the menu has). It runs as a window of the program-window machinery (apps/progwin.h pw_launch_fn), like the
 * installer, so it needs no window code of its own; apps/installer.h's little helpers (ins_button, ins_wrap) draw its buttons and text.
 * On a live disc the root volume is in RAM: the shortcuts last until the next boot. */

/* ---------------------------------------------------------------- running a shortcut */
static int sma_prefix(const char *s, const char *p) { while (*p) { if (*s++ != *p++) return 0; } return 1; }

/* 0 = started, otherwise a sentence for the status line */
static const char *sm_launch(const char *lnk) {
    u8 b[48]; u32 len = 0;
    if (vfs_read(lnk, b, sizeof b - 1, &len, 0) != MWFS_OK) return "The shortcut cannot be read.";
    b[len] = 0;
    const char *q = (const char *)b;
    if (sma_prefix(q, "app:")) {
        q += 4;
        if (sma_prefix(q, "NOTEPAD")) {
            int slot = find_free_notepad_slot();
            if (slot < 0) return t(STR_ALL_NOTEPAD_WINDOWS_OPEN);
            active_np = &notepads[slot];
            active_np->win.open = 1; active_np->win.minimized = 0;
            active_np->text_len = 0; active_np->text_buf[0] = 0;
            active_np->bound_slot = -1; active_np->file_menu_open = 0; active_np->confirm_mode = CONFIRM_NONE;
            ko_ime_reset();
            win_z_raise(slot);
            return 0;
        }
        if (sma_prefix(q, "SETTING")) { setting.open = 1; win_z_raise(WIN_ID_SETTING); return 0; }
        if (sma_prefix(q, "WEB"))     { web_win.open = 1; web_opened(); win_z_raise(WIN_ID_WEB); return 0; }
        if (sma_prefix(q, "TERM"))    { term_win.open = 1; win_z_raise(WIN_ID_TERMINAL); return 0; }
        if (sma_prefix(q, "FILES"))   { files_open_window(); return 0; }
        return "That shortcut points to an unknown program.";
    }
    if (sma_prefix(q, "prog:")) {
        q += 5;
        int slot = prog_find_by_name(q);
        if (slot < 0) return "That program is not installed.";
        return pw_launch(slot, q);
    }
    return "That shortcut is not understood.";
}

/* ---------------------------------------------------------------- the wizard */
#define SMA_ROWS 28
enum { SMA_PICK, SMA_FOLDER, SMA_DONE };
typedef struct { char label[34]; char target[40]; } sma_prog_t;
typedef struct { char path[112]; char name[34]; int depth; } sma_dir_t;
static sma_prog_t *sma_prog;                 /* SMA_ROWS each, on the heap (sys/boot.h boot_heap_init) */
static sma_dir_t  *sma_dir;
static int  sma_np, sma_nd, sma_psel, sma_dsel, sma_ptop, sma_dtop, sma_page, sma_naming, sma_new_n, sma_quit, sma_prev_down;
static char sma_new[28], sma_err[80], sma_msg[100];

static void sma_cat(char *o, int max, const char *s) { int n = 0; while (o[n]) n++; while (*s && n < max - 1) o[n++] = *s++; o[n] = 0; }
static void sma_cpy(char *o, int max, const char *s) { o[0] = 0; sma_cat(o, max, s); }

static void sma_add_prog(const char *label, const char *target) {
    if (sma_np >= SMA_ROWS) return;
    sma_cpy(sma_prog[sma_np].label, sizeof sma_prog[0].label, label);
    sma_cpy(sma_prog[sma_np].target, sizeof sma_prog[0].target, target);
    sma_np++;
}
static int sma_progs_cb(const mwfs_dirent_t *e, void *ctx) {
    (void)ctx;
    if (e->type != MWFS_T_FILE) return 1;
    char tg[40]; sma_cpy(tg, sizeof tg, "prog:"); sma_cat(tg, sizeof tg, e->name);
    sma_add_prog(e->name, tg);
    return sma_np < SMA_ROWS;
}
static void sma_scan_progs(void) {
    sma_np = 0;
    sma_add_prog("Notepad", "app:NOTEPAD"); sma_add_prog("Setting", "app:SETTING"); sma_add_prog("MiniWeb", "app:WEB");
    sma_add_prog("MiniShell", "app:TERM"); sma_add_prog("MiniFiles", "app:FILES");
    vfs_node_t d;
    if (vfs_stat("/progs", &d) == MWFS_OK && d.type == MWFS_T_DIR) vfs_ls_each(&d, sma_progs_cb, 0);
}

typedef struct { char name[12][34]; int n; } sma_names_t;
static int sma_dirs_cb(const mwfs_dirent_t *e, void *ctx) {
    sma_names_t *c = (sma_names_t *)ctx;
    if (e->type != MWFS_T_DIR || c->n >= 12) return 1;
    sma_cpy(c->name[c->n++], 34, e->name);
    return 1;
}
static void sma_add_dir(const char *path, const char *name, int depth) {
    if (sma_nd >= SMA_ROWS) return;
    sma_cpy(sma_dir[sma_nd].path, sizeof sma_dir[0].path, path);
    sma_cpy(sma_dir[sma_nd].name, sizeof sma_dir[0].name, name);
    sma_dir[sma_nd].depth = depth;
    sma_nd++;
}
static void sma_scan_dirs(const char *path, int depth) {            /* the folders of `path` (and, one level down, theirs) */
    sma_names_t c; c.n = 0;
    vfs_node_t d;
    if (vfs_stat(path, &d) != MWFS_OK || d.type != MWFS_T_DIR) return;
    vfs_ls_each(&d, sma_dirs_cb, &c);
    for (int i = 0; i < c.n; i++) {
        char p[112]; sma_cpy(p, sizeof p, path); sma_cat(p, sizeof p, "/"); sma_cat(p, sizeof p, c.name[i]);
        sma_add_dir(p, c.name[i], depth);
        if (depth < 2) sma_scan_dirs(p, depth + 1);
    }
}
static void sma_scan_tree(void) {
    vfs_mkdir("/home/start");                                             /* the first use makes it (an existing one just says so) */
    sma_nd = 0;
    sma_add_dir("/home/start", "Start Menu", 0);
    sma_scan_dirs("/home/start", 1);
}

static const char *sma_errtext(int e) {
    return e == MWFS_E_NAME ? "That name is not allowed." : e == MWFS_E_EXIST ? "That folder already exists." : e == MWFS_E_FULL ? "The disk is full." : "It could not be made.";
}
static void sma_make_folder(void) {
    sma_err[0] = 0;
    if (sma_new_n == 0) return;
    if (sma_dir[sma_dsel].depth >= 2) { sma_cpy(sma_err, sizeof sma_err, "Folders can be nested two deep."); return; }
    char p[112]; sma_cpy(p, sizeof p, sma_dir[sma_dsel].path); sma_cat(p, sizeof p, "/"); sma_cat(p, sizeof p, sma_new);
    int e = vfs_mkdir(p);
    if (e != MWFS_OK) { sma_cpy(sma_err, sizeof sma_err, sma_errtext(e)); return; }
    sma_scan_tree();
    for (int i = 0; i < sma_nd; i++) { int same = 1; for (int k = 0; sma_dir[i].path[k] || p[k]; k++) if (sma_dir[i].path[k] != p[k]) { same = 0; break; } if (same) sma_dsel = i; }
    sma_naming = 0; sma_new_n = 0; sma_new[0] = 0;
}
static void sma_finish(void) {
    sma_err[0] = 0;
    char base[34]; sma_cpy(base, sizeof base, sma_prog[sma_psel].label);
    int n = 0; while (base[n]) n++;
    if (n > 4 && base[n - 4] == '.') base[n - 4] = 0;                 /* HELLO.MWP -> HELLO */
    char p[112]; sma_cpy(p, sizeof p, sma_dir[sma_dsel].path); sma_cat(p, sizeof p, "/"); sma_cat(p, sizeof p, base); sma_cat(p, sizeof p, ".LNK");
    const char *tg = sma_prog[sma_psel].target; int l = 0; while (tg[l]) l++;
    int e = vfs_write(p, (const u8 *)tg, (u32)l, 0);
    if (e != MWFS_OK) { sma_cpy(sma_err, sizeof sma_err, e == MWFS_E_FULL ? "The disk is full." : "The shortcut could not be written."); return; }
    sma_cpy(sma_msg, sizeof sma_msg, base); sma_cat(sma_msg, sizeof sma_msg, " was added to ");
    sma_cat(sma_msg, sizeof sma_msg, sma_dir[sma_dsel].depth ? sma_dir[sma_dsel].name : "the Start menu's Programs list"); sma_cat(sma_msg, sizeof sma_msg, ".");
    sma_page = SMA_DONE;
}

static void sma_header(const char *title) {
    int cw = pw_cw();
    ic16_blit(10, 8, IC_FOLDER);
    font_draw_k(32, 10, title, TH_TEXT);
    ui_hline(8, 28, cw - 16, TH_SHADOW); ui_hline(8, 29, cw - 16, TH_LIGHT);
}

/* a scrolling list of rows; `draw_row` is inlined by the two callers below */
static void sma_page_pick(int mx, int my, int down, int click, int k) {
    int cw = pw_cw(), ch = pw_ch();
    sma_header(SKO("Add to the Start menu", "시작 메뉴에 추가"));
    int y = ins_wrap(12, 36, cw - 24, SKO("Pick the program to put in the Start menu's Programs list.", "시작 메뉴의 프로그램 목록에 넣을 프로그램을 고르세요."), TH_TEXT);
    int lx = 10, ly = y + 6, lw = cw - 20, rh = 16;
    int vis = (ch - ly - 44) / rh; if (vis < 1) vis = 1;
    ui_field(lx, ly, lw, vis * rh + 4, TH_FIELD, 1);
    if (k == KEY_DOWN && sma_psel < sma_np - 1) sma_psel++;
    else if (k == KEY_UP && sma_psel > 0) sma_psel--;
    if (sma_psel < sma_ptop) sma_ptop = sma_psel;
    if (sma_psel >= sma_ptop + vis) sma_ptop = sma_psel - vis + 1;
    if (click && in_rect(mx, my, lx + 2, ly + 2, lw - 4, vis * rh)) { int i = sma_ptop + (my - ly - 2) / rh; if (i >= 0 && i < sma_np) sma_psel = i; }
    for (int i = 0; i < vis && sma_ptop + i < sma_np; i++) {
        int ry = ly + 2 + i * rh, sel = (sma_ptop + i == sma_psel);
        if (sel) bb_fillrect(lx + 2, ry, lw - 4, rh, TH_ACCENT);
        ic16_blit(lx + 6, ry, IC_PROG);
        font_draw_k(lx + 28, ry + 2, sma_prog[sma_ptop + i].label, sel ? TH_TEXT_INVERSE : TH_TEXT);
    }
    int by = ch - 28;
    if (ins_button(cw - 180, by, 80, 20, SKO("Cancel", "취소"), 1, mx, my, down, click) || k == 27) sma_quit = 1;
    if (ins_button(cw - 90, by, 80, 20, SKO("Next >", "다음 >"), sma_np > 0, mx, my, down, click) || (k == '\n' && sma_np > 0)) { sma_page = SMA_FOLDER; sma_err[0] = 0; sma_scan_tree(); }
}

static void sma_page_folder(int mx, int my, int down, int click, int k) {
    int cw = pw_cw(), ch = pw_ch();
    sma_header(SKO("Select Program Folder", "프로그램 폴더 선택"));
    int y = ins_wrap(12, 36, cw - 24, SKO("Select the folder to place the shortcut in:", "바로가기를 넣을 폴더를 고르세요:"), TH_TEXT);
    int lx = 10, ly = y + 4, lw = cw - 20, rh = 16;
    int vis = (ch - ly - 84) / rh; if (vis < 1) vis = 1;
    const int was_naming = sma_naming;              /* the key that ends the naming (Enter, Esc) must not also act on the page behind it */
    ui_field(lx, ly, lw, vis * rh + 4, TH_FIELD, 1);
    if (!sma_naming) {
        if (k == KEY_DOWN && sma_dsel < sma_nd - 1) sma_dsel++;
        else if (k == KEY_UP && sma_dsel > 0) sma_dsel--;
    }
    if (sma_dsel < sma_dtop) sma_dtop = sma_dsel;
    if (sma_dsel >= sma_dtop + vis) sma_dtop = sma_dsel - vis + 1;
    if (click && in_rect(mx, my, lx + 2, ly + 2, lw - 4, vis * rh)) { int i = sma_dtop + (my - ly - 2) / rh; if (i >= 0 && i < sma_nd) sma_dsel = i; }
    for (int i = 0; i < vis && sma_dtop + i < sma_nd; i++) {
        sma_dir_t *d = &sma_dir[sma_dtop + i];
        int ry = ly + 2 + i * rh, sel = (sma_dtop + i == sma_dsel), ix = lx + 6 + d->depth * 16;
        if (sel) bb_fillrect(ix + 20, ry, font_width_k(d->name) + 8, rh, TH_ACCENT);
        ic16_blit(ix, ry, sel ? IC_FOLDER_OPEN : IC_FOLDER);
        font_draw_k(ix + 24, ry + 2, d->name, sel ? TH_TEXT_INVERSE : TH_TEXT);
    }
    int ny = ly + vis * rh + 10;
    if (sma_naming) {
        font_draw_k(12, ny + 3, SKO("Folder name:", "폴더 이름:"), TH_TEXT);
        int fx = 12 + font_width_k(SKO("Folder name:", "폴더 이름:")) + 8, fw = cw - fx - 100;
        ui_field(fx, ny, fw, 18, TH_FIELD, 1);
        font_draw_k(fx + 4, ny + 3, sma_new, TH_FIELD_TEXT);
        bb_fillrect(fx + 4 + font_width_k(sma_new), ny + 3, 1, FONT_CELL, TH_FIELD_TEXT);
        if (ins_button(cw - 88, ny, 76, 18, SKO("Create", "만들기"), sma_new_n > 0, mx, my, down, click)) sma_make_folder();
        if (k == '\b') { if (sma_new_n > 0) sma_new[--sma_new_n] = 0; }
        else if (k >= 32 && k < 127 && sma_new_n < 24) { sma_new[sma_new_n++] = (char)k; sma_new[sma_new_n] = 0; }
        else if (k == '\n') sma_make_folder();
        else if (k == 27) { sma_naming = 0; sma_err[0] = 0; }
    } else if (sma_err[0]) font_draw_k(12, ny + 3, sma_err, COL_RED);
    if (sma_naming && sma_err[0]) font_draw_k(12, ny + 24, sma_err, COL_RED);
    int by = ch - 28;
    if (ins_button(10, by, 110, 20, SKO("New Folder...", "새 폴더..."), 1, mx, my, down, click)) { sma_naming = !sma_naming; sma_new_n = 0; sma_new[0] = 0; sma_err[0] = 0; }
    if (ins_button(cw - 270, by, 80, 20, SKO("< Back", "< 뒤로"), 1, mx, my, down, click)) { sma_page = SMA_PICK; sma_naming = 0; }
    if (ins_button(cw - 180, by, 80, 20, SKO("Cancel", "취소"), 1, mx, my, down, click)) sma_quit = 1;
    if (ins_button(cw - 90, by, 80, 20, SKO("Finish", "마침"), !was_naming, mx, my, down, click) || (!was_naming && k == '\n')) sma_finish();
    if (!was_naming && k == 27) sma_quit = 1;
}

static void sma_page_done(int mx, int my, int down, int click, int k) {
    int cw = pw_cw(), ch = pw_ch();
    sma_header(SKO("Done", "완료"));
    int y = ins_wrap(14, 40, cw - 28, sma_msg, TH_TEXT) + 6;
    ins_wrap(14, y, cw - 28, SKO("Open the Start menu and click Programs to see it.", "시작 메뉴에서 프로그램을 눌러 확인하세요."), TH_TEXT);
    if (ins_button(cw - 90, ch - 28, 80, 20, SKO("Close", "닫기"), 1, mx, my, down, click) || k == '\n' || k == 27) sma_quit = 1;
}

static void sma_main(void) {
    sma_page = SMA_PICK; sma_psel = sma_dsel = sma_ptop = sma_dtop = 0; sma_naming = 0; sma_new_n = 0; sma_new[0] = 0;
    sma_err[0] = 0; sma_quit = 0; sma_prev_down = 0;
    sma_scan_progs();
    while (!sma_quit) {
        int mx = pw.mx, my = pw.my, down = pw.mb & 1, click = down && !sma_prev_down;
        sma_prev_down = down;
        int k = pw_pop_key();
        bb_fillrect(0, 0, pw_cw(), pw_ch(), TH_FACE);
        if (sma_page == SMA_PICK) sma_page_pick(mx, my, down, click, k);
        else if (sma_page == SMA_FOLDER) sma_page_folder(mx, my, down, click, k);
        else sma_page_done(mx, my, down, click, k);
        pw_yield();
    }
}

/* Opens the wizard window; 0, or a sentence saying why not. */
static const char *startmenu_add_open(void) {
    if (!vfs_is_root_ok()) return "There is no root volume to keep shortcuts on.";
    if (!sma_prog || !sma_dir) return "Not enough memory to start it.";
    return pw_launch_fn(sma_main, "START MENU", IC_FOLDER);
}

#endif
