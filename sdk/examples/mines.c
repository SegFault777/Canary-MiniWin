/* SPDX-License-Identifier: GPL-3.0-only */
/* @mwp title: Minesweeper */
/* @mwp abi: 4 */
/* mines.c -- Minesweeper (SDK 3.0 example): the mouse with both buttons, a board in memory from mw_calloc (the SDK's own heap, one block from
 * sys.mem_alloc cut into pieces), a flood fill with an explicit queue, a clock from ticks_ms, resizing the window per difficulty, and the
 * pop-up menu (version 4) for the difficulty.
 *
 *   Left click      reveal a square (the first click is always safe); on a number whose flags add up: reveal all its neighbours
 *   Right click     flag / unflag a square (beside the board: the difficulty menu)         Click the face: new game
 *   1 2 3           beginner (9x9, 10 mines), intermediate (16x16, 40), expert (30x16, 99)          Esc  quit
 *
 *   sdk/build.sh sdk/examples/mines.c        sdk/install.sh sdk/examples/mines.c MINES.MWP */
#define MW_HEAP_BYTES 8192          /* the default heap (mw_malloc ...): the biggest board is 30 x 16 squares plus the flood-fill queue */
#include "miniwin.h"

#define CELL 16
#define MARGIN 8
#define HEAD 44                     /* the strip above the board: mine counter, face, clock */

/* a square: bits 0-3 the number of mines around it, then the flags below */
#define F_MINE 0x10
#define F_OPEN 0x20
#define F_FLAG 0x40
#define F_BOOM 0x80                 /* the mine that was stepped on */

typedef struct {
    int level, cols, rows, mines;
    u8 *sq;                         /* cols * rows squares, from mw_calloc */
    u16 *queue;                     /* the flood fill's work list */
    int started, over, won;         /* started: the first click happened (mines are placed then) */
    int opened, flags;
    u32 t0, secs;
    int prev_mb, face_down;
} game_t;

static const int LEVELS[3][3] = { { 9, 9, 10 }, { 16, 16, 40 }, { 30, 16, 99 } };     /* columns, rows, mines */
static const u32 NUMCOL[9] = { 0, 0x0000FF, 0x008000, 0xFF0000, 0x000080, 0x800000, 0x008080, 0x000000, 0x808080 };

static int board_w(const game_t *g) { return MARGIN * 2 + g->cols * CELL; }
static int board_h(const game_t *g) { return HEAD + MARGIN + g->rows * CELL + MARGIN; }

static void new_game(game_t *g, int level) {
    if (g->sq) mw_free(g->sq);
    if (g->queue) mw_free(g->queue);
    g->level = level; g->cols = LEVELS[level][0]; g->rows = LEVELS[level][1]; g->mines = LEVELS[level][2];
    g->sq = mw_calloc((u32)(g->cols * g->rows), 1);
    g->queue = mw_calloc((u32)(g->cols * g->rows), sizeof(u16));
    g->started = g->over = g->won = 0; g->opened = g->flags = 0; g->t0 = 0; g->secs = 0; g->face_down = 0;
    if (mw_windowed()) sys.set_size(board_w(g), board_h(g));
}

static void place_mines(game_t *g, int safe_x, int safe_y) {          /* after the first click: never on it, nor next to it */
    int n = 0;
    while (n < g->mines) {
        int x = (int)mw_rand_range((u32)g->cols), y = (int)mw_rand_range((u32)g->rows);
        if (mw_abs(x - safe_x) <= 1 && mw_abs(y - safe_y) <= 1) continue;
        if (g->sq[y * g->cols + x] & F_MINE) continue;
        g->sq[y * g->cols + x] |= F_MINE; n++;
    }
    for (int y = 0; y < g->rows; y++)
        for (int x = 0; x < g->cols; x++) {
            int c = 0;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
                int xx = x + dx, yy = y + dy;
                if ((dx || dy) && xx >= 0 && yy >= 0 && xx < g->cols && yy < g->rows && (g->sq[yy * g->cols + xx] & F_MINE)) c++;
            }
            g->sq[y * g->cols + x] = (u8)((g->sq[y * g->cols + x] & 0xF0) | c);
        }
}

static void lose(game_t *g, int bx, int by) {
    g->over = 1;
    g->sq[by * g->cols + bx] |= F_BOOM;
    for (int i = 0; i < g->cols * g->rows; i++) if (g->sq[i] & F_MINE) g->sq[i] |= F_OPEN;       /* show every mine */
}

