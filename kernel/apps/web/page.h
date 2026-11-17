/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_APPS_WEB_PAGE_H
#define MW_APPS_WEB_PAGE_H

/* apps/web/page.h -- the finished response -> page: content-type handling, redirects, link queries, polling.
 * (Split out of kernel.c in pre-29; it is still one translation unit, so modules are included by
 * kernel.c in dependency order -- see the module map there.) */

/* copies an attribute into a C string (empty if absent) */
static void web_attr_copy(u32 node, u32 id, char *out, u32 cap) {
    u32 len; const u8 *v = dom_attr(node, id, &len);
    u32 i = 0;
    if (v) for (; i < len && i + 1 < cap; i++) out[i] = (char)v[i];
    out[i] = 0;
}

#ifdef MW_AUTOTEST_URL
/* ---- little queries over the parsed page (the self-test and link following use them) ---- */
static int web_node_is_link(u32 n) { return RD_NODES[n].kind == RDK_ELEM && RD_NODES[n].tag == TG_A && dom_has_attr(n, AT_HREF); }
static u32 web_link_count(void) {
    u32 c = 0;
    for (u32 n = 1; n < dom_node_count; n++) if (web_node_is_link(n)) c++;
    return c;
}
static const char *web_nth_href(u32 nth) {
    static char buf[WEB_PATH_MAX];
    buf[0] = 0;
    for (u32 n = 1; n < dom_node_count; n++) {
        if (!web_node_is_link(n)) continue;
        if (nth-- == 0) { web_attr_copy(n, AT_HREF, buf, sizeof(buf)); break; }
    }
    return buf;
}
static u32 web_text_bytes(void) {
    rd_relayout(620, 400, 1);                       /* (the real window re-lays out on its next draw) */
    u32 t = 0;
    for (u32 i = 0; i < L.n; i++) if (RD_ITEMS[i].kind == DL_TEXT) t += RD_ITEMS[i].len;
    rd_invalidate();
    return t;
}
#endif /* MW_AUTOTEST_URL */

/* "https://host[:port]/path" for what web_resolve_ref() produced (the port only when it is not the scheme's own; "/" alone is left off). */
static void web_url_string(int https, const char *host, u16 port, const char *path, char *u, u32 cap) {
    u32 n = 0; u[0] = 0;
    append_str(u, &n, cap, https ? "https://" : "http://");
    append_str(u, &n, cap, host);
    if (port && port != (https ? 443 : 80)) {
        char d[8]; int nd = 0; u32 v = port;
        do { d[nd++] = (char)('0' + v % 10); v /= 10; } while (v && nd < 6);
        append_str(u, &n, cap, ":");
        while (nd) { char one[2] = { d[--nd], 0 }; append_str(u, &n, cap, one); }
    }
    if (!(path[0] == '/' && path[1] == 0)) append_str(u, &n, cap, path);
}
/* The full address a link on the shown page points to (relative links resolved); 0 = not a web address or too long. */
static int web_ref_to_url(const char *ref, char *u, u32 cap) {
    int https; u16 port; char host[WEB_URLBAR_MAXLEN + 1], path[WEB_PATH_MAX];
    if (!web_resolve_ref(ref, &https, host, sizeof(host), &port, path, sizeof(path))) return 0;
    web_url_string(https, host, port, path, u, cap);
    return 1;
}

/* Go to `ref` (a redirect's Location or a <meta refresh> address, relative to the page that is shown) the way a browser does: the same
 * budget of redirects, the Address row shows where the page really is now, and the history entry is replaced (Back must not land on an
 * address that only redirects again). 0 = it cannot be followed (too long for MiniWeb, or not a web address), nothing was started. */
static int web_follow_ref(const char *ref) {
    int https; u16 port;
    char host[WEB_URLBAR_MAXLEN + 1], path[WEB_PATH_MAX];
    u32 ll = 0; while (ref[ll]) ll++;
    if (ll + 1 >= HR_LOCATION_MAX || !web_resolve_ref(ref, &https, host, sizeof(host), &port, path, sizeof(path))) return 0;
    web_redirects_left--;
    serial_puts("[WEB] redirect ("); serial_put_dec((u32)hr.status); serial_puts(") -> ");
    serial_puts(host); serial_puts(path); serial_putc('\n');
    {   char u[WEB_URLBAR_MAXLEN + 1]; web_url_string(https, host, port, path, u, sizeof(u)); web_set_urlbar_text(u); }
    web_hist_replace = 1;
    web_start_fetch(https, host, port, path);
    return 1;
}

