/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_APPS_WEB_VIEW_H
#define MW_APPS_WEB_VIEW_H

/* apps/web/view.h -- MINIWEB.MWP drawing: window chrome, address bar, page body, scrolling, clicks, form submit.
 * (Split out of kernel.c in pre-29; it is still one translation unit, so modules are included by
 * kernel.c in dependency order -- see the module map there.) */

/* Where the page body (text area + scrollbar) sits inside the window. One function so drawing,
 * scrolling and click hit-testing can never disagree about the geometry. */
static void web_body_geom(int *bx, int *by, int *bw, int *bh) {
    *bx = web_win.x + 3;
    *by = web_content_y();
    *bw = web_win.w - 6 - WEB_SB_W;
    *bh = web_status_y() - 2 - *by;
}
#define WEB_LINE_H FONT_LINE_H             /* the failure screens still draw plain text lines */
#define WEB_SCROLL_STEP (3 * 14)                /* one arrow-key press: three body-text lines, in pixels */
static int web_view_h(void) {
    int bx, by, bw, bh;
    web_body_geom(&bx, &by, &bw, &bh);
    return bh < 1 ? 1 : bh;
}
static void web_scroll_by(int px) {
    int bh = web_view_h();
    int max = web_doc_h > bh ? web_doc_h - bh : 0;
    int sc = web_scroll_px + px;
    if (sc < 0) sc = 0;
    if (sc > max) sc = max;
    web_scroll_px = sc;
}
/* Keeps the layout in step with the window's width (the window can be resized or maximized). */
static void web_ensure_layout(void) {
    int bx, by, bw, bh;
    web_body_geom(&bx, &by, &bw, &bh);
    web_doc_h = rd_relayout(bw, bh, 0);
    web_scroll_by(0);        /* re-clamp */
}

/* ---- the Hangul IME in a page's text field (1.1-alpha-14) ----
 * A field's text is only ever appended to (render.h rd_ctl_put_bytes), so the syllable being composed is simply kept at the END of the text
 * as a real 3-byte character that every IME key replaces -- the page paints it like any other text. Committing is forgetting that it is live. */
static u8 web_ime_live = 0;
static void web_ime_flush(void) {
    if (!web_ime_live) return;
    web_ime_live = 0;
    if (ko_ime_is_composing()) ko_ime_reset();
}
static void web_ime_put(rd_ctl_t *s) {
    int cp = ko_ime_preview_codepoint();
    if (cp < 0) return;
    u8 b3[3] = { (u8)(0xE0 | (cp >> 12)), (u8)(0x80 | ((cp >> 6) & 0x3F)), (u8)(0x80 | (cp & 0x3F)) };
    if (rd_ctl_put_bytes(b3, 3)) web_ime_live = 3;
    (void)s;
}
/* One key for the focused page field. Returns what rd_ctl_key() returns: 0 nothing, 1 the text changed, 2 Enter (submit). */
static int web_ctl_key(int k) {
    rd_ctl_t *s = rd_focus ? rd_ctl_find(rd_focus, 0) : 0;
    if (!s) return 0;
    if (web_ime_live && !ko_ime_is_composing()) web_ime_live = 0;
    int pass = lay_input_kind(s->node) == CTL_PASSWORD;                       /* a password is typed as Latin */
    int typing = (k >= 32 && k < 127) || k == '\b';
    if (current_ime == IME_KOREAN && !pass && typing) {
        int was_live = web_ime_live;
        if (was_live && s->len >= 3) s->len -= 3;                             /* take the preview out; it is put back below */
        web_ime_live = 0;
        if (k == '\b') {
            if (was_live) {
                if (ko_ime_backspace() && ko_ime_is_composing()) web_ime_put(s);
                return 1;
            }
        } else {
            char sc[8]; u32 sl = 0;
            int used = ko_ime_feed_key((char)k, sc, &sl, sizeof sc);          /* a finished syllable (if any) ... */
            if (sl) rd_ctl_put_bytes((const u8 *)sc, (int)sl);                /* ... is committed to the text */
            if (used) { if (ko_ime_is_composing()) web_ime_put(s); return 1; }
        }
    } else web_ime_flush();                                                   /* Enter, a non-typing key, the Latin mode: commit */
    return rd_ctl_key(k);
}