/* Opens a square; an empty one (no mines around) opens its neighbours too, and so on -- with a queue, not recursion. */
static void reveal(game_t *g, int x0, int y0) {
    int head = 0, tail = 0;
    u8 *s = &g->sq[y0 * g->cols + x0];
    if (*s & (F_OPEN | F_FLAG)) return;
    if (*s & F_MINE) { lose(g, x0, y0); return; }
    *s |= F_OPEN; g->opened++;
    g->queue[tail++] = (u16)(y0 * g->cols + x0);
    while (head < tail) {
        int i = g->queue[head++], x = i % g->cols, y = i / g->cols;
        if ((g->sq[i] & 15) != 0) continue;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= g->cols || yy >= g->rows) continue;
            u8 *n = &g->sq[yy * g->cols + xx];
            if (*n & (F_OPEN | F_FLAG | F_MINE)) continue;
            *n |= F_OPEN; g->opened++;
            g->queue[tail++] = (u16)(yy * g->cols + xx);
        }
    }
    if (g->opened == g->cols * g->rows - g->mines) { g->over = g->won = 1; for (int i = 0; i < g->cols * g->rows; i++) if (g->sq[i] & F_MINE) g->sq[i] |= F_FLAG; g->flags = g->mines; }
}

/* a click on an opened number whose flags add up: open the rest of its neighbours */
static void chord(game_t *g, int x, int y) {
    int n = g->sq[y * g->cols + x] & 15, fl = 0;
    if (!n) return;
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        int xx = x + dx, yy = y + dy;
        if ((dx || dy) && xx >= 0 && yy >= 0 && xx < g->cols && yy < g->rows && (g->sq[yy * g->cols + xx] & F_FLAG)) fl++;
    }
    if (fl != n) return;
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        int xx = x + dx, yy = y + dy;
        if ((dx || dy) && xx >= 0 && yy >= 0 && xx < g->cols && yy < g->rows && !g->over) reveal(g, xx, yy);
    }
}

static void draw_square(const game_t *g, int px, int py, u8 v) {
    u32 face = mw_ui_color(MW_TH_FACE);
    if (!(v & F_OPEN)) {
        mw_ui_box(px, py, CELL, CELL, 0, face);
        if (v & F_FLAG) {
            sys.fill_rect(px + 7, py + 3, 2, 9, MW_BLACK); sys.fill_rect(px + 4, py + 11, 8, 2, MW_BLACK);
            sys.fill_rect(px + 4, py + 3, 4, 4, 0xE00000);
            if (g->over && !g->won && !(v & F_MINE)) { mw_line(px + 3, py + 3, px + 12, py + 12, 0xE00000); mw_line(px + 12, py + 3, px + 3, py + 12, 0xE00000); }
        }
        return;
    }
    sys.fill_rect(px, py, CELL, CELL, (v & F_BOOM) ? 0xE00000 : face);
    mw_hline(px, py, CELL, mw_ui_color(MW_TH_SHADOW)); mw_vline(px, py, CELL, mw_ui_color(MW_TH_SHADOW));
    if (v & F_MINE) { mw_fill_circle(px + 8, py + 8, 4, MW_BLACK); sys.fill_rect(px + 4, py + 7, 9, 2, MW_BLACK); sys.fill_rect(px + 7, py + 4, 2, 9, MW_BLACK); sys.fill_rect(px + 6, py + 6, 2, 2, MW_WHITE); }
    else if (v & 15) { char d[2] = { (char)('0' + (v & 15)), 0 }; mw_text(px + 3, py + 2, d, NUMCOL[v & 15]); }
}

static int face_x(const game_t *g) { return (board_w(g) - 28) / 2; }

