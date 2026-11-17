/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_UI_TEXTFIELD_H
#define MW_UI_TEXTFIELD_H

/* ui/textfield.h -- the one single-line text field of the OS (1.1-alpha-14). Notepad got a caret and a selection first (apps/notepad/edit.h);
 * this is the same thing for every one-line box: MiniFiles' rename box, MiniWeb's address bar, MiniShell's command line and Notepad's Save As.
 * The text lives in a plain NUL-terminated buffer the OWNER keeps (so the old code that reads it keeps working); the field itself is only a
 * caret, an anchor (the other end of the selection) and where it was last drawn:
 *
 *     static char name[32]; static tf_t name_tf;
 *     tf_key(&name_tf, name, sizeof name, TF_NAME, k)             -- a key: typing, Backspace, Delete, arrows, Home/End (Shift extends),
 *                                                                    Ctrl+A / C / X / V; returns 1 when the field used the key (Enter/Esc: 0)
 *     tf_draw(&name_tf, name, TF_NAME, x, y, w, 0, focused, fg)  -- text, selection and caret, KERNED like every other label
 *     tf_mouse_down(&name_tf, name, TF_NAME, mx, shift)          -- click = caret there; dragging (tf_poll_drag) extends the selection
 *
 * Positions are BYTE indexes on character boundaries (a Hangul syllable is 3 bytes). Widths are measured with font_width_k() -- the same
 * kerning that paints the text (Hangul: one full cell) -- and every x is "the width of what comes before it", so the caret, the highlight and a click always agree
 * with the pixels (tf_x() adds the kern between the two letters at a boundary, which a cut-off prefix width does not contain). Drawing
 * scrolls sideways to keep the caret visible and clips to the box.
 *
 * HANGUL IME. With Korean selected (Right Alt) the keys go through hangul_ime.h like in Notepad. The syllable being composed is kept IN the
 * buffer, right before the caret, as a real 3-byte character that is replaced on every key (t->live = 3): drawing, caret, width and the
 * owner reading the buffer (Enter) therefore all just work, and "committing" is only forgetting that it is live (ko_ime_reset()). A mouse
 * click, an arrow key, Enter, the right-click menu and Right Alt all commit (tf_ime_flush()). Typing over a selection replaces it when the
 * composition starts. The owner keeps a separate length next to some buffers: tf_key() takes a `sync` function that refreshes it after
 * every change (tf_ime_flush() changes no text: the live bytes are already in it). TF_NOKO / TF_PASS fields have no IME: the letters are
 * typed as Latin. Pasted Hangul is kept unless the flags say no.
 *
 * Needs font.h (kerning), font_ko.h (UTF-8 lengths), clipboard.h, keyboard.h, vga.h (clip rectangle) and the theme. */

typedef struct { u32 caret, anchor; int scroll, ox; u8 drag, live; } tf_t;     /* ox = screen x of byte 0 as last drawn (clicks use it); live = bytes of IME syllable before the caret (0 or 3) */

#define TF_ASCII 0      /* any printable ASCII */
#define TF_NAME  1      /* file-name characters only: letters, digits and . _ - , ( ) space, plus Hangul (what mwfs_name_ok() takes) */
#define TF_PASS  2      /* shown as stars; cannot be cut or copied (and has no Hangul IME) */
#define TF_NOKO  4      /* no Hangul: no IME and pasted Hangul is dropped */
#define TF_DISP_MAX 192 /* the longest buffer a field can show (the biggest one is the command line, 121) */