/* ---- explaining a failed page load, in words ---- */
static const char *web_tls_fail_text(void) {
    switch (tls_conn.fail_reason) {
        case TLS_FAIL_TCP:                     return "The connection closed during the secure handshake.";
        case TLS_FAIL_UNEXPECTED_MESSAGE:      return "The server sent something unexpected during the handshake (protocol error).";
        case TLS_FAIL_UNSUPPORTED_CIPHER_SUITE:return "The server picked an encryption method MiniWin does not support.";
        case TLS_FAIL_UNSUPPORTED_CURVE:       return "The server's key-exchange curve is not supported (MiniWin: x25519, P-256, P-384).";
        case TLS_FAIL_CERT_PARSE:              return "The server's certificate could not be read.";
        case TLS_FAIL_CERT_CHAIN:              return "The certificate chain is broken, expired, or uses an unsupported signature.";
        case TLS_FAIL_CERT_UNTRUSTED:          return "The server's certificate is not signed by a root MiniWin trusts.";
        case TLS_FAIL_HOSTNAME_MISMATCH:       return "The certificate is for a different host name than the one requested.";
        case TLS_FAIL_SKE_SIGNATURE:           return "The server's key-exchange signature did not verify.";
        case TLS_FAIL_DECRYPT:                 return "A secure record failed its integrity check (data was corrupted).";
        case TLS_FAIL_SERVER_FINISHED:         return "The server's Finished message did not verify.";
        case TLS_FAIL_BUFFER_OVERFLOW:         return "A handshake message was larger than MiniWin can handle.";
        case TLS_FAIL_PEER_ALERT:              return "The server refused the connection with a TLS alert (see the number below).";
        case TLS_FAIL_CERT_EXTENSION:          return "The certificate has an extension MiniWin cannot safely handle, or its key usage does not allow this use.";
        case TLS_FAIL_BAD_RECORD:              return "The server sent a malformed or oversized secure record.";
        case TLS_FAIL_PROTOCOL_VERSION:        return "The server chose a TLS version or key-exchange group MiniWin did not offer (MiniWin speaks TLS 1.3 and 1.2, with x25519).";
        default:                               return "The secure connection failed.";
    }
}

/* Word-wraps `text` into the page area starting at *row; advances *row. */
static void web_draw_wrapped(int *row, const char *text, u32 color) {
    int bx, by, bw, bh;
    web_body_geom(&bx, &by, &bw, &bh);
    u32 cols = (u32)(bw / FONT_CELL);
    if (cols < 10) cols = 10;
    u32 n = 0; while (text[n]) n++;
    u32 pos = 0;
    while (pos < n && (*row + 1) * WEB_LINE_H <= bh) {
        u32 end = pos + cols;
        if (end >= n) end = n;
        else { u32 k = end; while (k > pos && text[k] != ' ') k--; if (k > pos) end = k; }
        char line[160]; u32 ll = 0;
        for (u32 i = pos; i < end && ll + 1 < sizeof(line); i++) line[ll++] = text[i];
        line[ll] = 0;
        font_draw_string(bx, by + *row * WEB_LINE_H, line, color);
        (*row)++;
        pos = end; while (pos < n && text[pos] == ' ') pos++;
    }
}

