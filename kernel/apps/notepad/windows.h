/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_APPS_NOTEPAD_WINDOWS_H
#define MW_APPS_NOTEPAD_WINDOWS_H

/* apps/notepad/windows.h -- the NOTEPAD_MAX Notepad windows: state, creation, focus, per-window text buffers.
 * (Split out of kernel.c in pre-29; it is still one translation unit, so modules are included by
 * kernel.c in dependency order -- see the module map there.) */

/* ============================================================
 * Multiple Notepad windows
 *
 * Up to NOTEPAD_MAX independent Notepad windows can be open at once
 * (matches FS_MAX_FILES -- also just a sane cap for how many overlapping
 * windows make sense on a 320x200 screen). Each one carries its own
 * geometry, its own document buffer, its own bound-file slot, its own
 * File-menu-open flag, and its own Save-changes confirm dialog state --
 * none of that is shared between windows anymore.
 *
 * Every Notepad-specific helper function below (draw_window(),
 * btn_min_x(), file_label_hit(), save_current_document(), ...) reads and
 * writes through a single scratch pointer, `active_np`, rather than
 * taking a notepad_t* parameter directly. Whichever bit of code is about
 * to draw, click-test, or type into a particular window sets active_np
 * first. This is a deliberately simple "current context" pattern instead
 * of threading a pointer through a couple dozen function signatures --
 * there's only ever one Notepad window being drawn, clicked, or typed
 * into at any given instant anyway, even though up to four can exist.
 * ============================================================ */
#define NOTEPAD_MAX 4

typedef struct {
    window_t win;
    int id;                          /* index into notepads[] -- set once at boot */
    char text_buf[FS_MAX_FILE_BYTES];
    u32  text_len;
    int  bound_slot;                 /* which FS slot this document is saved to, -1 = unbound */
    int  file_menu_open;
    int  confirm_mode;               /* CONFIRM_NONE / CONFIRM_NEW / CONFIRM_CLOSE / CONFIRM_SAVEAS, this window's own */
    char saveas_name[FS_NAME_MAXLEN + 1];   /* the Save As dialog's file-name field */
    int  saveas_len;
    tf_t saveas_tf;                         /* ... and its caret and selection (ui/textfield.h) */
    int  scroll;                     /* first visible wrapped row of the text */
    u32  scroll_len;                 /* text_len when the view last followed the typing (see draw_window) */
    u32  caret, anchor;              /* the text cursor (a byte index, always on a character boundary) and the other end of the selection: the selection is [min, max) of the two, empty when they are equal */
    int  sel_drag;                   /* the left button is down and the mouse is extending the selection */
    u32  scroll_caret;               /* the caret when the view last followed it */
    int  typing;                     /* the last thing done was typing (so the next keystroke needs no new undo snapshot) */
    int  undo_valid;
    u32  undo_len;
    char undo_buf[FS_MAX_FILE_BYTES];   /* the text as it was before the last edit; Edit > Undo swaps it with the current text, so Undo twice is Redo */
    int  info_kind;                  /* CONFIRM_INFO: 1 = Keys, 2 = About */
} notepad_t;

static notepad_t *notepads;            /* NOTEPAD_MAX windows, from the heap (boot_heap_init in sys/boot.h): 33 KB that no longer sit in .bss */
static notepad_t *active_np = 0;   /* set in kernel_boot once the heap holds the windows */

/* ------------------------------------------------------------
 * Window IDs + z-order (stacking) + minimize-order (taskbar layout)
 *
 * A "window id" is just 0..NOTEPAD_MAX-1 for notepads[id], or
 * WIN_ID_SETTING for the Setting window, WIN_ID_WEB for the MiniWeb
 * browser window, or WIN_ID_TERMINAL for MiniShell.mwp -- one small
 * integer namespace covering every top-level window in the OS, so the
 * taskbar, z-order, and focus-on-click logic can all treat "which
 * window" generically instead of hardcoding every app by name at every
 * call site.
 * ------------------------------------------------------------ */
#define WIN_ID_SETTING  NOTEPAD_MAX
#define WIN_ID_WEB      (NOTEPAD_MAX + 1)
#define WIN_ID_TERMINAL (NOTEPAD_MAX + 2)
#define WIN_ID_FILES    (NOTEPAD_MAX + 3)
#define WIN_ID_PROG     (NOTEPAD_MAX + 4)     /* the FIRST of PW_MAX program windows (apps/progwin.h): WIN_ID_PROG + 0 .. PW_MAX - 1, one per running program */
#define WIN_ID_COUNT    (NOTEPAD_MAX + 4 + PW_MAX)
#define PW_IS(id)       ((id) >= WIN_ID_PROG && (id) < WIN_ID_PROG + PW_MAX)
/* Dialog windows (ui/dialog.h): ids from 32 up, past every scroll-bar owner id (ui/scrollbar.h ignores owners >= SB_MAX_TARGETS = 32), so a dialog
 * can never be mistaken for a window with a scroll bar. Slot i < NOTEPAD_MAX is the dialog of Notepad window i, slot DLG_WEB is MiniWeb's About box.
 * Per-window tables (z-order, taskbar order, minimize order) are indexed by win_slot(id): the plain ids first, the dialogs after them. */