static inline u32 tf_len(const char *b) { u32 n = 0; while (b[n]) n++; return n; }
static inline int tf_ko(int fl) { return !(fl & (TF_NOKO | TF_PASS)); }                /* may this field hold Hangul? */
/* Is s[0..3) one Hangul syllable (U+AC00..D7A3) or compatibility jamo (U+3131..318E) in UTF-8? (the only multi-byte text the fonts can draw) */
static int tf_hangul3(const char *s, u32 avail) {
    if (avail < 3 || ((unsigned char)s[0] & 0xF0) != 0xE0 || ((unsigned char)s[1] & 0xC0) != 0x80 || ((unsigned char)s[2] & 0xC0) != 0x80) return 0;
    u32 cp = (((u32)(unsigned char)s[0] & 0x0F) << 12) | (((u32)(unsigned char)s[1] & 0x3F) << 6) | ((u32)(unsigned char)s[2] & 0x3F);
    return (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0x3131 && cp <= 0x318E);
}
static int tf_ok(char c, int fl) {
    if (fl & TF_NAME) return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == ',' || c == ' ' || c == '(' || c == ')';
    return c >= 32 && c < 127;
}
static inline u32 tf_lo(const tf_t *t) { return t->caret < t->anchor ? t->caret : t->anchor; }
static inline u32 tf_hi(const tf_t *t) { return t->caret < t->anchor ? t->anchor : t->caret; }
static inline int tf_has_sel(const tf_t *t) { return t->caret != t->anchor; }
/* The owner may rewrite the buffer behind our back (a page was loaded, a command ran): keep both ends inside it and on a boundary. */
static void tf_fix(tf_t *t, const char *b) {
    u32 n = tf_len(b);
    if (t->caret > n) t->caret = n;
    if (t->anchor > n) t->anchor = n;
}
static inline void tf_to_end(tf_t *t, const char *b) { t->caret = t->anchor = tf_len(b); t->scroll = 0; }
static inline void tf_select_all(tf_t *t, const char *b) { t->anchor = 0; t->caret = tf_len(b); }
static u32 tf_back(const char *b, u32 i) { return i == 0 ? 0 : i - (u32)ko_utf8_last_char_len(b, i); }
static u32 tf_fwd(const char *b, u32 i) {
    u32 n = tf_len(b);
    if (i >= n) return n;
    u32 c = (u32)ko_utf8_char_len((unsigned char)b[i]);
    return i + c > n ? i + 1 : i + c;
}

/* What is drawn for buf: the text itself, or one star per byte for a password. */
static char *tf_disp(char *buf, int fl, char *tmp) {
    if (!(fl & TF_PASS)) return buf;
    u32 n = tf_len(buf);
    if (n > TF_DISP_MAX - 1) n = TF_DISP_MAX - 1;
    for (u32 i = 0; i < n; i++) tmp[i] = '*';
    tmp[n] = 0;
    return tmp;
}
/* Width of the first `upto` bytes of d (a cut-off prefix: no kern after its last letter). */
static int tf_pw(char *d, u32 upto) {
    char sv = d[upto]; d[upto] = 0;
    int w = font_width_k(d);
    d[upto] = sv;
    return w;
}
/* Where byte i starts: the prefix width plus the kern between the previous letter and this one. */
static int tf_x(char *d, u32 i) {
    int w = tf_pw(d, i);
    if (font_kerning && i > 0 && d[i] && (unsigned char)d[i - 1] < 0x80 && (unsigned char)d[i] < 0x80) w += font_kern(d[i - 1], d[i]);   /* Latin pairs only */
    return w;
}
/* The byte index nearest to rel (pixels from the start of the text). */
static u32 tf_idx_at(char *d, int rel) {
    u32 n = tf_len(d), i = 0;
    if (rel <= 0) return 0;
    int prev = 0;
    while (i < n) {
        u32 j = tf_fwd(d, i);
        int w = tf_pw(d, j);
        if (rel < (prev + w) / 2) return i;
        prev = w; i = j;
    }
    return n;
}