static void draw_web_failure(void) {
    int row = 0;
    char head[140]; u32 hl = 0;
    if (web_waiting_net) {
        web_draw_wrapped(&row, "This computer has no IP address yet, so the page cannot be requested.", TH_TEXT);
        row++;
        web_draw_wrapped(&row, "MiniWin is asking the network's DHCP server for one and will keep retrying; the page loads by itself as soon as the answer arrives.", TH_TEXT);
        if (dhcp_tries >= 2) {
            row++;
            web_draw_wrapped(&row, "Still no answer. Make sure the virtual machine's network adapter is connected and supported (e1000 or rtl8139), e.g. QEMU: -netdev user,id=n0 -device e1000,netdev=n0", TH_SHADOW);
        }
        return;
    }
    if (web_dns_failed) {
        append_str(head, &hl, sizeof(head), "Could not find \"");
        append_str(head, &hl, sizeof(head), web_last_host);
        append_str(head, &hl, sizeof(head), "\".");
        web_draw_wrapped(&row, head, TH_TEXT);
        row++;
        web_draw_wrapped(&row, dns_client.fail[0] ? dns_client.fail : "The DNS lookup failed.", TH_TEXT);
        row++;
        web_draw_wrapped(&row, "Check the spelling, and that the virtual machine has working network access (DNS comes from the DHCP server).", TH_SHADOW);
        return;
    }
    int https_failed = web_use_https && https_client.state == HTTPS_FAILED;
    int http_failed = !web_use_https && http_client.state == HTTP_FAILED;
    if (!https_failed && !http_failed) return;
    if (hr_size_fatal()) {
        /* Not a connection problem at all: the page arrived (or was announced) but is bigger than the response
         * buffer, and a cut-off page is worse than none. Say so instead of "could not connect". */
        append_str(head, &hl, sizeof(head), "The page from ");
        append_str(head, &hl, sizeof(head), web_last_host);
        append_str(head, &hl, sizeof(head), " is too large to display.");
        web_draw_wrapped(&row, head, TH_TEXT);
        row++;
        web_draw_wrapped(&row, "MiniWin keeps a page in a fixed-size buffer (256 KB). Showing only the first part would change what the page means, so nothing is shown.", TH_TEXT);
        return;
    }
    append_str(head, &hl, sizeof(head), https_failed ? "Could not open a secure connection to " : "Could not connect to ");
    append_str(head, &hl, sizeof(head), web_last_host);
    append_str(head, &hl, sizeof(head), ".");
    web_draw_wrapped(&row, head, TH_TEXT);
    row++;
    if (https_failed) {
        web_draw_wrapped(&row, web_tls_fail_text(), TH_TEXT);
        char code[64]; u32 cl = 0;
        append_str(code, &cl, sizeof(code), "(error ");
        char d[12]; u32 v = (u32)tls_conn.fail_reason; int nd = 0;
        do { d[nd++] = (char)('0' + v % 10); v /= 10; } while (v && nd < 6);
        while (nd) { char one[2] = { d[--nd], 0 }; append_str(code, &cl, sizeof(code), one); }
        if (tls_conn.fail_reason == TLS_FAIL_PEER_ALERT) {
            append_str(code, &cl, sizeof(code), ", alert ");
            v = tls_conn.alert_desc; nd = 0;
            do { d[nd++] = (char)('0' + v % 10); v /= 10; } while (v && nd < 6);
            while (nd) { char one[2] = { d[--nd], 0 }; append_str(code, &cl, sizeof(code), one); }
        }
        append_str(code, &cl, sizeof(code), ")");
        row++;
        web_draw_wrapped(&row, code, TH_SHADOW);
    } else {
        web_draw_wrapped(&row, "The server did not answer, or refused the connection.", TH_TEXT);
    }
}

/* Draws the rendered page into the window body (the engine clips to it), then the scrollbar. */

