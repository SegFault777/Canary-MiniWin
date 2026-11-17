/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MW_APPS_WEB_FETCH_H
#define MW_APPS_WEB_FETCH_H

/* apps/web/fetch.h -- starting a fetch: DNS, connect, HTTP/HTTPS transport selection, redirects.
 * (Split out of kernel.c in pre-29; it is still one translation unit, so modules are included by
 * kernel.c in dependency order -- see the module map there.) */

static void web_connect(u32 ip) {
    if (web_use_https) {
        u8 seed[16];
        web_make_entropy_seed(seed);
        https_get(ip, web_last_port, web_last_host, web_last_path, seed);
    } else {
        http_get(ip, web_last_port, web_last_host, web_last_path);
    }
}

static void web_begin_transport(void) {
    u32 ip;
    if (web_parse_ipv4(web_last_host, &ip)) {
        web_connect(ip);                       /* an IP literal has nothing to resolve */
    } else {
        web_resolving = 1;
        dns_resolve(web_last_host);
    }
}

/* ---- the history behind the Back and Forward buttons: the last WEB_HIST_N loads (a new load forgets the pages ahead of the current one) ---- */
#define WEB_HIST_N 8
typedef struct { u8 https; u16 port; char host[WEB_URLBAR_MAXLEN + 1]; char path[WEB_PATH_MAX]; } web_hist_t;
static web_hist_t web_hist[WEB_HIST_N];
static int web_hist_replace = 0;                                        /* the next load is a redirect: it REPLACES the current entry (Back must not land on an address that only redirects again) */
static int web_hist_n = 0, web_hist_pos = -1, web_hist_skip = 0;        /* skip: the next load is Back/Forward itself, not a new page */
static int web_stopped = 0;                                             /* the Stop button was pressed for the load in progress */
static int web_streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void web_hist_push(int https, const char *host, u16 port, const char *path) {
    if (web_hist_skip) { web_hist_skip = 0; return; }
    if (web_hist_replace) {
        web_hist_replace = 0;
        if (web_hist_pos >= 0) {
            web_hist_t *r = &web_hist[web_hist_pos];
            r->https = (u8)https; r->port = port;
            kstrcpy(r->host, host, sizeof(r->host)); kstrcpy(r->path, path, sizeof(r->path));
            return;
        }
    }
    if (web_hist_pos >= 0 && web_hist[web_hist_pos].https == (u8)https && web_hist[web_hist_pos].port == port &&
        web_streq(web_hist[web_hist_pos].host, host) && web_streq(web_hist[web_hist_pos].path, path)) return;   /* the same page again (Refresh) */
    if (web_hist_pos + 1 < web_hist_n) web_hist_n = web_hist_pos + 1;
    if (web_hist_n == WEB_HIST_N) {
        for (int i = 1; i < WEB_HIST_N; i++) {
            web_hist[i - 1].https = web_hist[i].https; web_hist[i - 1].port = web_hist[i].port;
            kstrcpy(web_hist[i - 1].host, web_hist[i].host, sizeof(web_hist[i - 1].host));
            kstrcpy(web_hist[i - 1].path, web_hist[i].path, sizeof(web_hist[i - 1].path));
        }
        web_hist_n--;
    }
    web_hist_t *e = &web_hist[web_hist_n];
    e->https = (u8)https; e->port = port;
    kstrcpy(e->host, host, sizeof(e->host)); kstrcpy(e->path, path, sizeof(e->path));
    web_hist_pos = web_hist_n++;
}

/* The ONE place a page load starts -- from the URL bar, a bookmark, a clicked link, or a redirect. */
static void web_start_fetch(int https, const char *host, u16 port, const char *path) {
    web_hist_push(https, host, port ? port : (u16)(https ? 443 : 80), path);
    web_stopped = 0;
    web_dns_failed = 0;
    web_resolving = 0;
    web_waiting_net = 0;
    web_page_ready = 0;
    js_dom_active = 0;                                  /* the old page's timers must not poke the placeholder that replaces it */
    http_client.state = HTTP_IDLE;
    https_client.state = HTTPS_IDLE;
    web_use_https = https;
    web_last_port = port ? port : (u16)(https ? 443 : 80);
    kstrcpy(web_last_host, host, sizeof(web_last_host));
    kstrcpy(web_last_path, path, sizeof(web_last_path));
    rd_load_message("", "", "");
    web_title[0] = 0;
    web_scroll_px = 0;
    hr_post_active = 0;             /* a POST is a one-shot: whatever starts next is a plain GET unless the form code re-arms it */

    if (!net_cfg.ready) {
        /* No IP address yet (DHCP hasn't been answered). Don't fire a DNS query from 0.0.0.0 at a
         * server that doesn't exist -- wait for the lease; web_poll() starts the load when it arrives. */
        web_waiting_net = 1;
        return;
    }
    web_begin_transport();
}