/* ---- editing ---- */
static void tf_del(tf_t *t, char *buf, u32 a, u32 b) {
    u32 n = tf_len(buf);
    if (b > n) b = n;
    if (b > a) for (u32 i = b; i <= n; i++) buf[i - (b - a)] = buf[i];
    t->caret = t->anchor = a;
}
static void tf_del_sel(tf_t *t, char *buf) { if (tf_has_sel(t)) tf_del(t, buf, tf_lo(t), tf_hi(t)); }
/* Types s over the selection at the caret; only what the flags allow goes in, and only as much as fits (cap counts the NUL). Returns bytes added. */
static u32 tf_insert(tf_t *t, char *buf, u32 cap, int fl, const char *s, u32 n) {
    tf_fix(t, buf);
    tf_del_sel(t, buf);
    u32 len = tf_len(buf), added = 0;
    for (u32 i = 0; i < n; i++) {
        u32 cl = 1;
        if (tf_ko(fl) && tf_hangul3(s + i, n - i)) cl = 3;                 /* a whole Hangul syllable or none of it */
        if (cl == 1 && !tf_ok(s[i], fl)) continue;
        if (len + cl >= cap) { i += cl - 1; continue; }
        for (u32 j = len + cl; j >= t->caret + cl; j--) buf[j] = buf[j - cl];
        for (u32 q = 0; q < cl; q++) buf[t->caret++] = s[i + q];
        len += cl; added += cl; i += cl - 1;
    }
    t->anchor = t->caret;
    return added;
}
static void tf_copy(tf_t *t, const char *buf, int fl) { if (!(fl & TF_PASS) && tf_has_sel(t)) clip_set(buf + tf_lo(t), tf_hi(t) - tf_lo(t)); }
static void tf_cut(tf_t *t, char *buf, int fl) { if (fl & TF_PASS) return; tf_copy(t, buf, fl); tf_del_sel(t, buf); }
/* Pastes the first line of the clipboard. */
static void tf_paste(tf_t *t, char *buf, u32 cap, int fl) {
    u32 n = 0;
    while (n < clip_len && clip_buf[n] != '\n' && clip_buf[n] != '\r') n++;
    tf_insert(t, buf, cap, fl, clip_buf, n);
}

/* ---- Hangul IME (hangul_ime.h): the syllable being composed lives in the buffer, before the caret, as t->live bytes ---- */
static tf_t *tf_ime_owner = 0;                                   /* the field that has a live syllable */
/* Inserts n raw bytes at the caret if they fit (no filtering). Returns 1 if they went in. */
static int tf_raw_ins(tf_t *t, char *buf, u32 cap, const char *s, u32 n) {
    u32 len = tf_len(buf);
    if (len + n >= cap) return 0;
    for (u32 j = len + n; j >= t->caret + n; j--) buf[j] = buf[j - n];
    for (u32 q = 0; q < n; q++) buf[t->caret + q] = s[q];
    t->caret += n; t->anchor = t->caret;
    return 1;
}
static void tf_live_remove(tf_t *t, char *buf) {
    if (t->live && t->caret >= t->live) tf_del(t, buf, t->caret - t->live, t->caret);
    t->live = 0;
}
static void tf_live_put(tf_t *t, char *buf, u32 cap) {              /* the preview of the syllable in progress, as a real character */
    int cp = ko_ime_preview_codepoint();
    tf_ime_owner = t;
    if (cp < 0) return;
    char b3[3] = { (char)(0xE0 | (cp >> 12)), (char)(0x80 | ((cp >> 6) & 0x3F)), (char)(0x80 | (cp & 0x3F)) };
    if (tf_raw_ins(t, buf, cap, b3, 3)) t->live = 3;
}
/* Commits whatever syllable is live (its bytes are already in the text: it just stops being replaceable) and ends the composition. Called on a
 * click, Right Alt, Enter, the right-click menu, and by tf_key() for every key that is not typing. */
static void tf_ime_flush(void) {
    if (!tf_ime_owner) return;                                  /* (a syllable Notepad is composing is Notepad's: np_commit_ime() commits it) */
    tf_ime_owner->live = 0; tf_ime_owner = 0;
    if (ko_ime_is_composing()) ko_ime_reset();
}

/* A key. Returns 1 when the field used it, 0 for the ones that are the owner's business (Enter, Esc, Up, Down, Tab, other Ctrl keys).
 * `sync` (may be 0) is called after any change so an owner with a separate length variable can refresh it. */
