/* SPDX-License-Identifier: GPL-3.0-only */
/* host_js_dom_test.c -- js.h + js_dom.h against the REAL DOM/CSS/layout engine: scripts edit the page, events fire,
 * timers tick, and the edits survive a relayout.  tools/test/run_host_js.sh */
#include "rd_host.h"

#define RD_PUTPIXEL(x, y, c) do { } while (0)
#define RD_FILLRECT(x, y, w, h, c) do { } while (0)
#define RD_COLOR(rgb) ((unsigned int)(rgb))
#include "../../kernel/render.h"
#define NETCLOCK_H
static u32 fake_secs = 100;
static u32 net_wall_seconds(void) { return fake_secs; }
#define JS_ALLOC(n) malloc(n)
#define JS_FREE(p)  free(p)
static char logbuf[4096];
static void host_log(const char *s) { size_t l = strlen(logbuf), k = strlen(s); if (l + k < sizeof(logbuf) - 1) strcpy(logbuf + l, s); }
#define JS_LOG(s) host_log(s)
#include "../../kernel/js_dom.h"

static int checks, failures;
static void load(const char *html) { logbuf[0] = 0; rd_load_html((const u8 *)html, (u32)strlen(html)); rd_relayout(620, 400, 1); js_dom_load("example.com", "/t"); rd_relayout(620, 400, 1); }
/* evaluates `expr` in the page's global scope and returns String(result) */
static const char *js(const char *expr) {
    static char res[400], src[600];
    snprintf(src, sizeof src, "var __r = (%s);", expr);
    js_dom_enter(); if (!js_run_source(src, (u32)strlen(src))) { snprintf(res, sizeof res, "ERR[%s]", js_msg); return res; }
    js_top_enter(); u32 s = js_tostr(js_prop_get(js_genv, js_str_c("__r"), 0));
    js_dom_commit();                                           /* what js_dom_event()/js_dom_tick() do after every run */
    snprintf(res, sizeof res, "%.*s", (int)js_sl(s), js_sp(s)); return res;
}
#define EXPECT(expr, want) do { checks++; const char *g = js(expr); if (strcmp(g, want)) { failures++; printf("FAIL: %s\n   want [%s]\n   got  [%s]\n   log: %s\n", expr, want, g, logbuf); } } while (0)
static u32 find_id(const char *id) { for (u32 i = 1; i < dom_node_count; i++) { u32 l; const u8 *v = dom_attr(i, AT_ID, &l); if (v && l == strlen(id) && !memcmp(v, id, l)) return i; } return 0; }