/* Follows a link (or anything else carrying an href) from the current page. */
static void web_navigate_ref(const char *ref) {
    int https; u16 port;
    char host[WEB_URLBAR_MAXLEN + 1], path[WEB_PATH_MAX];
    if (!web_resolve_ref(ref, &https, host, sizeof(host), &port, path, sizeof(path))) return;
    {   /* show where we are going in the address bar (what a browser does when you click a link) */
        u32 n = 0; web_urlbar_buf[0] = 0;
        append_str(web_urlbar_buf, &n, sizeof(web_urlbar_buf), https ? "https://" : "http://");
        append_str(web_urlbar_buf, &n, sizeof(web_urlbar_buf), host);
        if (port && port != (https ? 443 : 80)) {
            char d[8]; int nd = 0; u32 v = port;
            do { d[nd++] = (char)('0' + v % 10); v /= 10; } while (v && nd < 6);
            if (n + 1 < sizeof(web_urlbar_buf)) web_urlbar_buf[n++] = ':';
            while (nd && n + 1 < sizeof(web_urlbar_buf)) web_urlbar_buf[n++] = d[--nd];
            web_urlbar_buf[n] = 0;
        }
        append_str(web_urlbar_buf, &n, sizeof(web_urlbar_buf), path);
        web_urlbar_len = n;
        tf_to_end(&web_urlbar_tf, web_urlbar_buf);
    }
    web_current_site = WEB_SOURCE_URLBAR;
    web_redirects_left = 5;
    web_start_fetch(https, host, port, path);
}

static void web_go_url(void) {
    if (web_urlbar_len == 0) return; /* nothing typed, nothing to do */

    web_current_site = WEB_SOURCE_URLBAR;
    web_redirects_left = 5;

    if (web_looks_like_url(web_urlbar_buf)) {
        /* No scheme means HTTPS (nearly every real site is HTTPS-only now); an explicit one is honored. */
        const char *rest = web_urlbar_buf;
        int want_https = 1;
        if (rest[0]=='h'&&rest[1]=='t'&&rest[2]=='t'&&rest[3]=='p'&&rest[4]=='s'&&rest[5]==':'&&rest[6]=='/'&&rest[7]=='/') rest += 8;
        else if (rest[0]=='h'&&rest[1]=='t'&&rest[2]=='t'&&rest[3]=='p'&&rest[4]==':'&&rest[5]=='/'&&rest[6]=='/') { rest += 7; want_https = 0; }
        else if (rest[0]=='h'&&rest[1]=='3'&&rest[2]==':'&&rest[3]=='/'&&rest[4]=='/') { rest += 5; h3_force_next = 1; }   /* h3://host/path: HTTP/3 only, no fallback */

        char host[WEB_URLBAR_MAXLEN + 1], path[WEB_PATH_MAX];
        u16 port = 0;
        /* split at the first '/', '?' or '#' (so "example.com?x=1" works too) */
        u32 hi = 0;
        while (rest[hi] && rest[hi] != '/' && rest[hi] != '?' && rest[hi] != '#' && hi < sizeof(host) - 1) { host[hi] = rest[hi]; hi++; }
        host[hi] = 0;
        web_split_port(host, &port);
        const char *tail = rest + hi;
        if (tail[0] == 0 || tail[0] == '#') kstrcpy(path, "/", sizeof(path));
        else if (tail[0] == '?') { kstrcpy(path, "/", sizeof(path)); u32 pl = 1; append_str(path, &pl, sizeof(path), tail); }
        else kstrcpy(path, tail, sizeof(path));
        web_pct_high(path, sizeof(path));                      /* Hangul in the address goes out percent-encoded */
        web_start_fetch(want_https, host, port, path);
    } else {
        char encoded[WEB_PATH_MAX];
        char path[WEB_PATH_MAX];
        kstrcpy(path, "/lite/?q=", sizeof(path));
        u32 path_len = 9; /* strlen("/lite/?q=") -- append_str() needs the current length */
        /* the encoded text has to fit what is LEFT of the request path, not just its own buffer */
        if (!web_urlencode(web_urlbar_buf, encoded, sizeof(path) - path_len)) {
            status = t(STR_SEARCH_TOO_LONG);       /* never search for a cut-off phrase */
            return;
        }
        append_str(path, &path_len, sizeof(path), encoded);
        web_start_fetch(1, "lite.duckduckgo.com", 443, path);
    }
}