/* <meta http-equiv="refresh" content="0; url=..."> : pages without scripts (and the <noscript> part of pages with them) use it to send you on.
 * Followed when it fires within 3 seconds -- there is no timer for a longer one -- and has an address (a refresh of the page itself is ignored). */
static int web_follow_meta_refresh(void) {
    static char c[HR_LOCATION_MAX + 40];
    for (u32 n = 1; n < dom_node_count; n++) {
        rd_node_t *e = &RD_NODES[n];
        if (e->kind != RDK_ELEM || e->tag != TG_META) continue;
        u32 el; const u8 *eq = dom_attr(n, AT_HTTP_EQUIV, &el);
        if (!eq || el != 7) continue;
        static const char rf[] = "refresh"; int same = 1;
        for (u32 i = 0; i < 7; i++) { u8 ch = eq[i]; if (ch >= 'A' && ch <= 'Z') ch = (u8)(ch + 32); if (ch != (u8)rf[i]) same = 0; }
        if (!same) continue;
        web_attr_copy(n, AT_CONTENT, c, sizeof(c));
        const char *q = c; u32 delay = 0;
        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
        while (*q >= '0' && *q <= '9') { if (delay < 1000) delay = delay * 10 + (u32)(*q - '0'); q++; }
        while (*q == ' ' || *q == '.' || (*q >= '0' && *q <= '9')) q++;           /* a fraction of a second */
        if (*q == ';' || *q == ',') q++;
        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
        if ((q[0] == 'u' || q[0] == 'U') && (q[1] == 'r' || q[1] == 'R') && (q[2] == 'l' || q[2] == 'L')) {
            q += 3; while (*q == ' ') q++;
            if (*q == '=') q++;
            while (*q == ' ') q++;
        }
        if (*q == '\'' || *q == '"') q++;
        char ref[HR_LOCATION_MAX + 1]; u32 k = 0;
        while (q[k] && q[k] != '\'' && q[k] != '"' && k < HR_LOCATION_MAX) { ref[k] = q[k]; k++; }
        while (k && ref[k - 1] == ' ') k--;
        ref[k] = 0;
        if (k == 0 || delay > 3 || web_redirects_left <= 0) return 0;
        return web_follow_ref(ref);
    }
    return 0;
}

/* Is there anything to look at? Text outside <script>/<style>/<title>, or a picture, a control, a frame, a rule. (<noscript> counts: nothing here runs
 * the page's scripts, so a browser without scripts would show it too.) */
static int web_page_has_content(void) {
    for (u32 n = 1; n < dom_node_count; n++) {
        rd_node_t *e = &RD_NODES[n];
        if (e->kind == RDK_ELEM) {
            switch (e->tag) {
                case TG_IMG: case TG_INPUT: case TG_BUTTON: case TG_SELECT: case TG_TEXTAREA: case TG_IFRAME: case TG_CANVAS:
                case TG_VIDEO: case TG_AUDIO: case TG_OBJECT: case TG_EMBED: case TG_HR: case TG_SVG: case TG_TABLE:
                    return 1;
                default: break;
            }
        } else if (e->kind == RDK_TEXT && e->b) {
            int ink = 0;
            for (u32 i = 0; i < e->b; i++) if (RD_POOL[e->a + i] > ' ') { ink = 1; break; }
            if (!ink) continue;
            int hidden = 0;
            for (u32 p = e->parent; p; p = RD_NODES[p].parent) {
                u32 t = RD_NODES[p].tag;
                if (RD_NODES[p].kind == RDK_ELEM && (t == TG_SCRIPT || t == TG_STYLE || t == TG_TITLE || t == TG_TEMPLATE)) { hidden = 1; break; }
            }
            if (!hidden) return 1;
        }
    }
    return 0;
}
/* A page that is drawn entirely by scripts is blank here (MiniWeb does not run external scripts). Say so instead of showing a white window. */
static void web_load_blank_notice(void) {
    static char h[900]; u32 n = 0; h[0] = 0;
    append_str(h, &n, sizeof(h), "<html><head><title>");
    append_str(h, &n, sizeof(h), SKO("Empty page", "빈 페이지"));
    append_str(h, &n, sizeof(h), "</title></head><body><h2>");
    append_str(h, &n, sizeof(h), SKO("There is nothing to show", "보여줄 내용이 없습니다"));
    append_str(h, &n, sizeof(h), "</h2><p>");
    append_str(h, &n, sizeof(h), SKO("This page is drawn by scripts (JavaScript), and MiniWeb cannot run a page's scripts yet, so the page is empty here.",
                                     "이 페이지는 스크립트(JavaScript)가 화면을 그리는데, MiniWeb은 아직 페이지의 스크립트를 실행하지 못해서 비어 있습니다."));
    append_str(h, &n, sizeof(h), "</p><p><a href=\"" WEB_HOME_URL "\">");
    append_str(h, &n, sizeof(h), SKO("Go to the home page (DuckDuckGo lite works without scripts)", "홈 페이지로 가기 (DuckDuckGo lite는 스크립트 없이 동작합니다)"));
    append_str(h, &n, sizeof(h), "</a></p></body></html>");
    js_dom_active = 0;
    rd_load_html((const u8 *)h, n);
}

