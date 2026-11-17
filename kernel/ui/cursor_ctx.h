/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_UI_CURSOR_CTX_H
#define MW_UI_CURSOR_CTX_H

/* ui/cursor_ctx.h -- which pointer shape fits where the pointer is (1.1-alpha-14). Called every frame by draw_cursor() (ui/cursor.h), so it only
 * LOOKS: nothing here changes state. In order of priority:
 *   a window being resized      -> the resize arrow of the edge being dragged
 *   a window being dragged      -> the four-way "move" arrow
 *   a system-wide busy state    -> the hourglass (cursor_busy)
 *   an open pop-up menu         -> a "no" sign over a greyed-out entry, the arrow elsewhere
 *   the edge of a window        -> the resize arrow for that edge (a window that is maximized has none)
 *   a text area / field         -> the I-beam: Notepad's text, MiniShell's input line, MiniWeb's address bar and its text boxes
 *   a link, button or <summary> -> the hand (MiniWeb); MiniWeb while it is fetching a page -> the arrow with an hourglass
 *   a program's window          -> whatever the program asked for with set_cursor() (arrow if nothing)
 * Needs the input code (dragging_id), every app's geometry helpers, ui/ctxmenu.h and apps/progwin.h. */

static int cursor_resize_shape(int zone) {
    if (zone == (RESIZE_N | RESIZE_W) || zone == (RESIZE_S | RESIZE_E)) return MW_CURSOR_DIAG_NWSE;
    if (zone == (RESIZE_N | RESIZE_E) || zone == (RESIZE_S | RESIZE_W)) return MW_CURSOR_DIAG_NESW;
    if (zone == RESIZE_W || zone == RESIZE_E) return MW_CURSOR_HRESIZE;
    if (zone == RESIZE_N || zone == RESIZE_S) return MW_CURSOR_VRESIZE;
    return MW_CURSOR_ARROW;
}

static int cursor_pick(int x, int y) {
    if (resizing_id >= 0) return cursor_resize_shape(resizing_zone);
    if (dragging_id >= 0) return MW_CURSOR_MOVE;
    if (cursor_busy) return cursor_busy == 2 ? MW_CURSOR_WAIT : MW_CURSOR_WAIT_ARROW;
    if (pm_open) {
        int inside; const pm_item_t *it = pm_pick(x, y, &inside);
        return (it && (it->fl & MBF_OFF)) ? MW_CURSOR_NO : MW_CURSOR_ARROW;
    }
    if (start_menu_open || clock_popup_open || y >= TASKBAR_Y) return MW_CURSOR_ARROW;
    int hit_id, zone = compute_hover_resize_zone(x, y, &hit_id);
    if (hit_id < 0) return MW_CURSOR_ARROW;
    if (zone != RESIZE_NONE) return cursor_resize_shape(zone);

    if (hit_id < NOTEPAD_MAX) {                                              /* Notepad: its text area */
        notepad_t *keep = active_np;
        active_np = &notepads[hit_id];
        int in = active_np->confirm_mode == CONFIRM_NONE && np_edit_hit(x, y);
        active_np = keep;
        return in ? MW_CURSOR_IBEAM : MW_CURSOR_ARROW;
    }
    if (hit_id == WIN_ID_TERMINAL) return term_input_hit(x, y) ? MW_CURSOR_IBEAM : MW_CURSOR_ARROW;
    if (hit_id == WIN_ID_WEB) {
        if (web_urlbar_hit(x, y)) return MW_CURSOR_IBEAM;
        int fetching = web_resolving || http_client.state == HTTP_CONNECTING || http_client.state == HTTP_SENDING_REQUEST || http_client.state == HTTP_AWAITING_RESPONSE;
        int bx, by, bw, bh;
        web_body_geom(&bx, &by, &bw, &bh);
        if (web_page_ready && in_rect(x, y, bx, by, bw, bh)) {              /* over the page: a link or a control under the pointer? */
            int dx = x - bx, dy = y - by + web_scroll_px;
            for (int i = (int)L.n - 1; i >= 0; i--) {
                const rd_item_t *it = &RD_ITEMS[i];
                if (it->kind == DL_NONE || !it->ref) continue;
                if (dx < it->x || dx >= it->x + it->w || dy < it->y || dy >= it->y + it->h) continue;
                if (it->kind == DL_CTL && (it->aux == CTL_TEXT || it->aux == CTL_PASSWORD || it->aux == CTL_TEXTAREA)) return MW_CURSOR_IBEAM;
                return MW_CURSOR_HAND;
            }
        }
        return fetching ? MW_CURSOR_WAIT_ARROW : MW_CURSOR_ARROW;
    }
    if (PW_IS(hit_id)) { pw_select(hit_id); if (pw.used && !pw.finished && pw_client_hit(x, y)) return pw.cursor; }
    return MW_CURSOR_ARROW;
}

#endif
