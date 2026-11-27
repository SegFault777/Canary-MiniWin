/* SPDX-License-Identifier: GPL-3.0-only */
/* @mwp title: Tetris */
/* @mwp abi: 3 */
/* tetris.c -- a complete falling-blocks game (SDK 3.0 example): the keyboard, timing with ticks_ms, drawing with fill_rect and the theme colours,
 * resizing its own window with set_size, a 7-bag randomizer from mw_rand, and all of the state in ONE struct on the stack (no globals: a
 * classic program has no .bss).
 *
 *   Left / Right  move        Up  rotate        Down  soft drop        Space  hard drop
 *   P  pause      R  new game        Esc  quit
 *
 *   sdk/build.sh sdk/examples/tetris.c        sdk/install.sh sdk/examples/tetris.c TETRIS.MWP */
#include "miniwin.h"

#define BW 10                       /* the well: 10 columns, 20 rows */
#define BH 20
#define CELL 16                     /* pixels per block */
#define PANEL 112                   /* the score panel to the right of the well */
#define PAD 10
#define CLIENT_W (PAD + BW * CELL + PAD + PANEL + PAD)
#define CLIENT_H (PAD + BH * CELL + PAD)

/* The seven pieces, four turns each: a 4x4 grid in 16 bits, the top row in the high nibble. */
static const u16 SHAPE[7][4] = {
    { 0x0F00, 0x2222, 0x00F0, 0x4444 },     /* I */
    { 0x6600, 0x6600, 0x6600, 0x6600 },     /* O */
    { 0x4E00, 0x4640, 0x0E40, 0x4C40 },     /* T */
    { 0x6C00, 0x4620, 0x06C0, 0x8C40 },     /* S */
    { 0xC600, 0x2640, 0x0C60, 0x4C80 },     /* Z */
    { 0x8E00, 0x6440, 0x0E20, 0x44C0 },     /* J */
    { 0x2E00, 0x4460, 0x0E80, 0xC440 },     /* L */
};
static const u32 COLOR[7] = { 0x00C8E8, 0xF0D000, 0xA040D8, 0x40C040, 0xE04040, 0x4060E0, 0xE88020 };

typedef struct {
    u8  well[BH][BW];               /* 0 = empty, 1..7 = the piece that landed there */
    int type, rot, x, y;            /* the falling piece: x, y = its 4x4 grid's top-left cell in the well (x may be negative) */
    int next;
    u8  bag[7]; int bag_pos;        /* the 7-bag: every piece once, shuffled, before any repeats */
    int score, lines, level;
    int over, paused;
    u32 last_drop;                  /* ticks_ms of the last gravity step */
} game_t;

static int piece_fits(const game_t *g, int type, int rot, int px, int py) {
    u16 m = SHAPE[type][rot];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            if (!(m & (0x8000u >> (r * 4 + c)))) continue;
            int x = px + c, y = py + r;
            if (x < 0 || x >= BW || y >= BH) return 0;
            if (y >= 0 && g->well[y][x]) return 0;
        }
    return 1;
}

static int bag_next(game_t *g) {
    if (g->bag_pos >= 7) {
        for (int i = 0; i < 7; i++) g->bag[i] = (u8)i;
        for (int i = 6; i > 0; i--) { int j = (int)mw_rand_range((u32)i + 1); u8 t = g->bag[i]; g->bag[i] = g->bag[j]; g->bag[j] = t; }
        g->bag_pos = 0;
    }
    return g->bag[g->bag_pos++];
}

static void spawn(game_t *g) {
    g->type = g->next; g->next = bag_next(g);
    g->rot = 0; g->x = 3; g->y = -1;
    if (!piece_fits(g, g->type, g->rot, g->x, g->y)) g->over = 1;
    g->last_drop = sys.ticks_ms();
}

static void new_game(game_t *g) {
    mw_memset(g, 0, sizeof *g);
    g->bag_pos = 7;
    g->next = bag_next(g);
    spawn(g);
}

static int gravity_ms(const game_t *g) { int ms = 700 - g->level * 60; return ms < 70 ? 70 : ms; }

