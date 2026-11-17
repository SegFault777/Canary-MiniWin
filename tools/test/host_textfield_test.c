/* SPDX-License-Identifier: GPL-3.0-only */
/* kernel/ui/textfield.h (1.1-alpha-14): the shared one-line text field -- typing, deletion, caret keys with Shift, the clipboard keys, the
 * file-name and password flags, UTF-8 boundaries, buffer capacity, kerned positions (a click on a boundary must land on it again), scrolling,
 * the clip rectangle being put back, the mouse -- run on the real kernel code with the font and screen stubbed, plus a random-key fuzz with
 * the sanitizers watching the buffer. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
#define FONT_CELL 11
#define FONT_LINE_H 13          /* the cell + the descender rows (kernel/font.h) */
#define TH_ACCENT 1
#define TH_TEXT_INVERSE 2
#define KEY_UP -1
#define KEY_DOWN -2
#define KEY_LEFT -3
#define KEY_RIGHT -4
#define KEY_HOME -7
#define KEY_END -8
#define KEY_DEL -9
static char clipmem[4096];
#define MW_BSSX_CLIP (clipmem)
static int ctrl_h, shift_h;
static int keyboard_ctrl_held(void) { return ctrl_h; }
static int keyboard_shift_held(void) { return shift_h; }
static inline int ko_utf8_char_len(unsigned char lead) { return (lead & 0xF0) == 0xE0 ? 3 : 1; }
static inline int ko_utf8_last_char_len(const char *buf, u32 len) {
    if (len >= 3 && ((unsigned char)buf[len - 1] & 0xC0) == 0x80 && ((unsigned char)buf[len - 2] & 0xC0) == 0x80 && ((unsigned char)buf[len - 3] & 0xF0) == 0xE0) return 3;
    return 1;
}
/* a mock Hangul IME with the same contract as hangul_ime.h: letters a-z compose (a, then aa; a third letter commits the syllable and starts the
 * next), anything else commits and is left to the caller; the preview codepoint is U+AC00 + number of letters in the syllable */
#define IME_ENGLISH 0
#define IME_KOREAN 1
static int current_ime = IME_ENGLISH, mk_n = 0;
static int ko_ime_is_composing(void) { return mk_n > 0; }
static int ko_ime_preview_codepoint(void) { return mk_n ? 0xAC00 + mk_n : -1; }
static void ko_ime_reset(void) { mk_n = 0; }
static int ko_ime_backspace(void) { if (mk_n) { mk_n--; return 1; } return 0; }
static void mk_emit(char *b, u32 *len, u32 max, int cp) { if (*len + 3 > max) return; b[(*len)++] = (char)(0xE0 | (cp >> 12)); b[(*len)++] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[(*len)++] = (char)(0x80 | (cp & 0x3F)); }
static int ko_ime_feed_key(char c, char *b, u32 *len, u32 max) {
    if (c >= 'a' && c <= 'z') { if (mk_n == 2) { mk_emit(b, len, max, 0xAC02); mk_n = 1; } else mk_n++; return 1; }
    if (mk_n) { mk_emit(b, len, max, 0xAC00 + mk_n); mk_n = 0; }
    return 0;
}
/* the font: narrow i/l, a kern pair A,V; the same width rule as font.h */
static int font_kerning = 1;
static int font_adv(char c) { return c == 'i' || c == 'l' ? 4 : c == ' ' ? 5 : 8; }
static int font_kern(char a, char b) { return a == 'A' && b == 'V' ? -2 : 0; }
static int font_width_k(const char *s) {          /* like ko_string_width(): a 3-byte Hangul syllable is one cell, Latin letters kern */
    int w = 0;
    while (*s) {
        if (ko_utf8_char_len((unsigned char)*s) == 3 && s[1] && s[2]) { w += FONT_CELL; s += 3; continue; }
        w += font_kerning ? font_adv(*s) + ((s[1] && ko_utf8_char_len((unsigned char)s[1]) == 1) ? font_kern(*s, s[1]) : 0) : FONT_CELL;
        s++;
    }
    return w;
}
static int drawn_x[64], drawn_n; static char drawn_s[64][64]; static u32 drawn_c[64];
static void font_draw_k(int x, int y, const char *s, u32 c) { (void)y; if (drawn_n < 64) { drawn_x[drawn_n] = x; strncpy(drawn_s[drawn_n], s, 63); drawn_c[drawn_n++] = c; } }
static int bb_cx0 = 0, bb_cy0 = 0, bb_cx1 = 4096, bb_cy1 = 4096;
static void bb_clip_set(int x, int y, int w, int h) { bb_cx0 = x; bb_cy0 = y; bb_cx1 = x + w; bb_cy1 = y + h; }
static int fills;
static void bb_fillrect(int x, int y, int w, int h, u32 c) { (void)x; (void)y; (void)w; (void)h; (void)c; fills++; }
#include "../../kernel/clipboard.h"
#include "../../kernel/ui/textfield.h"
#define tf_key5(t, b, c, f, k) tf_key(t, b, c, f, k, 0)

