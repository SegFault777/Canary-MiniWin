/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_APPS_NOTEPAD_EDIT_H
#define MW_APPS_NOTEPAD_EDIT_H

/* apps/notepad/edit.h -- Notepad's text cursor and selection (1.1-alpha-14). Until now Notepad could only append; now there is a caret anywhere
 * in the text and a selection range, set with the mouse (click, drag, Shift+click), the keyboard (arrows, Home, End, with Shift to extend) or
 * Edit > Select All. All positions are BYTE indexes that sit on character boundaries (a Hangul syllable is 3 bytes).
 *
 * The layout rules (a kerned advance per character -- np_adv() in save.h, FONT_CELL for Hangul -- wrap when a row is full, '\n' ends a row) are the ones np_rows_ex() in save.h and the
 * drawing code in draw.h use; np_pos_of() and np_idx_at() are the same walk, answering "where is byte i" and "which byte is at (row, x)".
 *
 * Typing keeps the old way of working -- the Hangul IME and every editing helper append at the END of the text -- and np_rotate_new() then
 * moves what was appended to the caret (three reversals, no extra buffer). Needs save.h (edit_x(), np_wrap_limit(), np_visible_rows()). */

#define NPB(n) ((n)->text_buf)
static inline u32 np_sel_lo(void) { return active_np->caret < active_np->anchor ? active_np->caret : active_np->anchor; }
static inline u32 np_sel_hi(void) { return active_np->caret < active_np->anchor ? active_np->anchor : active_np->caret; }
static inline int np_has_sel(void) { return active_np->caret != active_np->anchor; }
static void np_clamp(void) {
    if (active_np->caret > active_np->text_len) active_np->caret = active_np->text_len;
    if (active_np->anchor > active_np->text_len) active_np->anchor = active_np->text_len;
}
static u32 np_char_back(u32 i) { return i == 0 ? 0 : i - (u32)ko_utf8_last_char_len(active_np->text_buf, i); }
static u32 np_char_fwd(u32 i) {
    if (i >= active_np->text_len) return active_np->text_len;
    u32 c = (u32)ko_utf8_char_len((unsigned char)active_np->text_buf[i]);
    return i + c > active_np->text_len ? i + 1 : i + c;
}

/* Where byte index idx is drawn: its row and its x offset inside the row (a wrapped character is at the start of the next row). */
static void np_pos_of(u32 idx, int *prow, int *prel) {
    const notepad_t *np = active_np;
    int limit = np_wrap_limit(edit_w()), rel = 0, row = 0;
    for (u32 i = 0; i < np->text_len; ) {
        int three = ko_utf8_char_len((unsigned char)np->text_buf[i]) == 3 && i + 3 <= np->text_len;
        char c = np->text_buf[i];
        if (three) { if (rel > limit) { rel = 0; row++; } if (i >= idx) break; rel += FONT_CELL; i += 3; continue; }
        if (i >= idx) { if (c != '\n' && rel > limit) { rel = 0; row++; } break; }
        if (c == '\n' || rel > limit) { rel = 0; row++; if (c == '\n') { i++; continue; } }
        rel += np_adv(np->text_buf, np->text_len, i, 0); i++;
    }
    *prow = row; *prel = rel;
}
/* The byte index nearest to x (pixels from the row's left edge) on row row_t; past the end of a row it is the row's end, past the last row the end of the text. */
static u32 np_idx_at(int row_t, int x) {
    const notepad_t *np = active_np;
    int limit = np_wrap_limit(edit_w()), rel = 0, row = 0;
    for (u32 i = 0; i < np->text_len; ) {
        int three = ko_utf8_char_len((unsigned char)np->text_buf[i]) == 3 && i + 3 <= np->text_len;
        char c = np->text_buf[i];
        if (three) { if (rel > limit) { rel = 0; row++; } }
        else if (c == '\n' || rel > limit) { if (row == row_t) return i; rel = 0; row++; if (c == '\n') { i++; continue; } }
        int adv = np_adv(np->text_buf, np->text_len, i, three);
        if (row == row_t) { if (x < rel + adv / 2) return i; }
        else if (row > row_t) return i;
        rel += adv; i += three ? 3 : 1;
    }
    return np->text_len;
}

static void np_commit_ime(void);
static void np_del_range(u32 a, u32 b) {
    notepad_t *np = active_np;
    if (b > np->text_len) b = np->text_len;
    if (b > a) { for (u32 i = b; i <= np->text_len; i++) np->text_buf[i - (b - a)] = np->text_buf[i]; np->text_len -= b - a; }
    np->caret = np->anchor = a;
}
static void np_rev(char *b, u32 i, u32 j) { while (i + 1 < j) { char t = b[i]; b[i] = b[j - 1]; b[j - 1] = t; i++; j--; } }
/* Whatever was appended at the end since the text was `before` bytes long moves to the caret, and the caret moves behind it. */
static void np_rotate_new(u32 before) {
    notepad_t *np = active_np;
    u32 n = np->text_len - before, c = np->caret > before ? before : np->caret;
    if (!n) return;
    if (c < before) { np_rev(np->text_buf, c, before); np_rev(np->text_buf, before, np->text_len); np_rev(np->text_buf, c, np->text_len); }
    np->caret = np->anchor = c + n;
}
/* Inserts n bytes at the caret (as many as fit). Returns how many went in. */
static u32 np_insert(const char *s, u32 n) {
    notepad_t *np = active_np;
    u32 room = (u32)sizeof(np->text_buf) - 1 - np->text_len;
    if (n > room) n = room;
    for (u32 i = np->text_len + 1; i > np->caret; i--) np->text_buf[i - 1 + n] = np->text_buf[i - 1];
    for (u32 i = 0; i < n; i++) np->text_buf[np->caret + i] = s[i];
    np->text_len += n; np->text_buf[np->text_len] = 0; np->caret += n; np->anchor = np->caret;
    return n;
}
static void np_commit_ime(void) {
    if (current_ime == IME_KOREAN && ko_ime_is_composing()) {
        u32 before = active_np->text_len;
        ko_ime_commit(active_np->text_buf, &active_np->text_len, sizeof(active_np->text_buf));
        np_rotate_new(before);
    }
}

/* ---- the mouse ---- */
static int np_edit_hit(int px, int py) { return in_rect(px, py, edit_x(), edit_y(), edit_w() - SB_W - 2, edit_h()); }
static u32 np_mouse_idx(int px, int py) {
    int ey = edit_y() + 2, row = py < ey ? active_np->scroll - 1 : (py - ey) / FONT_LINE_H + active_np->scroll;
    return row < 0 ? 0 : np_idx_at(row, px - (edit_x() + 2));
}
static void np_mouse_down(int px, int py, int shift) {
    np_commit_ime();
    active_np->caret = np_mouse_idx(px, py);
    if (!shift) active_np->anchor = active_np->caret;
    active_np->sel_drag = 1; active_np->typing = 0;
}
static void np_mouse_drag(int px, int py) { active_np->caret = np_mouse_idx(px, py); }

#endif