/* ---------------------------------------------------------------- MiniWeb's menu bar: the page, the sites, help */
#define WEB_MENU_N 5
static const mb_menu_t web_menus[WEB_MENU_N] = {
    { { "File", "파일" }, 2, { { "Open address", "주소 열기" }, { "Close", "닫기" } } },
    { { "Edit", "편집" }, 1, { { "Select address", "주소 모두 선택" } } },
    { { "View", "보기" }, 3, { { "Refresh", "새로 고침" }, { "Stop", "중지" }, { "Top of page", "맨 위로" } } },
    { { "Go", "이동" }, 5, { { "Back", "뒤로" }, { "Forward", "앞으로" }, { "Home", "홈" }, { "PyPI (HTTPS)", "PyPI (HTTPS)" }, { "Gateway (HTTP)", "게이트웨이 (HTTP)" } } },
    { { "Help", "도움말" }, 1, { { "About MiniWeb", "MiniWeb 정보" } } },
};
static void web_tb_fire(int i);
static void web_menu_action(int menu, int item) {
    web_links_open = 0;
    if (menu == 0) {
        if (item == 0) { web_urlbar_focused = 1; rd_focus = 0; tf_select_all(&web_urlbar_tf, web_urlbar_buf); }   /* like a browser's Ctrl+L: all selected */
        else { web_win.open = 0; win_z_remove(WIN_ID_WEB); }
    } else if (menu == 1) {
        web_urlbar_focused = 1; rd_focus = 0; tf_select_all(&web_urlbar_tf, web_urlbar_buf);
    } else if (menu == 2) {
        if (item == 0) web_tb_fire(3); else if (item == 1) web_tb_fire(2); else web_scroll_by(-1000000);
    } else if (menu == 3) {
        web_urlbar_focused = 0;
        if (item == 0) web_tb_fire(0); else if (item == 1) web_tb_fire(1); else if (item == 2) web_tb_fire(4);
        else web_go_link(item == 3 ? WEB_SITE_PYPI : WEB_SITE_GATEWAY);
    } else web_about_open = 1;
}