static int web_ct_has(const char *ct, const char *word) {
    u32 wl = 0; while (word[wl]) wl++;
    for (u32 i = 0; ct[i]; i++) {
        u32 k = 0;
        while (k < wl && ct[i + k] && ((ct[i + k] >= 'A' && ct[i + k] <= 'Z') ? ct[i + k] + 32 : ct[i + k]) == word[k]) k++;
        if (k == wl) return 1;
    }
    return 0;
}


/* ---- what the toolbar buttons do ---- */
static int web_busy(void) {              /* a load is in progress (the Stop button is only live then) */
    if (web_waiting_net || web_resolving) return 1;
    if (web_page_ready || web_dns_failed || web_current_site == -1) return 0;
    if (web_use_https) return https_client.state != HTTPS_IDLE && https_client.state != HTTPS_FAILED;
    return http_client.state != HTTP_IDLE && http_client.state != HTTP_FAILED;
}
static void web_stop(void) {
    if (!web_busy()) return;
    web_waiting_net = 0; web_resolving = 0;
    http_client.state = HTTP_IDLE; https_client.state = HTTPS_IDLE;
    web_stopped = 1;
}
static void web_set_urlbar_text(const char *s) {
    u32 n = 0; web_urlbar_buf[0] = 0;
    append_str(web_urlbar_buf, &n, sizeof(web_urlbar_buf), s);
    web_urlbar_sync(); tf_to_end(&web_urlbar_tf, web_urlbar_buf);
}
static int web_can_back(void) { return web_hist_pos > 0; }
static int web_can_forward(void) { return web_hist_pos + 1 < web_hist_n; }
static void web_hist_go(int d) {
    int np = web_hist_pos + d;
    if (np < 0 || np >= web_hist_n) return;
    web_hist_pos = np;
    web_hist_t *e = &web_hist[np];
    char url[WEB_URLBAR_MAXLEN + 1]; u32 n = 0; url[0] = 0;
    append_str(url, &n, sizeof(url), e->https ? "https://" : "http://");
    append_str(url, &n, sizeof(url), e->host);
    if (e->path[0] && !(e->path[0] == '/' && e->path[1] == 0)) append_str(url, &n, sizeof(url), e->path);
    web_set_urlbar_text(url);
    web_current_site = WEB_SOURCE_URLBAR; web_redirects_left = 5;
    web_hist_skip = 1;
    web_start_fetch(e->https, e->host, e->port, e->path);
}
#define WEB_HOME_URL "https://lite.duckduckgo.com/lite/"
static void web_home(void) { web_set_urlbar_text(WEB_HOME_URL); web_go_url(); }

/* A site of the Links list: loads it and shows its address in the Address row. */
static void web_go_link(int site) {
    web_go(site);
    char u[WEB_URLBAR_MAXLEN + 1]; u32 n = 0; u[0] = 0;
    append_str(u, &n, sizeof(u), site == WEB_SITE_PYPI ? "https://" : "http://");
    append_str(u, &n, sizeof(u), web_last_host);
    web_set_urlbar_text(u);
}

/* MiniWeb was just opened: with nothing loaded yet it starts on the home page (DuckDuckGo lite) instead of an empty window. */
static void web_opened(void) {
    if (web_current_site == -1 && web_last_host[0] == 0 && web_urlbar_buf[0] == 0) web_home();
}

#endif