static int tf_key(tf_t *t, char *buf, u32 cap, int fl, int k, void (*sync)(void)) {
    if (k == 0) return 0;                                              /* no key this frame (KEY_NONE): nothing to do -- and it must not commit a syllable */
    tf_fix(t, buf);
    if (t->live && !ko_ime_is_composing()) t->live = 0;                  /* the composition ended behind our back (reset, mode switch) */
    if (tf_ime_owner && tf_ime_owner != t) tf_ime_flush();             /* another field was in the middle of a syllable */
    int shift = keyboard_shift_held();
    int ko = tf_ko(fl) && current_ime == IME_KOREAN;
    int typing = (k >= 32 && k < 127 && !keyboard_ctrl_held()) || k == '\b';
    if ((!typing || !ko) && (t->live || tf_ime_owner == t)) tf_ime_flush();    /* an arrow, Ctrl+V, Esc ... or the Latin mode: commit first */
    if (ko && typing) {
        if (k == '\b') {
            if (t->live) {                                                /* Backspace steps back inside the syllable first */
                tf_live_remove(t, buf);
                if (ko_ime_backspace() && ko_ime_is_composing()) tf_live_put(t, buf, cap); else tf_ime_owner = 0;
                if (sync) sync();
                return 1;
            }
        } else {
            if (t->live) tf_live_remove(t, buf); else tf_del_sel(t, buf);   /* typing replaces a selection when the composition starts */
            char sc[8]; u32 sl = 0;
            int used = ko_ime_feed_key((char)k, sc, &sl, sizeof sc);        /* a finished syllable (if any) lands in sc ... */
            if (sl) tf_raw_ins(t, buf, cap, sc, sl);                        /* ... and is put in at the caret */
            if (used) {
                if (ko_ime_is_composing()) tf_live_put(t, buf, cap); else tf_ime_owner = 0;
                if (sync) sync();
                return 1;
            }
            tf_ime_owner = 0;                                              /* not a jamo key: the composition was committed; type the key as is */
        }
    }
    int r = 1;
    if (k > 0 && keyboard_ctrl_held()) {
        char c = (char)k;
        if (c == 'a' || c == 'A') tf_select_all(t, buf);
        else if (c == 'c' || c == 'C') tf_copy(t, buf, fl);
        else if (c == 'x' || c == 'X') tf_cut(t, buf, fl);
        else if (c == 'v' || c == 'V') tf_paste(t, buf, cap, fl);
        else return 0;
    } else if (k == KEY_LEFT || k == KEY_RIGHT || k == KEY_HOME || k == KEY_END) {
        u32 c = t->caret;
        if (k == KEY_LEFT) c = (tf_has_sel(t) && !shift) ? tf_lo(t) : tf_back(buf, c);
        else if (k == KEY_RIGHT) c = (tf_has_sel(t) && !shift) ? tf_hi(t) : tf_fwd(buf, c);
        else if (k == KEY_HOME) c = 0;
        else c = tf_len(buf);
        t->caret = c;
        if (!shift) t->anchor = c;
    } else if (k == KEY_DEL) {
        if (tf_has_sel(t)) tf_del_sel(t, buf); else tf_del(t, buf, t->caret, tf_fwd(buf, t->caret));
    } else if (k == '\b') {
        if (tf_has_sel(t)) tf_del_sel(t, buf); else tf_del(t, buf, tf_back(buf, t->caret), t->caret);
    } else if (k >= 32 && k < 127 && tf_ok((char)k, fl)) {
        char c = (char)k; tf_insert(t, buf, cap, fl, &c, 1);
    } else r = 0;
    if (r && sync) sync();
    return r;
}

/* ---- drawing ---- */
static void tf_seg(char *d, u32 a, u32 b, int x, int y, u32 col) {
    if (b <= a) return;
    char sv = d[b]; d[b] = 0;
    font_draw_k(x, y, d + a, col);
    d[b] = sv;
}
/* Paints the field's text in the box (x, y) .. w wide, FONT_CELL tall: lead (a prompt, or 0), the text, its selection (white on blue, only while
 * focused) and the caret. Scrolls sideways so the caret stays in view; clips to the box and puts the old clip back. */