/* The piece has landed: write it into the well, clear full rows, score, bring the next one. */
static void lock_piece(game_t *g) {
    u16 m = SHAPE[g->type][g->rot];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (m & (0x8000u >> (r * 4 + c))) {
                int x = g->x + c, y = g->y + r;
                if (y >= 0 && y < BH && x >= 0 && x < BW) g->well[y][x] = (u8)(g->type + 1);
            }
    int cleared = 0;
    for (int y = BH - 1; y >= 0; ) {
        int full = 1;
        for (int x = 0; x < BW; x++) if (!g->well[y][x]) { full = 0; break; }
        if (!full) { y--; continue; }
        for (int yy = y; yy > 0; yy--) mw_memcpy(g->well[yy], g->well[yy - 1], BW);
        mw_memset(g->well[0], 0, BW);
        cleared++;                                          /* the same row index now holds what was above it: look at it again */
    }
    static const int pts[5] = { 0, 40, 100, 300, 1200 };
    g->score += pts[cleared] * (g->level + 1);
    g->lines += cleared;
    g->level = g->lines / 10;
    spawn(g);
}

static void try_move(game_t *g, int dx) { if (piece_fits(g, g->type, g->rot, g->x + dx, g->y)) g->x += dx; }
static void try_rotate(game_t *g) {
    int nr = (g->rot + 1) & 3;
    static const int kick[5] = { 0, -1, 1, -2, 2 };         /* if it does not fit where it is, try nudging it sideways */
    for (int i = 0; i < 5; i++)
        if (piece_fits(g, g->type, nr, g->x + kick[i], g->y)) { g->rot = nr; g->x += kick[i]; return; }
}
static int step_down(game_t *g) {                           /* 1 if it moved, 0 if it landed */
    if (piece_fits(g, g->type, g->rot, g->x, g->y + 1)) { g->y++; g->last_drop = sys.ticks_ms(); return 1; }
    lock_piece(g);
    return 0;
}

static void draw_block(int x, int y, u32 c) {
    sys.fill_rect(x, y, CELL, CELL, c);
    mw_hline(x, y, CELL, mw_mix(c, MW_WHITE, 128)); mw_vline(x, y, CELL, mw_mix(c, MW_WHITE, 128));
    mw_hline(x, y + CELL - 1, CELL, mw_mix(c, MW_BLACK, 128)); mw_vline(x + CELL - 1, y, CELL, mw_mix(c, MW_BLACK, 128));
}

static void draw_piece_grid(int type, int rot, int px, int py, int cell, u32 col) {      /* a piece anywhere (the "next" preview) */
    u16 m = SHAPE[type][rot];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (m & (0x8000u >> (r * 4 + c))) {
                if (cell == CELL) draw_block(px + c * cell, py + r * cell, col);
                else { sys.fill_rect(px + c * cell, py + r * cell, cell, cell, col); mw_hline(px + c * cell, py + r * cell, cell, mw_mix(col, MW_WHITE, 128)); }
            }
}