static void draw_game(const game_t *g, int ox, int oy) {
    u32 face = mw_ui_color(MW_TH_FACE);
    mw_fill_screen(face);
    int w = board_w(g);
    mw_ui_box(ox + 2, oy + 4, w - 4, HEAD - 8, 1, face);                                       /* the strip: counter, face, clock */
    char b[8];
    mw_snprintf(b, sizeof b, "%03d", g->mines - g->flags);
    sys.fill_rect(ox + 10, oy + 10, 46, 22, 0x200000); mw_text(ox + 15, oy + 15, b, 0xFF3030);
    mw_snprintf(b, sizeof b, "%03d", (int)(g->secs > 999 ? 999 : g->secs));
    sys.fill_rect(ox + w - 56, oy + 10, 46, 22, 0x200000); mw_text(ox + w - 51, oy + 15, b, 0xFF3030);
    int fx = ox + face_x(g), fy = oy + 8;                                                       /* the face: a button */
    mw_ui_box(fx, fy, 28, 28, g->face_down, face);
    int o = g->face_down ? 1 : 0;
    mw_fill_circle(fx + 14 + o, fy + 14 + o, 9, 0xF0D820); mw_circle(fx + 14 + o, fy + 14 + o, 9, MW_BLACK);
    if (g->over && !g->won) { mw_text(fx + 6 + o, fy + 7 + o, "x x", MW_BLACK); mw_hline(fx + 9 + o, fy + 20 + o, 10, MW_BLACK); }
    else { sys.fill_rect(fx + 9 + o, fy + 10 + o, 2, 3, MW_BLACK); sys.fill_rect(fx + 17 + o, fy + 10 + o, 2, 3, MW_BLACK);
           if (g->won) { mw_hline(fx + 8 + o, fy + 10 + o, 12, MW_BLACK); }
           mw_hline(fx + 10 + o, fy + 19 + o, 8, MW_BLACK); }
    for (int y = 0; y < g->rows; y++)
        for (int x = 0; x < g->cols; x++)
            draw_square(g, ox + MARGIN + x * CELL, oy + HEAD + y * CELL, g->sq[y * g->cols + x]);
}

void mwp_entry(void) {
    if (!mw_has(4)) {
        mw_fill_screen(MW_BLUE);
        sys.draw_string(20, 20, "NEEDS MINIWIN 1.1-ALPHA-14 OR NEWER", MW_WHITE);
        sys.present(); mw_wait_key(); return;
    }
    mw_time_t tm; sys.time_now(&tm);
    mw_srand(sys.ticks_ms() * 2654435761u + (u32)tm.second * 977u + (u32)tm.minute * 61u + 1u);
    game_t g; mw_memset(&g, 0, sizeof g);
    new_game(&g, 0);
    if (!g.sq || !g.queue) { mw_fill_screen(MW_BLUE); sys.draw_string(20, 20, "OUT OF MEMORY", MW_WHITE); sys.present(); mw_wait_key(); return; }
    for (;;) {
        int k = sys.key_poll();
        if (k == MW_KEY_ESC) break;
        if (k >= '1' && k <= '3') new_game(&g, k - '1');
        int mx = 0, my = 0, mb = 0;
        sys.mouse_poll(&mx, &my, &mb);
        int ox = ((int)sys.screen_w - board_w(&g)) / 2, oy = ((int)sys.screen_h - board_h(&g)) / 2;
        if (ox < 0) ox = 0;
        if (oy < 0) oy = 0;
        int left_down = (mb & 1) && !(g.prev_mb & 1), left_up = !(mb & 1) && (g.prev_mb & 1), right_down = (mb & 2) && !(g.prev_mb & 2);
        int on_face = mw_in_rect(mx, my, ox + face_x(&g), oy + 8, 28, 28);
        int cx = (mx - (ox + MARGIN)) / CELL, cy = (my - (oy + HEAD)) / CELL;
        int on_board = mx >= ox + MARGIN && my >= oy + HEAD && cx >= 0 && cy >= 0 && cx < g.cols && cy < g.rows;
        if (left_down && on_face) g.face_down = 1;
        if (left_up) { if (g.face_down && on_face) new_game(&g, g.level); g.face_down = 0; }
        if (on_board && !g.over && !g.face_down) {
            u8 v = g.sq[cy * g.cols + cx];
            if (left_down) {
                if (!g.started) { g.started = 1; g.t0 = sys.ticks_ms(); place_mines(&g, cx, cy); }
                if (v & F_OPEN) chord(&g, cx, cy); else reveal(&g, cx, cy);
            } else if (right_down && !(v & F_OPEN)) {
                g.sq[cy * g.cols + cx] ^= F_FLAG;
                g.flags += (g.sq[cy * g.cols + cx] & F_FLAG) ? 1 : -1;
            }
        } else if (right_down && !on_board && !on_face && my > oy + HEAD - 6) {      /* a right click beside the board: the difficulty menu */
            int c = sys.menu_popup(mx, my, "*Beginner 9x9\nIntermediate 16x16\nExpert 30x16\n-New game");
            if (c >= 0 && c < 3) new_game(&g, c); else if (c == 3) new_game(&g, g.level);
        }
        g.prev_mb = mb;
        if (g.started && !g.over) g.secs = (sys.ticks_ms() - g.t0) / 1000u;
        draw_game(&g, ox, oy);
        sys.present();
        sys.sleep_ms(15);
    }
}