/* ---------------------------------------------------------------- the toolbar */
static const struct { const char *icon, *en, *ko; } web_tb_def[WEB_TB_N] = {
    { "PREV", "Back", "뒤로" }, { "NEXT", "Forward", "앞으로" }, { "ERROR", "Stop", "중지" }, { "REFRESH", "Refresh", "새로 고침" },
    { "HOME", "Home", "홈" }, { "SEARCH", "Search", "검색" }, { "FONT", "Font", "글꼴" }, { "MAIL", "Mail", "메일" }, { "EDIT", "Edit", "편집" },
};
static int web_tb_enabled(int i) {
    if (i == 0) return web_can_back();
    if (i == 1) return web_can_forward();
    if (i == 2) return web_busy();
    if (i == 3) return web_urlbar_buf[0] != 0;
    return 1;
}
static void web_tb_fire(int i) {
    if (i == WEB_TB_LINKS) { web_links_open = !web_links_was_open; return; }
    if (i < 0 || i >= WEB_TB_N || !web_tb_enabled(i)) return;
    if (i == 0) web_hist_go(-1);
    else if (i == 1) web_hist_go(1);
    else if (i == 2) web_stop();
    else if (i == 3) {                                                           /* Refresh: the page that is shown (the Address row may hold a shortened or edited text) */
        if (web_last_host[0]) { web_current_site = WEB_SOURCE_URLBAR; web_redirects_left = 5; web_start_fetch(web_use_https, web_last_host, web_last_port, web_last_path); }
        else web_go_url();
    }
    else if (i == 4 || i == 5) web_home();                                       /* Home and Search: the search page (DuckDuckGo lite) */
    else status = SKO("This button does nothing yet.", "이 단추는 아직 동작하지 않습니다.");   /* Font, Mail, Edit */
}
static void web_draw_toolbar(void) {
    int wx = web_win.x, ww = web_win.w, ty = web_tb_y();
    for (int i = 0; i < WEB_TB_N; i++) {
        if (!web_tb_shown(i)) break;
        int x = web_tb_x(i), p = (pressed_btn_kind == BTN_WEB_GO && pressed_btn_win == WIN_ID_WEB && web_tb_pressed == i && web_tb_enabled(i)) ? 1 : 0;
        int en = web_tb_enabled(i);
        if (p) { bb_fillrect(x, ty, WEB_TB_BW, WEB_TB_H, TH_FACE); ui_bevel(x, ty, WEB_TB_BW, WEB_TB_H, 1); }
        const u32 *ic = sheet_icon32(web_tb_def[i].icon);
        if (ic) blit_rgba32(x + (WEB_TB_BW - 32) / 2 + p, ty + 3 + p, ic);
        const char *lab = SKO(web_tb_def[i].en, web_tb_def[i].ko);
        int lw = ko_string_width(lab), lx = x + (WEB_TB_BW - lw) / 2 + p, ly = ty + 36 + p;
        if (!en) {                                                               /* greyed: the icon is crossed by a light dither, the label is embossed */
            for (int yy = 0; yy < 32; yy += 2) for (int xx = (yy / 2) & 1; xx < 32; xx += 2) bb_putpixel(x + (WEB_TB_BW - 32) / 2 + xx, ty + 3 + yy, TH_FACE);
            ko_draw_mixed_string(lx + 1, ly + 1, lab, TH_LIGHT);
            ko_draw_mixed_string(lx, ly, lab, TH_SHADOW);
        } else ko_draw_mixed_string(lx, ly, lab, TH_TEXT);
    }
    if (web_logo_shown()) {                                                      /* the logo box at the right end */
        int lx = wx + ww - 4 - WEB_LOGO_W;
        ui_sunken(lx, ty, WEB_LOGO_W, WEB_TB_H);
        const u32 *lg = sheet_icon32("MINIWIN");
        if (lg) blit_rgba32(lx + (WEB_LOGO_W - 32) / 2, ty + 3, lg);
        const char *nm = "MiniWeb"; ko_draw_mixed_string(lx + (WEB_LOGO_W - ko_string_width(nm)) / 2, ty + 36, nm, TH_TEXT);
    }
    ui_hline(wx + 2, ty + WEB_TB_H + 1, ww - 4, TH_SHADOW);
    ui_hline(wx + 2, ty + WEB_TB_H + 2, ww - 4, TH_LIGHT);
}
static void web_draw_links_pop(void) {
    if (!web_links_open) return;
    static const char *const lab[WEB_SITE_COUNT] = { "PyPI.org (HTTPS)", "Gateway (10.0.2.2)" };
    int px = web_links_pop_x(), py = web_links_row_y(0) - 1, ph = WEB_SITE_COUNT * WEB_LINKS_ROW_H + 2;
    bb_fillrect(px, py, WEB_LINKS_POP_W, ph, TH_FIELD); ui_bevel(px, py, WEB_LINKS_POP_W, ph, 0);
    for (int i = 0; i < WEB_SITE_COUNT; i++) {
        u32 fg = ui_item(px + 1, web_links_row_y(i), WEB_LINKS_POP_W - 2, WEB_LINKS_ROW_H, web_current_site == i);
        ko_draw_mixed_string(px + 6, web_links_row_y(i) + 2, lab[i], fg);
    }
}

/* ---------------------------------------------------------------- the About box (MiniWin's own words) */
#define WEB_ABOUT_W 330
#define WEB_ABOUT_H 128
static inline int web_about_x(void) { return dlg_ox(DLG_WEB); }        /* the About box is a window of its own now (ui/dialog.h); its contents start at its corner */
static inline int web_about_y(void) { return dlg_oy(DLG_WEB); }
static inline int web_about_ok_x(void) { return web_about_x() + (WEB_ABOUT_W - 70) / 2; }
static inline int web_about_ok_y(void) { return web_about_y() + WEB_ABOUT_H - 30; }
static int web_about_ok_hit(int mx, int my) { return in_rect(mx, my, web_about_ok_x(), web_about_ok_y(), 70, 22); }
static void web_draw_about(void) {
    if (!web_about_open) return;
    int x = web_about_x(), y = web_about_y();                              /* (the frame and the title bar belong to the window: dlg_draw) */
    const u32 *ic = sheet_icon32("WEB"); if (ic) blit_rgba32(x + 14, y + 28, ic);
    ko_draw_mixed_string(x + 56, y + 28, "MiniWeb  (MiniWin 1.1-alpha-14)", TH_TEXT);
    ko_draw_mixed_string(x + 56, y + 43, SKO("HTML5 + JavaScript engine", "HTML5 + 자바스크립트 엔진"), TH_TEXT);
    ko_draw_mixed_string(x + 56, y + 57, SKO("HTTP, HTTPS (TLS 1.2 / 1.3), HTTP/3", "HTTP, HTTPS (TLS 1.2 / 1.3), HTTP/3"), TH_TEXT);
    ko_draw_mixed_string(x + 56, y + 71, SKO("Free software: GPL-3.0-only", "자유 소프트웨어: GPL-3.0-only"), TH_TEXT);
    ui_pushbutton(web_about_ok_x(), web_about_ok_y(), 70, 22, "OK", pressed_btn_kind == BTN_WEB_GO && web_tb_pressed == 99, 1);
}