static void draw_game(const game_t *g, int ox, int oy) {
    u32 face = mw_ui_color(MW_TH_FACE), text = mw_ui_color(MW_TH_TEXT);
    mw_fill_screen(face);
    int wx = ox + PAD, wy = oy + PAD;
    mw_ui_box(wx - 3, wy - 3, BW * CELL + 6, BH * CELL + 6, 1, 0x101018);                   /* the well, sunken */
    for (int y = 0; y < BH; y++)
        for (int x = 0; x < BW; x++) {
            if (g->well[y][x]) draw_block(wx + x * CELL, wy + y * CELL, COLOR[g->well[y][x] - 1]);
            else if (((x + y) & 1) == 0) sys.fill_rect(wx + x * CELL + CELL / 2 - 1, wy + y * CELL + CELL / 2 - 1, 2, 2, 0x202838);      /* faint grid dots */
        }
    if (!g->over) {
        int gy = g->y;                                                                       /* where it would land: an outline */
        while (piece_fits(g, g->type, g->rot, g->x, gy + 1)) gy++;
        u16 m = SHAPE[g->type][g->rot];
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                if (m & (0x8000u >> (r * 4 + c))) {
                    int cx = g->x + c;
                    if (gy + r >= 0 && gy != g->y) sys.draw_rect(wx + cx * CELL + 2, wy + (gy + r) * CELL + 2, CELL - 4, CELL - 4, mw_mix(COLOR[g->type], MW_BLACK, 128));
                    if (g->y + r >= 0) draw_block(wx + cx * CELL, wy + (g->y + r) * CELL, COLOR[g->type]);
                }
    }
    int px = wx + BW * CELL + PAD + 3;
    mw_text(px, wy, "NEXT", text);
    mw_ui_box(px, wy + 16, 4 * 12 + 12, 4 * 12 + 12, 1, 0x101018);
    draw_piece_grid(g->next, 0, px + 6, wy + 22, 12, COLOR[g->next]);
    mw_printf(px, wy + 90, text, "Score");   mw_printf(px, wy + 104, text, "%u", (u32)g->score);
    mw_printf(px, wy + 130, text, "Lines");  mw_printf(px, wy + 144, text, "%d", g->lines);
    mw_printf(px, wy + 170, text, "Level");  mw_printf(px, wy + 184, text, "%d", g->level + 1);
    mw_text(px, wy + 224, "Arrows move", text); mw_text(px, wy + 238, "Up: rotate", text);
    mw_text(px, wy + 252, "Space: drop", text); mw_text(px, wy + 266, "P pause", text); mw_text(px, wy + 280, "R restart", text);
    if (g->over || g->paused) {
        int bx = wx + 8, by = wy + BH * CELL / 2 - 26, bw = BW * CELL - 16;
        mw_ui_box(bx, by, bw, 52, 0, face);
        mw_text_center(bx, by + 10, bw, g->over ? "GAME OVER" : "PAUSED", text);
        mw_text_center(bx, by + 30, bw, g->over ? "R: play again" : "P: go on", text);
    }
}

void mwp_entry(void) {
    if (!mw_has(3)) {                                       /* needs theme colours, set_size: the version-3 table */
        mw_fill_screen(MW_BLUE);
        sys.draw_string(20, 20, "NEEDS MINIWIN 1.1-ALPHA-11 OR NEWER", MW_WHITE);
        sys.present(); mw_wait_key(); return;
    }
    mw_time_t t; sys.time_now(&t);
    mw_srand(sys.ticks_ms() * 2654435761u + (u32)t.second * 977u + (u32)t.minute * 61u);
    if (mw_windowed()) sys.set_size(CLIENT_W, CLIENT_H);
    game_t g;                                               /* all the state, on the stack */
    new_game(&g);
    for (;;) {
        int k = sys.key_poll();
        if (k == MW_KEY_ESC) break;
        if (k == 'r' || k == 'R') new_game(&g);
        else if (k == 'p' || k == 'P') { if (!g.over) { g.paused = !g.paused; g.last_drop = sys.ticks_ms(); } }
        else if (!g.over && !g.paused) {
            if (k == MW_KEY_LEFT) try_move(&g, -1);
            else if (k == MW_KEY_RIGHT) try_move(&g, 1);
            else if (k == MW_KEY_UP) try_rotate(&g);
            else if (k == MW_KEY_DOWN) { if (step_down(&g)) g.score += 1; }
            else if (k == ' ') { int n = 0; while (piece_fits(&g, g.type, g.rot, g.x, g.y + 1)) { g.y++; n++; } g.score += 2 * n; lock_piece(&g); }
        }
        if (!g.over && !g.paused && sys.ticks_ms() - g.last_drop >= (u32)gravity_ms(&g)) step_down(&g);
        int ox = ((int)sys.screen_w - CLIENT_W) / 2, oy = ((int)sys.screen_h - CLIENT_H) / 2;
        if (ox < 0) ox = 0;
        if (oy < 0) oy = 0;
        draw_game(&g, ox, oy);
        sys.present();
        sys.sleep_ms(12);
    }
}