#define WIN_ID_DLG      32
#define DLG_WEB         NOTEPAD_MAX                 /* MiniWeb's About box */
#define DLG_DISP        (NOTEPAD_MAX + 1)           /* "Keep this screen size?" (and the "cannot show that size" notice): the system's, no owner window */
#define DLG_DPASK       (NOTEPAD_MAX + 2)           /* Display Properties: "restart to use the new screen area?" -- owner: Setting */
#define DLG_FNTASK      (NOTEPAD_MAX + 3)           /* Fonts: "this font is very small, use it anyway?" -- owner: Setting */
#define DLG_ADDFONT     (NOTEPAD_MAX + 4)           /* Fonts: Install New Font / Add Fonts -- owner: Setting */
#define DLG_IS(id)      ((id) >= WIN_ID_DLG && (id) < WIN_ID_DLG + DLG_MAX)
#define WIN_SLOTS       (WIN_ID_COUNT + DLG_MAX)
_Static_assert(DLG_MAX == NOTEPAD_MAX + 5, "one dialog slot per Notepad window plus the five others above");
static inline int win_slot(int id)    { return DLG_IS(id) ? WIN_ID_COUNT + (id - WIN_ID_DLG) : id; }
static inline int win_id_at(int slot) { return slot < WIN_ID_COUNT ? slot : WIN_ID_DLG + (slot - WIN_ID_COUNT); }
static inline int dlg_owner_id(int slot) {                                                              /* the window a dialog belongs to (and blocks); -1 = none */
    return slot < NOTEPAD_MAX ? slot : slot == DLG_WEB ? WIN_ID_WEB : slot == DLG_DISP ? -1 : WIN_ID_SETTING;
}
/* Where a dialog's own contents begin: the dialog's window, or -- when it is maximized -- the middle of it. The dialogs' drawing and
 * hit-testing (apps/notepad/dialogs.h, file_menu.h, apps/web/view.h) measure everything from this corner, so they work the same either way. */
static inline int dlg_ox(int slot) { const window_t *w = &dlg_wins[slot]; return w->x + (w->w - dlg_nat_w[slot]) / 2; }
static inline int dlg_oy(int slot) { const window_t *w = &dlg_wins[slot]; return w->y + (w->h - dlg_nat_h[slot]) / 2; }
static const char *dlg_title(int slot);          /* ui/dialog.h */
static int dlg_icon(int slot);
static inline void pw_select(int win_id) { pw_sel = win_id - WIN_ID_PROG; }      /* makes `pw` / `pw_win` mean the program window `win_id` */

static int win_is_open(int id) {
    if (DLG_IS(id)) return dlg_wins[id - WIN_ID_DLG].open;
    if (id == WIN_ID_SETTING) return setting.open;
    if (id == WIN_ID_WEB) return web_win.open;
    if (id == WIN_ID_TERMINAL) return term_win.open;
    if (id == WIN_ID_FILES) return files_win.open;
    if (PW_IS(id)) return pw_wins[id - WIN_ID_PROG].open;
    return notepads[id].win.open;
}
static int win_is_minimized(int id) {
    if (DLG_IS(id)) return dlg_wins[id - WIN_ID_DLG].minimized;
    if (id == WIN_ID_SETTING) return setting.minimized;
    if (id == WIN_ID_WEB) return web_win.minimized;
    if (id == WIN_ID_TERMINAL) return term_win.minimized;
    if (id == WIN_ID_FILES) return files_win.minimized;
    if (PW_IS(id)) return pw_wins[id - WIN_ID_PROG].minimized;
    return notepads[id].win.minimized;
}
static window_t *win_ptr(int id) {
    if (DLG_IS(id)) return &dlg_wins[id - WIN_ID_DLG];
    if (id == WIN_ID_SETTING) return &setting;
    if (id == WIN_ID_WEB) return &web_win;
    if (id == WIN_ID_TERMINAL) return &term_win;
    if (id == WIN_ID_FILES) return &files_win;
    if (PW_IS(id)) return &pw_wins[id - WIN_ID_PROG];
    return &notepads[id].win;
}

/* z_order[0..z_count-1] lists every currently-OPEN window id, back
 * (bottom) to front (top). Closing a window removes it; clicking one
 * (or opening/restoring it) moves it to the end, i.e. the front -- the
 * entire "clicking a window brings it to the front" feature is just
 * these three tiny functions plus render_frame() drawing in this order
 * and the click-handler hit-testing in reverse. */
static int z_order[WIN_SLOTS];
static int z_count = 0;