static void draw_web_page(void) {
    web_ensure_layout();
    int bx, by, bw, bh;
    web_body_geom(&bx, &by, &bw, &bh);
    rd_paint(bx, by, bw, bh, web_scroll_px);

    sb_draw(WIN_ID_WEB);                              /* the scroll bar (ui/scrollbar.h; geometry in ui/scroll_targets.h) */
}

/* A form was submitted (a button clicked, or Enter pressed in a text field): build the query string and
 * GET (or POST) the form's action. */
#define WEB_QUERY_MAX 480
static void web_submit_form(u32 form, u32 submitter) {
    if (js_dom_submit(form)) return;                    /* onsubmit="return false" / preventDefault(): stay on the page */
    char q[WEB_QUERY_MAX], method[8], action[WEB_PATH_MAX], ref[WEB_PATH_MAX];
    int qlen = rd_form_query(form, submitter, q, sizeof(q));
    web_attr_copy(form, AT_METHOD, method, sizeof(method));
    web_attr_copy(form, AT_ACTION, action, sizeof(action));
    int post = (method[0] == 'p' || method[0] == 'P');
    if (action[0] == 0) kstrcpy(action, web_last_path, sizeof(action));       /* no action="": the page itself */
    for (u32 i = 0; action[i]; i++) if (action[i] == '?' || action[i] == '#') { action[i] = 0; break; }
    /* Every stage below used to cut the data off silently (the encoder at WEB_QUERY_MAX, the request path at
     * WEB_PATH_MAX, the POST body at HR_POST_MAX) and send what was left as if it were the whole form. A form
     * that does not fit is not sent at all, and the user is told. */
    u32 alen = 0; while (action[alen]) alen++;
    if (rd_query_overflow || (post ? (u32)qlen >= HR_POST_MAX : alen + 1 + (u32)qlen + 1 > sizeof(ref))) {
        status = t(STR_FORM_TOO_LONG);
        return;
    }
    u32 rl = 0; ref[0] = 0;
    append_str(ref, &rl, sizeof(ref), action);
    if (!post) { append_str(ref, &rl, sizeof(ref), "?"); append_str(ref, &rl, sizeof(ref), q); }
    web_navigate_ref(ref);
    if (post) hr_set_post(q);                    /* armed AFTER web_start_fetch() disarmed it; the request is built later */
}

/* A mouse click somewhere in the page area: on the scrollbar it scrolls; on the page it follows a link,
 * pokes a form control, or opens/closes a <details>. Returns 1 if the click landed in the body region at all
 * (so the caller doesn't treat it as a click on nothing). */
/* The href of the link under (mx,my) on the shown page (for the right-click menu); 0 = no link there. */
static int web_link_at(int mx, int my, char *href, u32 cap) {
    int bx, by, bw, bh;
    web_body_geom(&bx, &by, &bw, &bh);
    if (!web_page_ready || !in_rect(mx, my, bx, by, bw, bh)) return 0;
    u32 node;
    if (rd_hit(mx - bx, my - by + web_scroll_px, &node) != HIT_LINK) return 0;
    web_attr_copy(node, AT_HREF, href, cap);
    return href[0] != 0;
}