static void tf_draw(tf_t *t, char *buf, int fl, int x, int y, int w, const char *lead, int focused, u32 fg) {
    char tmp[TF_DISP_MAX];
    tf_fix(t, buf);
    char *d = tf_disp(buf, fl, tmp);
    u32 n = tf_len(d);
    int leadw = lead ? font_width_k(lead) : 0;
    int cx = leadw + tf_x(d, t->caret), total = leadw + tf_pw(d, n);
    if (total + 3 <= w) t->scroll = 0;
    else {
        if (cx - t->scroll > w - 3) t->scroll = cx - (w - 3);
        if (cx - t->scroll < 0) t->scroll = cx;
        if (t->scroll < 0) t->scroll = 0;
    }
    int ox = x + leadw - t->scroll;
    t->ox = ox;
    int s0 = bb_cx0, s1 = bb_cy0, s2 = bb_cx1, s3 = bb_cy1;                 /* intersect with whatever clip is already set */
    int nx0 = x > s0 ? x : s0, ny0 = (y - 1) > s1 ? (y - 1) : s1;
    int nx1 = (x + w) < s2 ? (x + w) : s2, ny1 = (y + FONT_LINE_H) < s3 ? (y + FONT_LINE_H) : s3;
    bb_clip_set(nx0, ny0, nx1 - nx0, ny1 - ny0);
    if (lead) font_draw_k(x - t->scroll, y, lead, fg);
    u32 lo = tf_lo(t), hi = tf_hi(t);
    if (focused && hi > lo) {
        int xa = ox + tf_x(d, lo), xb = ox + (hi >= n ? tf_pw(d, n) : tf_x(d, hi));
        bb_fillrect(xa, y - 1, xb - xa, FONT_CELL + 2, TH_ACCENT);
        tf_seg(d, 0, lo, ox, y, fg);
        tf_seg(d, lo, hi, xa, y, TH_TEXT_INVERSE);
        if (hi < n) tf_seg(d, hi, n, xb, y, fg);
    } else tf_seg(d, 0, n, ox, y, fg);
    if (focused && t->live && ko_ime_is_composing() && t->caret >= t->live) {              /* the syllable still being composed: underlined */
        int ua = ox + tf_x(d, t->caret - t->live), ub = ox + (t->caret >= n ? tf_pw(d, n) : tf_x(d, t->caret));
        bb_fillrect(ua, y + FONT_CELL, ub - ua, 1, fg);
    }
    if (focused) bb_fillrect(ox + tf_x(d, t->caret), y, 2, FONT_CELL, fg);
    bb_cx0 = s0; bb_cy0 = s1; bb_cx1 = s2; bb_cy1 = s3;
}

/* ---- the mouse ---- */
static tf_t *tf_drag_t = 0;           /* the field whose selection the pressed mouse is extending */
static char *tf_drag_buf = 0;
static int   tf_drag_fl = 0;
static void tf_mouse_down(tf_t *t, char *buf, int fl, int mx, int shift) {
    char tmp[TF_DISP_MAX];
    tf_ime_flush();                                                   /* a click commits the syllable in progress */
    tf_fix(t, buf);
    t->caret = tf_idx_at(tf_disp(buf, fl, tmp), mx - t->ox);
    if (!shift) t->anchor = t->caret;
    t->drag = 1; tf_drag_t = t; tf_drag_buf = buf; tf_drag_fl = fl;
}
/* Called every frame with the pointer and the left button: extends the selection while it is held, lets go when it is not. */
static void tf_poll_drag(int mx, int left_now) {
    if (!tf_drag_t) return;
    if (!left_now) { tf_drag_t->drag = 0; tf_drag_t = 0; return; }
    char tmp[TF_DISP_MAX];
    tf_drag_t->caret = tf_idx_at(tf_disp(tf_drag_buf, tf_drag_fl, tmp), mx - tf_drag_t->ox);
}

#endif
