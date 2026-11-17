/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_UI_CURSOR_H
#define MW_UI_CURSOR_H

/* ui/cursor.h -- the mouse pointer: thirteen shapes and the drawing of whichever one cursor_pick() chooses.
 *
 * The shapes are the icon pack's own cursors (third_party/icon-bundle/Images/Cursor/, CC BY-SA 4.0), turned into cursor_data.h by
 * tools/gen_cursors.py: a black outline around a white fill, so the pointer shows on any background. Each is an array of row-strings
 * with a hot spot (the pixel that sits on the pointer's position). Which shape to show -- resize arrows on a window's edge, an I-beam
 * over text, a hand over a link, an hourglass while the browser is loading, the four-way arrow while a window is dragged, a "no" over a
 * greyed-out menu entry, whatever a program asked for inside its own window -- is decided by cursor_pick() in ui/cursor_ctx.h, which
 * needs every window's geometry and so comes after the input code. The numbers are the MW_CURSOR_* of sdk/include/mwp_abi.h. */

#include "../cursor_data.h"
#include "../../sdk/include/mwp_abi.h"

typedef struct { const char *const *rows; int w, h, hx, hy; } cursor_shape_t;
#define CUR_SHAPE(lower, UPPER) { cursor_##lower, CURSOR_##UPPER##_W, CURSOR_##UPPER##_H, CURSOR_##UPPER##_HX, CURSOR_##UPPER##_HY }
static const cursor_shape_t cursor_shapes[MW_CURSOR_COUNT] = {
    [MW_CURSOR_ARROW]     = CUR_SHAPE(arrow, ARROW),
    [MW_CURSOR_HELP]      = CUR_SHAPE(help, HELP),
    [MW_CURSOR_HAND]      = CUR_SHAPE(hand, HAND),
    [MW_CURSOR_IBEAM]     = CUR_SHAPE(ibeam, IBEAM),
    [MW_CURSOR_WAIT]      = CUR_SHAPE(wait, WAIT),
    [MW_CURSOR_WAIT_ARROW]= CUR_SHAPE(wait_arrow, WAIT_ARROW),
    [MW_CURSOR_MOVE]      = CUR_SHAPE(move, MOVE),
    [MW_CURSOR_NO]        = CUR_SHAPE(no, NO),
    [MW_CURSOR_PEN]       = CUR_SHAPE(pen, PEN),
    [MW_CURSOR_HRESIZE]   = CUR_SHAPE(hresize, HRESIZE),
    [MW_CURSOR_VRESIZE]   = CUR_SHAPE(vresize, VRESIZE),
    [MW_CURSOR_DIAG_NWSE] = CUR_SHAPE(diag_nwse, DIAG_NWSE),
    [MW_CURSOR_DIAG_NESW] = CUR_SHAPE(diag_nesw, DIAG_NESW),
};

static int cursor_busy = 0;              /* 0 = not busy, 1 = working in the background (arrow + hourglass), 2 = the system is blocked (hourglass) */
static int cursor_pick(int x, int y);    /* ui/cursor_ctx.h */

static void draw_cursor_shape(int x, int y, const char *const *rows, int nrows) {
    for (int j = 0; j < nrows; j++) {
        const char *row = rows[j];
        for (int i = 0; row[i]; i++) {
            if (row[i] == 'A') bb_putpixel(x + i, y + j, TH_CURSOR_OUTLINE);
            else if (row[i] == 'B') bb_putpixel(x + i, y + j, TH_CURSOR_FILL);
        }
    }
}

/* POINTER TRAILS (Settings > Mouse > Motion, 1.1-alpha-14): with cursor_trail_n > 0 the last cursor_trail_n positions, sampled every 28 ms, are drawn as
 * ghost copies of the pointer behind it. Stand still and they all sit under the real pointer, so it is just one pointer again. */
static int cursor_trail_n = 0;                                      /* 0 = off, else how many ghosts (2 .. 8) */
static int cursor_trail_x[8], cursor_trail_y[8];
static u32 cursor_trail_t = 0;
static u32 mwp_sys_ticks_ms(void);                                  /* mwp_api2.h: the millisecond clock */

static void draw_cursor(int x, int y) {
    const cursor_shape_t *s = &cursor_shapes[cursor_pick(x, y)];
    if (cursor_trail_n > 0) {
        static int frames = 0;
        u32 now = mwp_sys_ticks_ms();
        frames++;
        /* a sample every 28 ms -- or, when the millisecond clock does not run (no usable TSC), every 6th frame, so the trail still follows */
        if ((int)(now - cursor_trail_t) >= 28 || (int)(now - cursor_trail_t) < 0 || frames >= 6) {
            for (int i = 7; i > 0; i--) { cursor_trail_x[i] = cursor_trail_x[i - 1]; cursor_trail_y[i] = cursor_trail_y[i - 1]; }
            cursor_trail_x[0] = x; cursor_trail_y[0] = y; cursor_trail_t = now; frames = 0;
        }
        for (int i = cursor_trail_n - 1; i >= 0; i--)               /* oldest first, so the real pointer ends up on top */
            if (cursor_trail_x[i] != x || cursor_trail_y[i] != y) draw_cursor_shape(cursor_trail_x[i] - s->hx, cursor_trail_y[i] - s->hy, s->rows, s->h);
    }
    draw_cursor_shape(x - s->hx, y - s->hy, s->rows, s->h);
}

#endif