/* The response has fully arrived: follow a redirect, or turn the body into the page to show. */
static void web_page_complete(void) {
    web_page_ready = 1;
    int redirect_unfollowable = 0;
    if (hr_is_redirect() && web_redirects_left > 0) {
        if (web_follow_ref(hr.location)) return;
        redirect_unfollowable = 1;       /* the new address is too long for MiniWeb, or is not a web address (mailto:, ...) */
    }
    const u8 *body = HR_BUF + hr.body_start;
    u32 n = hr.body_len;
    js_dom_active = 0;                       /* whatever page was scripted is gone; the HTML branch below re-arms it if the new one is too */
    if (hr.encoded) {
        rd_load_message("Page not shown", "This page is compressed (Content-Encoding) and MiniWeb cannot decompress it.", "");
    } else if (hr_is_redirect() && redirect_unfollowable) {
        rd_load_message("Page moved", "This page has moved to an address MiniWeb cannot follow (it is too long, or it is not a web address).", "");
    } else if (hr_is_redirect()) {
        rd_load_message("Too many redirects.", "The site kept sending MiniWeb from one address to another. Sites that do this usually want cookies, which MiniWeb does not store yet.", "");
    } else if (hr.content_type[0] == 0 ? (n > 0 && body[0] == '<') : web_ct_has(hr.content_type, "html")) {
        rd_load_html(body, n);
        js_dom_load(web_last_host, web_last_path);      /* inline <script>s, on* handlers, DOMContentLoaded, load */
        if (web_follow_meta_refresh()) return;           /* <meta http-equiv="refresh"> */
        if (!web_page_has_content()) web_load_blank_notice();
    } else if (hr.content_type[0] == 0 || web_ct_has(hr.content_type, "text/") ||
               web_ct_has(hr.content_type, "json") || web_ct_has(hr.content_type, "xml")) {
        rd_load_plain(body, n);
    } else {
        rd_load_message("MiniWeb can't display this kind of content.", "", "");
    }
    if (n == 0 && !hr.encoded && !hr_is_redirect()) {        /* (a redirect that could not be followed has an empty body too: its own message must stay) */
        char m[48]; u32 ml = 0;
        append_str(m, &ml, sizeof(m), "(empty response, HTTP ");
        char d[8]; u32 st = (u32)hr.status; int nd = 0;
        do { d[nd++] = (char)('0' + st % 10); st /= 10; } while (st && nd < 6);
        while (nd) { char one[2] = { d[--nd], 0 }; append_str(m, &ml, sizeof(m), one); }
        append_str(m, &ml, sizeof(m), ")");
        rd_load_message(m, "", "");
    }
    dom_title(web_title, sizeof(web_title));
    web_scroll_px = 0;
    rd_invalidate();        /* force a fresh layout at the current window width */
}

static void web_poll(void) {
    js_dom_tick();                                      /* setTimeout/setInterval callbacks */
    if (js_alert_new) { status = js_alert_text; js_alert_new = 0; }     /* alert()/confirm() text goes to the status line */
    if (web_waiting_net) {
        if (!net_cfg.ready) return;            /* still no lease; dhcp_poll() keeps retrying in the background */
        web_waiting_net = 0;
        web_begin_transport();
        return;
    }
    if (web_resolving) {
        if (dns_client.state == DNS_RESOLVED) {
            web_resolving = 0;
            web_connect(dns_client.result_ip);
        } else if (dns_client.state == DNS_FAILED) {
            web_resolving = 0;
            web_dns_failed = 1;
        }
        return; /* don't also poll HTTP/HTTPS this same tick -- there's nothing to poll yet */
    }
    if (web_use_https) {
        if (https_client.state != HTTPS_IDLE &&
            https_client.state != HTTPS_DONE && https_client.state != HTTPS_FAILED) {
            https_poll(web_now_packed());
        }
        if (https_client.state == HTTPS_DONE && !web_page_ready) web_page_complete();
    } else {
        if (http_client.state != HTTP_IDLE &&
            http_client.state != HTTP_DONE && http_client.state != HTTP_FAILED) {
            http_poll();
        }
        if (http_client.state == HTTP_DONE && !web_page_ready) web_page_complete();
    }
}

#endif