static int web_page_click(int mx, int my) {
    int bx, by, bw, bh;
    web_body_geom(&bx, &by, &bw, &bh);
    if (!in_rect(mx, my, bx, by, bw + WEB_SB_W, bh)) return 0;
    if (mx >= bx + bw) return 1;                      /* the scroll bar: ui/input.h gave it the press already (sb_press) */
    if (!web_page_ready) return 1;
    u32 node;
    int hit = rd_hit(mx - bx, my - by + web_scroll_px, &node);
    rd_focus = 0;
    if (hit != HIT_NONE && js_dom_click(node)) return 1;       /* a click handler called preventDefault(): the browser does nothing else */
    if (hit == HIT_LINK) {
        char href[WEB_PATH_MAX];
        web_attr_copy(node, AT_HREF, href, sizeof(href));
        if (href[0]) web_navigate_ref(href);
    } else if (hit == HIT_TOGGLE) {
        u32 d = RD_NODES[node].parent;                /* the <details> that owns this <summary> */
        if (d) { rd_toggle_flip(d); rd_invalidate(); }
    } else if (hit == HIT_CTL) {
        rd_node_t *e = &RD_NODES[node];
        if (e->tag == TG_SELECT) { rd_select_next(node); }
        else if (e->tag == TG_TEXTAREA) { rd_ctl_focus(node); }
        else if (e->tag == TG_INPUT || e->tag == TG_BUTTON) {
            int k = e->tag == TG_INPUT ? lay_input_kind(node) : CTL_BUTTON;
            u32 tl; const u8 *ty = dom_attr(node, AT_TYPE, &tl);
            if (k == CTL_TEXT || k == CTL_PASSWORD) rd_ctl_focus(node);
            else if (k == CTL_CHECK) { rd_ctl_set_checked(node, !rd_ctl_is_checked(node)); js_dom_change(node); }
            else if (k == CTL_RADIO) { rd_radio_select(node); js_dom_change(node); }
            else {                                    /* a button: submit unless it says otherwise */
                int plain = ty && (css_kw(ty, tl, "button") || css_kw(ty, tl, "reset"));
                u32 f = rd_find_form(node);
                if (f && !plain) web_submit_form(f, node);
            }
        }
    }
    return 1;
}