static int fails, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void typestr(tf_t *t, char *b, u32 cap, int fl, const char *s) { for (; *s; s++) tf_key5(t, b, cap, fl, *s); }
static int sbuf_eq(const char *a, const char *b) { return strcmp(a, b) == 0; }

int main(void) {
    char b[32]; tf_t t;
    /* typing and caret movement */
    memset(&t, 0, sizeof t); b[0] = 0;
    typestr(&t, b, sizeof b, TF_ASCII, "hello");
    CHECK(sbuf_eq(b, "hello") && t.caret == 5 && t.anchor == 5, "typing: '%s' caret %u", b, t.caret);
    tf_key5(&t, b, sizeof b, TF_ASCII, KEY_LEFT); tf_key5(&t, b, sizeof b, TF_ASCII, KEY_LEFT);
    tf_key5(&t, b, sizeof b, TF_ASCII, 'X');
    CHECK(sbuf_eq(b, "helXlo") && t.caret == 4, "insert in the middle: '%s' caret %u", b, t.caret);
    tf_key5(&t, b, sizeof b, TF_ASCII, KEY_HOME);
    CHECK(t.caret == 0 && t.anchor == 0, "Home");
    CHECK(tf_key5(&t, b, sizeof b, TF_ASCII, '\b') == 1 && sbuf_eq(b, "helXlo"), "Backspace at the start changes nothing");
    tf_key5(&t, b, sizeof b, TF_ASCII, KEY_END);
    CHECK(t.caret == 6, "End");
    tf_key5(&t, b, sizeof b, TF_ASCII, KEY_DEL);
    CHECK(sbuf_eq(b, "helXlo"), "Delete at the end changes nothing");
    tf_key5(&t, b, sizeof b, TF_ASCII, '\b');
    CHECK(sbuf_eq(b, "helXl") && t.caret == 5, "Backspace: '%s'", b);
    tf_key5(&t, b, sizeof b, TF_ASCII, KEY_HOME); tf_key5(&t, b, sizeof b, TF_ASCII, KEY_DEL);
    CHECK(sbuf_eq(b, "elXl") && t.caret == 0, "Delete: '%s'", b);

    /* Shift extends a selection; typing replaces it; an arrow collapses it to its edge */
    shift_h = 1; tf_key5(&t, b, sizeof b, TF_ASCII, KEY_RIGHT); tf_key5(&t, b, sizeof b, TF_ASCII, KEY_RIGHT); shift_h = 0;
    CHECK(t.anchor == 0 && t.caret == 2 && tf_has_sel(&t), "Shift+Right x2 selects two");
    tf_key5(&t, b, sizeof b, TF_ASCII, KEY_LEFT);
    CHECK(t.caret == 0 && t.anchor == 0, "Left collapses a selection to its start");
    shift_h = 1; tf_key5(&t, b, sizeof b, TF_ASCII, KEY_END); shift_h = 0;
    tf_key5(&t, b, sizeof b, TF_ASCII, 'Z');
    CHECK(sbuf_eq(b, "Z") && t.caret == 1, "typing over a selection: '%s'", b);
    typestr(&t, b, sizeof b, TF_ASCII, "abc");
    shift_h = 1; tf_key5(&t, b, sizeof b, TF_ASCII, KEY_HOME); shift_h = 0;
    tf_key5(&t, b, sizeof b, TF_ASCII, '\b');
    CHECK(b[0] == 0 && t.caret == 0, "Backspace over a selection deletes it all");

    /* clipboard keys */
    typestr(&t, b, sizeof b, TF_ASCII, "copy me");
    ctrl_h = 1;
    CHECK(tf_key5(&t, b, sizeof b, TF_ASCII, 'a') == 1 && t.anchor == 0 && t.caret == 7, "Ctrl+A selects all");
    tf_key5(&t, b, sizeof b, TF_ASCII, 'c');
    CHECK(clip_len == 7 && memcmp(clip_buf, "copy me", 7) == 0 && sbuf_eq(b, "copy me"), "Ctrl+C copies, keeps the text");
    tf_key5(&t, b, sizeof b, TF_ASCII, 'x');
    CHECK(b[0] == 0, "Ctrl+X cuts");
    tf_key5(&t, b, sizeof b, TF_ASCII, 'v'); tf_key5(&t, b, sizeof b, TF_ASCII, 'v');
    CHECK(sbuf_eq(b, "copy mecopy me") && t.caret == 14, "Ctrl+V twice: '%s'", b);
    CHECK(tf_key5(&t, b, sizeof b, TF_ASCII, 's') == 0 && sbuf_eq(b, "copy mecopy me"), "an unknown Ctrl key is left to the owner and types nothing");
    ctrl_h = 0;
    CHECK(tf_key5(&t, b, sizeof b, TF_ASCII, '\n') == 0 && tf_key5(&t, b, sizeof b, TF_ASCII, 27) == 0 && tf_key5(&t, b, sizeof b, TF_ASCII, KEY_UP) == 0, "Enter, Esc, Up are the owner's");

    /* paste takes the first line only; the file-name flag filters */
    clip_set("a/b\\c:d\nsecond", 14);
    memset(&t, 0, sizeof t); b[0] = 0; ctrl_h = 1; tf_key5(&t, b, sizeof b, TF_NAME, 'v'); ctrl_h = 0;
    CHECK(sbuf_eq(b, "abcd"), "paste into a name field: '%s'", b);
    memset(&t, 0, sizeof t); b[0] = 0; ctrl_h = 1; tf_key5(&t, b, sizeof b, TF_ASCII, 'v'); ctrl_h = 0;
    CHECK(sbuf_eq(b, "a/b\\c:d"), "paste into an ASCII field stops at the newline: '%s'", b);
    memset(&t, 0, sizeof t); b[0] = 0;
    typestr(&t, b, sizeof b, TF_NAME, "my file(1).txt/<>");
    CHECK(sbuf_eq(b, "my file(1).txt"), "name flag filters typing: '%s'", b);

    /* a password: stars, no copy/cut */
    memset(&t, 0, sizeof t); b[0] = 0; clip_set("keep", 4);
    typestr(&t, b, sizeof b, TF_PASS, "secret");
    ctrl_h = 1; tf_key5(&t, b, sizeof b, TF_PASS, 'a'); tf_key5(&t, b, sizeof b, TF_PASS, 'c'); tf_key5(&t, b, sizeof b, TF_PASS, 'x'); ctrl_h = 0;
    CHECK(sbuf_eq(b, "secret") && clip_len == 4 && memcmp(clip_buf, "keep", 4) == 0, "password: Ctrl+C and Ctrl+X do nothing");
    { char tmp[TF_DISP_MAX]; CHECK(sbuf_eq(tf_disp(b, TF_PASS, tmp), "******"), "password is shown as stars"); }

    /* capacity */
    { char s8[8]; memset(&t, 0, sizeof t); s8[0] = 0; typestr(&t, s8, sizeof s8, TF_ASCII, "0123456789");
      CHECK(sbuf_eq(s8, "0123456") && t.caret == 7, "a buffer of 8 holds 7 characters: '%s'", s8); }

    /* UTF-8: Backspace, Delete and the arrows move by a whole syllable */
    memset(&t, 0, sizeof t); strcpy(b, "a\xea\xb0\x80\xeb\x82\x98z"); tf_to_end(&t, b);       /* a 가 나 z */
    tf_key5(&t, b, sizeof b, TF_ASCII, KEY_LEFT); CHECK(t.caret == 7, "Left over 'z'");
    tf_key5(&t, b, sizeof b, TF_ASCII, KEY_LEFT); CHECK(t.caret == 4, "Left over a 3-byte syllable lands on its start (%u)", t.caret);
    tf_key5(&t, b, sizeof b, TF_ASCII, '\b'); CHECK(sbuf_eq(b, "a\xeb\x82\x98z") && t.caret == 1, "Backspace removes the whole syllable");
    tf_key5(&t, b, sizeof b, TF_ASCII, KEY_DEL); CHECK(sbuf_eq(b, "az"), "Delete removes the whole syllable");

    /* the owner rewrites the buffer: both ends are pulled back inside */
    memset(&t, 0, sizeof t); strcpy(b, "long text here"); tf_to_end(&t, b); strcpy(b, "ab");
    tf_fix(&t, b); CHECK(t.caret == 2 && t.anchor == 2, "tf_fix after a shorter buffer");

    /* kerned positions: a click exactly on a boundary lands on it; halfway rounds to the nearer one; kerning off too */
    for (int kern = 0; kern < 2; kern++) {
        font_kerning = kern;
        strcpy(b, "AVAil lAV iii");
        u32 n = tf_len(b), bad = 0;
        for (u32 i = 0; i <= n; i++) if (tf_idx_at(b, tf_x(b, i)) != i) bad++;
        CHECK(!bad, "kerning=%d: %u boundaries do not round-trip", kern, bad);
        bad = 0;
        for (u32 i = 0; i < n; i++) { int a = tf_x(b, i), c = (i + 1 < n) ? tf_x(b, i + 1) : tf_pw(b, n); u32 got = tf_idx_at(b, a + (c - a) / 4); if (got != i) bad++; }
        CHECK(!bad, "kerning=%d: a click on the left quarter of a letter picks that letter (%u wrong)", kern, bad);
        CHECK(tf_idx_at(b, -5) == 0 && tf_idx_at(b, 100000) == n, "clicks outside the text clamp");
        CHECK(tf_x(b, 1) == tf_pw(b, 1) + (kern ? font_kern('A', 'V') : 0), "tf_x adds the kern into the next letter (kerning=%d)", kern);
    }
    font_kerning = 1;

    char lb[64];
    /* drawing: scrolls to keep the caret in view, restores the clip, highlights only while focused */
    memset(&t, 0, sizeof t); strcpy(lb, "the quick brown fox jumps over the lazy dog"); tf_to_end(&t, lb);
    bb_cx0 = 3; bb_cy0 = 4; bb_cx1 = 500; bb_cy1 = 400;
    drawn_n = 0; fills = 0;
    tf_draw(&t, lb, TF_ASCII, 20, 30, 100, 0, 1, 7);
    CHECK(bb_cx0 == 3 && bb_cy0 == 4 && bb_cx1 == 500 && bb_cy1 == 400, "the old clip is put back");
    { int cx = t.ox + tf_x(lb, t.caret); CHECK(cx >= 20 && cx <= 120, "caret x %d is inside the 20..120 box", cx); }
    CHECK(t.scroll > 0, "a long text scrolls (%d)", t.scroll);
    tf_key5(&t, lb, sizeof lb, TF_ASCII, KEY_HOME); tf_draw(&t, lb, TF_ASCII, 20, 30, 100, 0, 1, 7);
    CHECK(t.scroll == 0 && t.ox == 20, "Home scrolls back (scroll %d ox %d)", t.scroll, t.ox);
    shift_h = 1; for (int i = 0; i < 4; i++) tf_key5(&t, lb, sizeof lb, TF_ASCII, KEY_RIGHT); shift_h = 0;
    drawn_n = 0; tf_draw(&t, lb, TF_ASCII, 20, 30, 100, 0, 1, 7);
    { int sel = 0; for (int i = 0; i < drawn_n; i++) if (drawn_c[i] == TH_TEXT_INVERSE && sbuf_eq(drawn_s[i], "the ")) sel = 1; CHECK(sel, "the selected text is drawn inverse"); }
    drawn_n = 0; tf_draw(&t, lb, TF_ASCII, 20, 30, 100, 0, 0, 7);
    { int sel = 0; for (int i = 0; i < drawn_n; i++) if (drawn_c[i] == TH_TEXT_INVERSE) sel = 1; CHECK(!sel, "an unfocused field shows no selection"); }
    drawn_n = 0; tf_draw(&t, lb, TF_ASCII, 20, 30, 100, "> ", 1, 7);
    CHECK(drawn_n >= 1 && sbuf_eq(drawn_s[0], "> "), "the prompt is drawn first");

    /* the mouse: down puts the caret, a drag extends, release lets go; Shift+click keeps the anchor */
    memset(&t, 0, sizeof t); strcpy(b, "hello world"); tf_to_end(&t, b);
    tf_draw(&t, b, TF_ASCII, 50, 30, 300, 0, 1, 7);
    tf_mouse_down(&t, b, TF_ASCII, 50 + tf_x(b, 5), 0);
    CHECK(t.caret == 5 && t.anchor == 5 && t.drag, "mouse down at a boundary");
    tf_poll_drag(50 + tf_x(b, 8), 1);
    CHECK(t.caret == 8 && t.anchor == 5, "dragging extends");
    tf_poll_drag(50 + tf_x(b, 8), 0);
    CHECK(!t.drag && tf_drag_t == 0, "release lets go");
    tf_poll_drag(0, 1);
    CHECK(t.caret == 8, "a stale poll after release does nothing");
    tf_mouse_down(&t, b, TF_ASCII, 50 + tf_x(b, 2), 1);
    CHECK(t.caret == 2 && t.anchor == 5, "Shift+click keeps the anchor");
    tf_poll_drag(0, 0);

    /* ---- the Hangul IME in a field ---- */
    {
        char hb[40]; tf_t ht; current_ime = IME_KOREAN; mk_n = 0;
        memset(&ht, 0, sizeof ht); hb[0] = 0;
        tf_key5(&ht, hb, sizeof hb, TF_ASCII, 'a');
        CHECK(tf_len(hb) == 3 && ht.live == 3 && ht.caret == 3 && (unsigned char)hb[0] == 0xEA, "IME: the first jamo key shows a live syllable in the text (len %u live %u)", tf_len(hb), ht.live);
        tf_key5(&ht, hb, sizeof hb, TF_ASCII, 'a');
        CHECK(tf_len(hb) == 3 && mk_n == 2 && ht.live == 3, "IME: the next key REPLACES the live syllable (len %u n %d)", tf_len(hb), mk_n);
        tf_key5(&ht, hb, sizeof hb, TF_ASCII, 'a');
        CHECK(tf_len(hb) == 6 && ht.live == 3 && ht.caret == 6 && mk_n == 1, "IME: a third key commits the syllable and starts the next (len %u)", tf_len(hb));
        tf_key5(&ht, hb, sizeof hb, TF_ASCII, '\b');
        CHECK(tf_len(hb) == 3 && !ht.live && mk_n == 0 && ht.caret == 3, "IME: Backspace steps back inside the syllable, leaving the committed one (len %u)", tf_len(hb));
        tf_key5(&ht, hb, sizeof hb, TF_ASCII, 'a'); tf_key5(&ht, hb, sizeof hb, TF_ASCII, 'a');
        tf_key5(&ht, hb, sizeof hb, TF_ASCII, ' ');
        CHECK(tf_len(hb) == 7 && hb[6] == ' ' && !ht.live && mk_n == 0, "IME: a space commits the syllable then types the space (len %u)", tf_len(hb));
        tf_key5(&ht, hb, sizeof hb, TF_ASCII, 'a');
        tf_key5(&ht, hb, sizeof hb, TF_ASCII, KEY_LEFT);
        CHECK(!ht.live && mk_n == 0 && tf_len(hb) == 10 && ht.caret == 7, "IME: an arrow key commits first, then moves over the whole syllable (caret %u)", ht.caret);
        memset(&ht, 0, sizeof ht); hb[0] = 0; mk_n = 0;                /* "no key" frames (MiniFiles calls the field every frame) must not commit */
        tf_key5(&ht, hb, sizeof hb, TF_ASCII, 'a'); tf_key5(&ht, hb, sizeof hb, TF_ASCII, 0); tf_key5(&ht, hb, sizeof hb, TF_ASCII, 0);
        CHECK(ht.live == 3 && mk_n == 1, "IME: frames without a key do not commit the syllable (live %u n %d)", ht.live, mk_n);
        tf_ime_flush();
        /* in the middle of text, over a selection */
        memset(&ht, 0, sizeof ht); strcpy(hb, "xy"); ht.caret = ht.anchor = 1; mk_n = 0;
        tf_key5(&ht, hb, sizeof hb, TF_ASCII, 'a');
        CHECK(tf_len(hb) == 5 && hb[0] == 'x' && hb[4] == 'y' && ht.caret == 4, "IME: composes at the caret, in the middle of the text");
        tf_ime_flush();
        memset(&ht, 0, sizeof ht); strcpy(hb, "hello"); tf_select_all(&ht, hb); mk_n = 0;
        tf_key5(&ht, hb, sizeof hb, TF_ASCII, 'a');
        CHECK(tf_len(hb) == 3 && ht.live == 3, "IME: starting a syllable replaces the selection");
        tf_ime_flush();
        /* a click commits; a second field's key commits the first's syllable */
        memset(&ht, 0, sizeof ht); hb[0] = 0; mk_n = 0;
        tf_key5(&ht, hb, sizeof hb, TF_ASCII, 'a'); ht.ox = 0;
        tf_mouse_down(&ht, hb, TF_ASCII, 0, 0); tf_poll_drag(0, 0);
        CHECK(!ht.live && mk_n == 0 && tf_len(hb) == 3, "IME: a mouse click commits the syllable in progress");
        { char b2[40] = ""; tf_t t2; memset(&t2, 0, sizeof t2);
          tf_key5(&ht, hb, sizeof hb, TF_ASCII, 'a');
          tf_key5(&t2, b2, sizeof b2, TF_ASCII, 'a');
          CHECK(!ht.live && ht.caret <= tf_len(hb) && t2.live == 3 && mk_n == 1, "IME: a key in another field commits this field's syllable"); tf_ime_flush(); }
        /* Latin mode, passwords and name-only fields type letters as Latin */
        current_ime = IME_ENGLISH; memset(&ht, 0, sizeof ht); hb[0] = 0; mk_n = 0;
        tf_key5(&ht, hb, sizeof hb, TF_ASCII, 'a'); CHECK(sbuf_eq(hb, "a") && !ht.live, "IME off: Latin letters");
        current_ime = IME_KOREAN; memset(&ht, 0, sizeof ht); hb[0] = 0; mk_n = 0;
        tf_key5(&ht, hb, sizeof hb, TF_PASS, 'a'); CHECK(sbuf_eq(hb, "a") && mk_n == 0, "a password field has no IME");
        /* pasted Hangul: kept in ASCII fields and names, only whole valid syllables, dropped by TF_NOKO */
        clip_set("\xea\xb0\x80x\xe2\x82\xac\xea\xb0", 10);                   /* 가 x € and half a syllable */
        memset(&ht, 0, sizeof ht); hb[0] = 0; ctrl_h = 1; tf_key5(&ht, hb, sizeof hb, TF_ASCII, 'v'); ctrl_h = 0;
        CHECK(tf_len(hb) == 4 && (unsigned char)hb[0] == 0xEA && hb[3] == 'x', "paste: Hangul syllable and Latin kept, a euro sign and a cut-off syllable dropped (len %u)", tf_len(hb));
        memset(&ht, 0, sizeof ht); hb[0] = 0; ctrl_h = 1; tf_key5(&ht, hb, sizeof hb, TF_NOKO, 'v'); ctrl_h = 0;
        CHECK(sbuf_eq(hb, "x"), "paste into a TF_NOKO field keeps only the Latin (%s)", hb);
        /* capacity: a 3-byte syllable never half-fits */
        { char s5[5]; memset(&ht, 0, sizeof ht); s5[0] = 0; mk_n = 0;
          tf_key5(&ht, s5, sizeof s5, TF_ASCII, 'a'); tf_key5(&ht, s5, sizeof s5, TF_ASCII, 'a'); tf_key5(&ht, s5, sizeof s5, TF_ASCII, 'a');
          int ok = tf_len(s5) < sizeof s5; for (u32 i = 0; i < tf_len(s5); ) { if ((unsigned char)s5[i] >= 0x80) { ok &= (((unsigned char)s5[i + 1] & 0xC0) == 0x80 && ((unsigned char)s5[i + 2] & 0xC0) == 0x80); i += 3; } else i++; }
          CHECK(ok, "IME in a 5-byte buffer stays whole and terminated ('%s')", s5); tf_ime_flush(); mk_n = 0; }
        current_ime = IME_ENGLISH; mk_n = 0;
    }

    /* fuzz: random keys, flags, caps; the invariants must always hold (ASan/UBSan watch the buffer) */
    srand(12345);
    for (int round = 0; round < 3000; round++) {
        char fb[40]; memset(fb, 0xAA, sizeof fb); fb[0] = 0; mk_n = 0; tf_ime_owner = 0;
        u32 cap = 4 + (u32)(rand() % 30); int fl = rand() % 3; tf_t ft; memset(&ft, 0, sizeof ft);
        if (rand() % 4 == 0) clip_set("paste\xea\xb0\x80 me\nnope", 16);
        for (int step = 0; step < 60; step++) {
            int r = rand() % 14, k;
            ctrl_h = shift_h = 0; current_ime = (rand() % 3 == 0) ? IME_ENGLISH : IME_KOREAN;
            if (r < 6) k = 32 + rand() % 95;
            else if (r == 6) k = '\b'; else if (r == 7) k = KEY_DEL; else if (r == 8) k = KEY_LEFT; else if (r == 9) k = KEY_RIGHT;
            else if (r == 10) k = KEY_HOME; else if (r == 11) k = KEY_END;
            else { k = "acxv"[rand() % 4]; ctrl_h = 1; }
            shift_h = rand() % 2;
            tf_key5(&ft, fb, cap, fl, k);
            u32 n = tf_len(fb);
            CHECK(n < cap && ft.caret <= n && ft.anchor <= n, "fuzz: len %u cap %u caret %u anchor %u", n, cap, ft.caret, ft.anchor);
            for (u32 i = 0; i < n; ) {                                   /* every byte is an allowed Latin one or part of a whole Hangul syllable */
                if ((unsigned char)fb[i] >= 0x80) { if (!tf_ko(fl) || !tf_hangul3(fb + i, n - i)) { CHECK(0, "fuzz: bad byte %02x at %u flags %d", (unsigned char)fb[i], i, fl); break; } i += 3; }
                else { if (!tf_ok(fb[i], fl)) { CHECK(0, "fuzz: bad byte %02x at %u flags %d", (unsigned char)fb[i], i, fl); break; } i++; }
            }
            CHECK(ft.live == 0 || (ft.live == 3 && ft.caret >= 3 && tf_hangul3(fb + ft.caret - 3, 3)), "fuzz: live syllable is a whole character before the caret (live %u caret %u)", ft.live, ft.caret);
            if (step % 15 == 0) { drawn_n = 0; tf_draw(&ft, fb, fl, 10, 10, 20 + rand() % 150, rand() % 2 ? "> " : 0, rand() % 2, 7); }
        }
    }
    ctrl_h = shift_h = 0;
    printf("textfield: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