static void win_z_remove(int id) {
    for (int i = 0; i < z_count; i++) {
        if (z_order[i] == id) {
            for (int j = i; j < z_count - 1; j++) z_order[j] = z_order[j + 1];
            z_count--;
            return;
        }
    }
}
/* 1.1-alpha-14: a window never starts (or comes back) bigger than the screen. The default sizes were made for 640x480; on a 320x200
 * screen (boot/stage2.asm, Control Panel > Display) they are cut down to what fits above the task bar and moved back onto the screen.
 * The size to restore to is cut the same way, so un-maximising cannot bring the overhang back. A no-op at 640x480. */
static void win_fit_axis(int *pos, int *len, int limit) {
    if (*len > limit) *len = limit;
    if (*pos + *len > limit) *pos = limit - *len;
    if (*pos < 0) *pos = 0;
}
static void win_fit_screen(window_t *w) {
    win_fit_axis(&w->restore_x, &w->restore_w, VGA_WIDTH);
    win_fit_axis(&w->restore_y, &w->restore_h, VGA_HEIGHT - TASKBAR_H);
    if (w->maximized) return;                                       /* (the maximised rectangle is the screen already) */
    win_fit_axis(&w->x, &w->w, VGA_WIDTH);
    win_fit_axis(&w->y, &w->h, VGA_HEIGHT - TASKBAR_H);
}
static void win_z_raise(int id) {
    win_z_remove(id);
    z_order[z_count++] = id;
    win_fit_screen(win_ptr(id));
    if (!DLG_IS(id)) {                                  /* a window with a question open never gets in front of it: the dialog follows its owner up */
        for (int s = 0; s < DLG_MAX; s++)
            if (dlg_wins[s].open && dlg_owner_id(s) == id) { win_z_remove(WIN_ID_DLG + s); z_order[z_count++] = WIN_ID_DLG + s; }
    }
}

/* ------------------------------------------------------------
 * Title-bar button press state -- shared by every _/[]/X button in the
 * OS (Notepad's, Setting's, and the Warning dialog's lone X). Pressing
 * one down doesn't fire it immediately anymore: it just "arms" that
 * specific button (kind + which window) and the button's own bevel
 * flips to a sunken look. The action only actually happens when the
 * mouse button is RELEASED while still over that same button -- the
 * same press-hold-release contract every real button widget uses, and
 * releasing anywhere else (drag off first) quietly cancels it.
 * ------------------------------------------------------------ */
#define BTN_NONE          0
#define BTN_MIN           1
#define BTN_MAX           2
#define BTN_CLOSE         3
#define BTN_WEB_GO        5  /* the URL bar's GO button -- always WIN_ID_WEB,
                              * but tracked the same press-hold-release
                              * way as every other button here so a drag-
                              * off cancels it instead of firing early */

/* Window-drag state: which window id is being dragged by its title bar (-1 = none) and the offset from its top-left corner to the point that
 * was grabbed, so it follows the pointer without its corner snapping to it. One mechanism for every window and every dialog (ui/input.h moves it). */
static int dragging_id = -1;
static int drag_offset_x = 0, drag_offset_y = 0;

static int pressed_btn_kind = BTN_NONE;
static int pressed_btn_win = -1;   /* which window id this press belongs to (WIN_ID_SETTING or a notepad index) */

/* (draw_bevel_button() lives in ui/widgets.h now, as ui_bevel().) */

/* Separate from z-order on purpose: z-order is about on-screen stacking
 * of VISIBLE windows, while this is purely "what order were things
 * minimized in," used only to lay out taskbar pills left-to-right in
 * that order. A window keeps its z-order slot while minimized (so
 * restoring puts it back where clicking-to-focus would), but it has no
 * on-screen rect to stack while minimized, hence the separate concept. */
static u32 minimize_seq[WIN_SLOTS];
static u32 next_minimize_seq = 1;
static void win_minimize(int id) {
    window_t *w_ = win_ptr(id);
    w_->minimized = 1;
    minimize_seq[win_slot(id)] = next_minimize_seq++;
}
static void win_restore(int id) {
    win_ptr(id)->minimized = 0;
    win_z_raise(id); /* restoring a window also focuses it, like real WMs do */
}

/* Actual body of ime_cycle_next(), forward-declared above -- now that
 * notepad_t exists, it can flush a syllable mid-switch into whichever
 * window currently has keyboard focus. */
static void ime_cycle_next(void) {
    for (int step = 1; step <= IME_COUNT; step++) {
        int candidate = (current_ime + step) % IME_COUNT;
        if (ime_enabled[candidate]) {
            if (candidate != current_ime && ko_ime_is_composing() && active_np) {
                ko_ime_commit(active_np->text_buf, &active_np->text_len, sizeof(active_np->text_buf));
            }
            current_ime = candidate;
            return;
        }
    }
}

#endif