static void draw_web_window(void) {
    int wx = web_win.x, wy = web_win.y, ww = web_win.w, wh = web_win.h;

    ui_window_frame_ic(wx, wy, ww, wh, web_win.maximized, "MINIWEB.MWP", IC_WEB);
    ui_titlebar_buttons(web_btn_min_x(), web_btn_max_x(), web_btn_close_x(), web_btn_y(),
                        pressed_btn_kind == BTN_MIN && pressed_btn_win == WIN_ID_WEB,
                        pressed_btn_kind == BTN_MAX && pressed_btn_win == WIN_ID_WEB,
                        pressed_btn_kind == BTN_CLOSE && pressed_btn_win == WIN_ID_WEB);
    mb_draw_bar(WIN_ID_WEB, wx, wy, ww, web_menus, WEB_MENU_N);

    /* the Address row: the word, a sunken field (kerned text, caret and selection while focused) and the Links button */
    {
        int ux = web_urlbar_x(), uy = web_urlbar_y(), uw = web_urlbar_w();
        ko_draw_mixed_string(wx + 8, uy + 3, SKO("Address", "주소"), TH_TEXT);
        ui_field(ux, uy, uw, WEB_URLBAR_H, TH_FIELD, web_urlbar_focused);
        tf_draw(&web_urlbar_tf, web_urlbar_buf, TF_ASCII, ux + 3, uy + 3, uw - 6, 0, web_urlbar_focused, TH_FIELD_TEXT);
        const char *ll = "Links";
        ui_pushbutton(web_links_x(), uy, WEB_LINKS_W, WEB_URLBAR_H, ll, (pressed_btn_kind == BTN_WEB_GO && pressed_btn_win == WIN_ID_WEB && web_tb_pressed == WEB_TB_LINKS) || web_links_open, 1);
    }
    web_draw_toolbar();
    ui_hline(wx + 2, web_content_y() - 2, ww - 4, TH_SHADOW);

    /* the status bar at the bottom: what is happening, in plain words, then the page's <title> */
    const char *status_text;
    if (web_stopped) {
        status_text = SKO("Stopped.", "중지되었습니다.");
    } else if (web_current_site == -1) {
        /* -1: nothing requested yet this boot (the initial value).
         * WEB_SOURCE_URLBAR (-2) and the two WEB_SITE_* bookmark
         * indices (0, 1) all mean "something's been requested" and
         * fall through to the resolving/connecting states below. */
        status_text = SKO("Type an address or a search, then press Enter.", "주소나 검색어를 입력하고 Enter를 누르세요.");
    } else if (web_waiting_net) {
        status_text = "Waiting for an IP address (DHCP)...";
    } else if (web_resolving) {
        static char rbuf[128]; u32 blen = 0; rbuf[0] = 0;
        append_str(rbuf, &blen, sizeof(rbuf), "Resolving ");
        append_str(rbuf, &blen, sizeof(rbuf), web_last_host);
        append_str(rbuf, &blen, sizeof(rbuf), "...");
        status_text = rbuf;
    } else if (web_dns_failed) {
        status_text = "DNS lookup failed.";
    } else if (web_use_https) {
        switch (https_client.state) {
            case HTTPS_CONNECTING:        status_text = https_client.use_h3 ? "QUIC handshake (HTTP/3)..." : "TLS handshake..."; break;
            case HTTPS_SENDING_REQUEST:   status_text = "Sending request..."; break;
            case HTTPS_AWAITING_RESPONSE: status_text = "Waiting for response..."; break;
            case HTTPS_DONE:              status_text = !web_page_ready ? "Rendering..." : rd_page_incomplete() ? "Page too big - only part shown." : h3_last_used ? "Done. (HTTP/3 over QUIC)" : "Done. (HTTPS)"; break;
            case HTTPS_FAILED:            status_text = hr_size_fatal() ? "Page too large." : https_client.use_h3 ? "HTTP/3 failed." : "TLS/HTTPS failed."; break;
            default:                      status_text = ""; break;
        }
    } else {
        switch (http_client.state) {
            case HTTP_CONNECTING:        status_text = "Connecting..."; break;
            case HTTP_SENDING_REQUEST:   status_text = "Sending request..."; break;
            case HTTP_AWAITING_RESPONSE: status_text = "Waiting for response..."; break;
            case HTTP_DONE:              status_text = rd_page_incomplete() ? "Page too big - only part shown." : "Done."; break;
            case HTTP_FAILED:            status_text = hr_size_fatal() ? "Page too large." : "Failed to connect."; break;
            default:                     status_text = ""; break;
        }
    }
    int sty = web_status_y();
    ui_sunken(wx + 3, sty, ww - 6, WEB_STATUS_H);
    if (status_text) ko_draw_mixed_string(wx + 7, sty + 3, status_text, TH_ACCENT);
    if (status_text && web_page_ready && web_title[0]) {
        int off = ko_string_width(status_text) + 2 * FONT_CELL, room = ww - 14 - off;       /* pixels left for the title */
        if (room > 3 * FONT_CELL) {
            char t[100]; u32 tl = 0;
            while (web_title[tl] && tl + 2 < sizeof(t)) {                                    /* as many characters as fit, measured kerned */
                t[tl] = web_title[tl]; t[tl + 1] = 0;
                if (ko_string_width(t) > room) { t[tl] = 0; break; }
                tl++;
            }
            t[tl] = 0;
            ko_draw_mixed_string(wx + 7 + off, sty + 3, t, TH_TEXT);
        }
    }

    /* response body (or nothing yet, or the failure already explained
     * by the status line above) */
    if (web_page_ready && !web_resolving && !web_dns_failed) draw_web_page();
    else if (!web_resolving) draw_web_failure();   /* a DNS / connection / TLS failure explains itself */
    web_draw_links_pop();
    mb_draw_drop(WIN_ID_WEB, wx, wy, web_menus, WEB_MENU_N);
}

#endif