int main(void) {
    /* 1. scripts run, the DOM is reachable, edits persist across relayout */
    load("<html><body><h1 id=t>Hello</h1><p class='a b' id=p>one <b>two</b></p><ul id=l><li>x<li>y</ul>"
         "<script>var h = document.getElementById('t'); h.textContent = 'Changed ' + h.tagName; document.title2 = 1;</script></body></html>");
    EXPECT("document.getElementById('t').textContent", "Changed H1");
    EXPECT("document.getElementById('p').textContent", "one two");
    EXPECT("document.querySelectorAll('li').length + ',' + document.querySelector('#l li').textContent", "2,x");
    EXPECT("document.querySelectorAll('ul > li, p b').length", "3");
    EXPECT("document.querySelector('p.a.b').id + document.querySelector('[id=t]').tagName + document.querySelectorAll('.nope').length", "pH10");
    EXPECT("document.getElementById('nope')", "null");
    EXPECT("document.getElementsByTagName('li').length", "2");

    /* 2. class, style, attributes */
    EXPECT("(function(){var p=document.getElementById('p'); p.classList.add('c'); p.classList.remove('a'); var r=p.className; r+='|'+p.classList.contains('b')+p.classList.toggle('b')+p.className; return r;})()", "b c|truefalsec");
    EXPECT("(function(){var p=document.getElementById('p'); p.style.backgroundColor='red'; p.style.color='blue'; p.style.color='green'; return p.getAttribute('style')+'|'+p.style.color+'|'+p.style.backgroundColor;})()", "background-color:red;color:green;|green|red");
    EXPECT("(function(){var p=document.getElementById('p'); p.setAttribute('title','T'); return p.getAttribute('title')+p.hasAttribute('title')+p.getAttribute('zzz');})()", "Ttruenull");
    EXPECT("(function(){var a=document.createElement('a'); a.href='/x'; a.textContent='go'; return a.href+a.tagName+a.textContent;})()", "/xAgo");

    /* 3. building and restructuring the tree */
    EXPECT("(function(){var l=document.getElementById('l'); var li=document.createElement('li'); li.textContent='new'; l.appendChild(li); l.insertBefore(document.createElement('li'), l.children[0]); return l.children.length+','+l.lastChild.textContent+','+l.firstChild.textContent+'|'+li.parentNode.id;})()", "4,new,|l");
    EXPECT("(function(){var l=document.getElementById('l'); l.removeChild(l.children[0]); l.children[2].remove(); return l.children.length+l.textContent;})()", "2xy");
    EXPECT("(function(){var d=document.createElement('div'); d.innerHTML='<p id=ip>hi <i>there</i></p><span>s</span>'; document.body.appendChild(d); return document.getElementById('ip').textContent+'|'+d.children.length;})()", "hi there|2");
    EXPECT("(function(){var h=document.getElementById('t'); h.innerHTML='<b>bold</b> text'; return h.children.length+h.textContent;})()", "1bold text");

    /* 4. edits survive a relayout (this is the pool_mark dance) */
    { checks++; rd_invalidate(); rd_relayout(620, 400, 1); rd_invalidate(); rd_relayout(500, 300, 1);
      const char *g = js("document.getElementById('t').textContent + '|' + document.getElementById('ip').textContent");
      if (strcmp(g, "bold text|hi there")) { failures++; printf("FAIL relayout persistence: [%s]\n", g); } }

    /* 5. events: addEventListener, onclick attribute, bubbling, preventDefault, handler errors don't kill the page */
    load("<html><body><div id=outer onclick=\"log += 'O';\"><button id=b>go</button><a id=lnk href='/x'>l</a></div><input id=i type=text><input id=c type=checkbox>"
         "<form id=f onsubmit=\"return false\"><input type=submit></form>"
         "<script>var log=''; var b=document.getElementById('b');"
         "b.addEventListener('click', function(e){ log += 'B' + e.type + (e.target === b) + (this === b); });"
         "document.getElementById('lnk').addEventListener('click', function(e){ e.preventDefault(); log += 'L'; });"
         "document.getElementById('i').addEventListener('input', function(){ log += 'I'; });"
         "document.getElementById('c').onchange = function(){ log += 'C'; };"
         "window.addEventListener('load', function(){ log += 'W'; });"
         "document.addEventListener('DOMContentLoaded', function(){ log += 'D'; });"
         "document.getElementById('f').addEventListener('submit', function(){ log += 'S'; });"
         "</script></body></html>");
    EXPECT("log", "DW");
    js_dom_click(find_id("b"));
    EXPECT("log", "DWBclicktruetrueO");
    { checks++; int pv = js_dom_click(find_id("lnk")); if (!pv) { failures++; printf("FAIL: preventDefault not reported\n"); } }
    EXPECT("log.slice(-2)", "LO");
    js_dom_input(find_id("i")); js_dom_change(find_id("c"));
    { checks++; int pv = js_dom_submit(find_id("f")); if (!pv) { failures++; printf("FAIL: onsubmit=return false not reported as prevented\n"); } }
    EXPECT("log.slice(-3)", "ICS");
    EXPECT("document.getElementById('b').click() + log.slice(-1)", "undefinedO");

    /* 6. timers via the DOM tick, alert(), error isolation */
    load("<html><body><p id=p>0</p><script>var n=0; var iv=setInterval(function(){ n++; document.getElementById('p').textContent = 'tick' + n; if (n==3) clearInterval(iv); }, 1000);"
         "setTimeout(function(){ alert('hello ' + n); }, 2500); setTimeout(function(){ nope(); }, 100);</script></body></html>");
    for (int s = 0; s < 40; s++) { fake_secs++; for (int pass = 0; pass < 5; pass++) js_dom_tick(); if (s % 2) fake_secs += 0; }
    EXPECT("document.getElementById('p').textContent", "tick3");
    { checks++; if (strcmp(js_alert_text, "hello 2") && strcmp(js_alert_text, "hello 3")) { failures++; printf("FAIL alert text [%s]\n", js_alert_text); } }
    { checks++; if (!strstr(logbuf, "nope is not defined")) { failures++; printf("FAIL: handler error not logged: %s\n", logbuf); } }

    /* 7. pages without scripts never wake the engine; value/checked; document.write; bad selectors */
    load("<html><body><p>plain</p></body></html>");
    { checks++; if (js_dom_active) { failures++; printf("FAIL: engine woke up for a script-less page\n"); } }
    load("<html><body><input id=i type=text value=abc><input id=c type=checkbox checked><select id=s><option value=1>a<option value=2 selected>b</select><script>document.write('<em id=w>W</em>');</script></body></html>");
    EXPECT("document.getElementById('w').tagName", "EM");
    EXPECT("(function(){var i=document.getElementById('i'); var r=i.value; i.value='xyz'; return r+i.value;})()", "abcxyz");
    EXPECT("(function(){var c=document.getElementById('c'); var r=c.checked; c.checked=false; return r+''+c.checked;})()", "truefalse");
    EXPECT("document.getElementById('s').value", "2");
    EXPECT("document.querySelectorAll('p:first-child').length", "0");
    EXPECT("localStorage.setItem('k','v') + localStorage.getItem('k') + localStorage.getItem('z')", "undefinedvnull");
    EXPECT("location.hostname + location.pathname + navigator.language", "example.com/tko");
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
