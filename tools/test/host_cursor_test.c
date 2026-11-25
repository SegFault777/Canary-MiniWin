/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test for the pointer trails of kernel/ui/cursor.h (Settings > Mouse > Motion > "Show pointer trails").
 * draw_cursor() is run with a fake millisecond clock and a canvas that bb_putpixel() paints, so what the screen would show is checkable:
 *   - trails off: one pointer, nothing else;
 *   - trails on: the pointer plus ghosts at the positions of the last cursor_trail_n samples (one sample per 28 ms, newest first), none
 *     under the pointer itself, and none at (0,0) just because the history started empty;
 *   - the pointer standing still: the ghosts gather under it and are gone;
 *   - a longer trail shows more ghosts than a shorter one;
 *   - a clock that does not run (every call says 0 ms): a sample every 6th frame, so the trail still follows. */
#include "rd_host.h"
#include <string.h>

#define TH_CURSOR_OUTLINE 0xAA0001
#define TH_CURSOR_FILL    0xAA0002
#define CW 320
#define CH 200
static unsigned canvas[CH][CW];
static void bb_putpixel(int x, int y, unsigned c) { if (x >= 0 && y >= 0 && x < CW && y < CH) canvas[y][x] = c; }
static u32 fake_ms = 0; static int clock_runs = 1;
static u32 mwp_sys_ticks_ms(void) { return clock_runs ? fake_ms : 0; }
static int cursor_pick(int x, int y) { (void)x; (void)y; return 0; }          /* always the arrow */

#include "../../kernel/ui/cursor.h"

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); printf(" " __VA_ARGS__); printf("\n"); } } while (0)

/* the pixel the pointer's hot spot sits on is part of the arrow: is a pointer drawn at (x,y)? */
static int drawn(int x, int y) {
    const cursor_shape_t *s = &cursor_shapes[MW_CURSOR_ARROW];
    for (int j = 0; j < s->h; j++) for (int i = 0; s->rows[j][i]; i++)
    {   int cy = y - s->hy + j, cx = x - s->hx + i;
        if (s->rows[j][i] == 'A' && cy >= 0 && cx >= 0 && cy < CH && cx < CW && canvas[cy][cx] == TH_CURSOR_OUTLINE) return 1; }
    return 0;
}
static int one_ink = 1;
static int count_pointers(void) {                                                  /* the outline pixels on the canvas, in pointers (ghosts must not overlap in these tests) */
    int ink = 0;
    for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++) ink += canvas[y][x] == TH_CURSOR_OUTLINE;
    return (ink + one_ink / 2) / one_ink;
}
static void frame(int x, int y, u32 ms) { memset(canvas, 0, sizeof canvas); fake_ms = ms; draw_cursor(x, y); }

int main(void) {
    /* trails off */
    cursor_trail_n = 0;
    frame(100, 100, 1000);
    CHECK(drawn(100, 100), "the pointer is drawn");
    for (int y = 0; y < CH; y++) for (int x = 0; x < CW; x++) one_ink += canvas[y][x] == TH_CURSOR_OUTLINE;
    one_ink--;                                                                       /* it started at 1 */
    CHECK(one_ink > 10, "the arrow has an outline (%d pixels)", one_ink);
    CHECK(count_pointers() == 1, "only one pointer without trails (%d)", count_pointers());

    /* trails on, four ghosts: the history starts at the pointer, not at (0,0) */
    cursor_trail_n = 4;
    frame(100, 100, 2000);
    CHECK(!drawn(0, 0) && !drawn(1, 1), "no ghost at (0,0) when the trail is switched on");
    CHECK(count_pointers() == 1, "one pointer to start with (%d)", count_pointers());

    /* move right, one frame every 30 ms (a sample each) */
    frame(130, 100, 2030); frame(160, 100, 2060); frame(190, 100, 2090); frame(220, 100, 2120);
    CHECK(drawn(220, 100), "the pointer is at the front");
    CHECK(drawn(190, 100) && drawn(160, 100) && drawn(130, 100), "ghosts at the three positions before it");
    CHECK(!drawn(100, 100), "only n=4 samples are kept: the position before them is gone");
    CHECK(count_pointers() == 4, "the pointer and three ghosts (%d)", count_pointers());
    /* a frame within 28 ms of the last sample takes no new sample */
    frame(230, 100, 2125);
    CHECK(drawn(230, 100) && drawn(190, 100) && drawn(160, 100) && drawn(130, 100), "no new sample inside 28 ms");

    /* standing still: after n samples the ghosts have gathered under the pointer */
    for (int i = 1; i <= 6; i++) frame(230, 100, 2125 + 30 * (u32)i);
    CHECK(count_pointers() == 1 && drawn(230, 100), "standing still leaves one pointer (%d)", count_pointers());

    /* a longer trail shows more ghosts than a shorter one */
    cursor_trail_n = 8;
    for (int i = 0; i < 12; i++) frame(10 + 24 * i, 60, 3000 + 30 * (u32)i);
    int n8 = count_pointers();
    cursor_trail_n = 2;
    frame(10 + 24 * 12, 60, 3000 + 30 * 12);
    int n2 = count_pointers();
    CHECK(n8 == 8 && n2 == 2, "a trail of 8 samples shows the pointer and 7 ghosts, of 2 the pointer and 1 (%d vs %d)", n8, n2);

    /* switching the trail off and on again starts fresh at the pointer, not at the old positions */
    cursor_trail_n = 0; frame(250, 150, 4000);
    cursor_trail_n = 4; frame(250, 150, 4030);
    CHECK(count_pointers() == 1, "switched on again: no stale ghosts (%d)", count_pointers());

    /* a clock that does not run: a sample every 6th frame */
    clock_runs = 0; cursor_trail_n = 3;
    for (int i = 0; i < 40; i++) frame(10 + 7 * i, 120, 0);
    CHECK(count_pointers() >= 2, "the trail still follows without a clock (%d)", count_pointers());

    printf("cursor: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
