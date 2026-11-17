/* SPDX-License-Identifier: GPL-3.0-only */
/* host_js_test.c -- unit tests for kernel/js.h (the JavaScript engine core).  tools/test/run_host_js.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef int i32;
#define IO_H
#define IMG_ALLOC(n) malloc(n)     /* kernel/img.h (pulled in through the DOM glue) allocates with these in the host tests */
#define IMG_FREE(p)  free(p)
#define JS_ALLOC(n) malloc(n)
#define JS_FREE(p)  free(p)
static char logbuf[4096];
static void host_log(const char *s) { size_t l = strlen(logbuf); size_t k = strlen(s); if (l + k < sizeof(logbuf) - 1) strcpy(logbuf + l, s); }
#define JS_LOG(s) host_log(s)
#include "../../kernel/js.h"

static int checks, failures;
static u32 fake_ms;
static u32 fake_clock(void) { return fake_ms; }

/* runs `src`, then returns String(out) for the global `out` */
static const char *ev(const char *src) {
    static char res[512];
    logbuf[0] = 0;
    js_begin(); js_clock = fake_clock;
    int ok = js_run_source(src, (u32)strlen(src));
    if (!ok) { snprintf(res, sizeof res, "ERR[%s]", js_msg); return res; }
    js_top_enter();
    int found; jv v = js_prop_get(js_genv, js_str_c("out"), &found);
    if (!found) return "NOOUT";
    u32 s = js_tostr(v);
    snprintf(res, sizeof res, "%.*s", (int)js_sl(s), js_sp(s));
    return res;
}
#define T(src, want) do { checks++; const char *got = ev(src); if (strcmp(got, want)) { failures++; printf("FAIL: %s\n   want [%s]\n   got  [%s]\n", src, want, got); } } while (0)

int main(void) {
    /* arithmetic & operators */
    T("var out = 1 + 2 * 3;", "7");
    T("var out = '10' - 3;", "7");
    T("var out = 'a' + 1 + 2;", "a12");
    T("var out = 7 / 2;", "3");
    T("var out = 1 / 0;", "Infinity");
    T("var out = 0 / 0;", "NaN");
    T("var out = 2 ** 10;", "1024");
    T("var out = 17 % 5;", "2");
    T("var out = (5 & 3) + (5 | 3) + (5 ^ 3) + (1 << 4) + (-16 >> 2);", "26");
    T("var out = [1 == '1', 1 === '1', null == undefined, null === undefined, NaN == NaN].join();", "true,false,true,false,false");
    T("var out = [typeof 1, typeof 'a', typeof undefined, typeof null, typeof {}, typeof function(){}, typeof nope].join();", "number,string,undefined,object,object,function,undefined");
    T("var a = 5; a += 3; a *= 2; a -= 1; var out = a;", "15");
    T("var i = 5; var out = [i++, i, ++i, i--, i].join();", "5,6,7,7,6");
    T("var out = (1, 2, 3);", "3");
    T("var out = null ?? 'd'; ", "d");
    T("var o = {a:{b:5}}; var out = [o?.a?.b, o?.x?.y, o.x?.y.z].join();", "5,,");
    T("var out = 5 > 3 ? 'y' : 'n';", "y");
    T("var out = 2147483647 + 1;", "Infinity");
    /* strings */
    T("var out = 'Hello'.length + ' ' + 'Hello'.toUpperCase() + ' ' + 'Hello'.slice(1, 3) + 'Hello'.charAt(1) + 'Hello'.indexOf('l');", "5 HELLO ele2");
    T("var out = '한국어 OK'.length + '한국어'.slice(1) + '한국어'.charAt(2) + '한국어'.indexOf('어');", "6국어어2");
    T("var out = 'a,b,c'.split(',').map(function(x){return x+x}).join('-');", "aa-bb-cc");
    T("var out = '  hi  '.trim() + '|' + 'abc'.padStart(5,'*') + '|' + 'ab'.repeat(3) + '|' + 'x-y-z'.replaceAll('-', '+');", "hi|**abc|ababab|x+y+z");
    T("var n = 'World'; var out = `Hello ${n}! ${1+2} ${'a'+`b${n}`}`;", "Hello World! 3 abWorld");
    T("var out = 'abc'.includes('bc') + ',' + 'abc'.startsWith('ab') + ',' + 'abc'.endsWith('bc') + ',' + 'a\\nb'.length + ',' + '\\u0041\\x42';", "true,true,true,3,AB");
    T("var out = (255).toString(16) + ',' + (5).toFixed(2) + ',' + String(12) + ',' + Number('42') + ',' + parseInt('12px') + ',' + parseInt('ff', 16) + ',' + parseInt('x');", "ff,5.00,12,42,12,255,NaN");
    /* control flow */
    T("var s = 0; for (var i = 0; i < 10; i++) { if (i == 3) continue; if (i == 8) break; s += i; } var out = s;", "25");
    T("var s = 0, i = 0; while (i < 5) { s += i; i++; } do { s += 100; } while (false); var out = s;", "110");
    T("var o = {a:1,b:2,c:3}; var k = []; for (var x in o) k.push(x + o[x]); var out = k.join();", "a1,b2,c3");
    T("var t = 0; for (const v of [1,2,3]) t += v; for (const c of 'ab') t += c; var out = t;", "6ab");
    T("function f(x) { switch (x) { case 1: return 'one'; case 2: case 3: return 'twothree'; default: return 'many'; } } var out = [f(1), f(3), f(9)].join();", "one,twothree,many");
    T("var out = ''; switch (2) { case 1: out += 'a'; case 2: out += 'b'; case 3: out += 'c'; break; case 4: out += 'd'; }", "bc");
    T("var out; try { throw new Error('boom'); } catch (e) { out = e.message + '|' + e.name; } finally { out += '!'; }", "boom|Error!");
    T("var out; try { null.x; } catch (e) { out = e.name; }", "TypeError");
    T("var out; try { nope(); } catch (e) { out = String(e); }", "ReferenceError: nope is not defined");
    T("function f() { try { return 'try'; } finally { out = 'fin'; } } var out; f();", "fin");
    T("var out = 0; label: ;", "ERR[SyntaxError: expected ; (line 1)]");
    /* functions & closures */
    T("function fact(n) { return n <= 1 ? 1 : n * fact(n - 1); } var out = fact(10);", "3628800");
    T("var add = (a, b) => a + b; var sq = x => x * x; var out = add(2, 3) + sq(4);", "21");
    T("function mk() { var c = 0; return function() { return ++c; }; } var f = mk(), g = mk(); f(); f(); var out = f() + ',' + g();", "3,1");
    T("var fs = []; for (let i = 0; i < 3; i++) fs.push(() => i); var out = fs.map(f => f()).join();", "0,1,2");
    T("var fs = []; for (var i = 0; i < 3; i++) fs.push(() => i); var out = fs.map(f => f()).join();", "3,3,3");
    T("var out = hoisted(); function hoisted() { return 'ok'; }", "ok");
    T("var out = typeof later; var later = 1;", "undefined");
    T("function f(a, b = 10) { return a + b; } var out = f(1) + ',' + f(1, 2);", "11,3");
    T("var o = { n: 5, get() { return this.n; }, add: function(x) { return this.n + x; } }; var out = o.get() + ',' + o.add(1);", "5,6");
    T("function P(x) { this.x = x; } var p = new P(7); var out = p.x;", "7");
    T("const k = 1; var out; try { k = 2; } catch (e) { out = e.name; }", "TypeError");
    /* arrays */
    T("var a = [3,1,2]; a.push(5); a.unshift(0); var out = a.join() + '|' + a.length + '|' + a.pop() + a.shift();", "0,3,1,2,5|5|50");
    T("var out = [5,1,10,2].sort().join() + '|' + [5,1,10,2].sort((a,b)=>a-b).join();", "1,10,2,5|1,2,5,10");
    T("var out = [1,2,3,4,5].filter(x => x % 2).map(x => x * 10).reduce((s, x) => s + x, 0);", "90");
    T("var a = [1,2,3,4,5]; var r = a.splice(1, 2, 'x', 'y', 'z'); var out = a.join() + '|' + r.join();", "1,x,y,z,4,5|2,3");
    T("var out = [1,2,3].slice(-2).join() + '|' + [1,2].concat([3], 4).join() + '|' + [1,2,3].indexOf(2) + [1,2,3].includes(9) + '|' + [3,4].reverse().join();", "2,3|1,2,3,4|1false|4,3");
    T("var a = []; a[3] = 'x'; var out = a.length + ',' + a.join('-') + ',' + Array.isArray(a) + ',' + [...'ab'].length + ',' + [...[1,2], 3].join();", "4,---x,true,2,1,2,3");
    T("var out = [1,2,3].find(x => x > 1) + ',' + [1,2,3].findIndex(x => x > 5) + ',' + [1,2].some(x => x > 1) + [1,2].every(x => x > 1);", "2,-1,truefalse");
    T("var out = 0; [1,2,3].forEach(function(v, i) { out += v * i; });", "8");
    /* objects & JSON */
    T("var o = {a:1, 'b c':2, [1+1]:3}; o.d = 4; delete o.a; var out = Object.keys(o).join() + '|' + ('d' in o) + ('a' in o);", "b c,2,d|truefalse");
    T("var o = {a:1, b:[1,2,{c:'x\"y'}], d:null, e:undefined, f:true}; var out = JSON.stringify(o);", "{\"a\":1,\"b\":[1,2,{\"c\":\"x\\\"y\"}],\"d\":null,\"f\":true}");
    T("var o = JSON.parse('{\"a\":[1,2,{\"b\":\"hi\"}],\"c\":true}'); var out = o.a[2].b + o.a.length + o.c;", "hi3true");
    T("var a = {x:1}, b = {...a, y:2}; var out = JSON.stringify(Object.assign({}, b, {z:3}));", "{\"x\":1,\"y\":2,\"z\":3}");
    T("var out = Object.entries({a:1,b:2}).map(e => e[0] + e[1]).join();", "a1,b2");
    T("var out = Math.max(1, 5, 3) + ',' + Math.min(4, 2) + ',' + Math.abs(-3) + ',' + Math.pow(2, 8) + ',' + Math.sqrt(144);", "5,2,3,256,12");
    /* timers */
    { checks++; js_begin(); js_clock = fake_clock; fake_ms = 1000;
      const char *src = "var log = ''; setTimeout(function(){ log += 'a'; }, 100); var iv = setInterval(function(){ log += 'i'; if (log.length > 4) clearInterval(iv); }, 50); var cancelled = setTimeout(function(){ log += 'X'; }, 10); clearTimeout(cancelled);";
      js_run_source(src, (u32)strlen(src));
      for (int k = 0; k < 20; k++) { fake_ms += 25; js_timers_run(); }
      js_top_enter(); u32 s = js_tostr(js_prop_get(js_genv, js_str_c("log"), 0));
      if (strncmp(js_sp(s), "iaii", 4) && strncmp(js_sp(s), "iiaii", 5)) { failures++; printf("FAIL timers: [%.*s]\n", (int)js_sl(s), js_sp(s)); } }
    /* GC while temporaries are live in C locals (the conservative stack scan has to earn its keep) */
    T("function churn() { var j; for (var i = 0; i < 20000; i++) j = ['g' + i, {k: i}]; return 'mid'; } var out = 'left-' + churn() + '-right';", "left-mid-right");
    T("function churn() { var j; for (var i = 0; i < 20000; i++) j = ['g' + i, {k: i}]; return 'mid'; } var arr = ['a' + 1, churn(), {n: 'obj' + 2}, 'c' + 3]; var out = JSON.stringify(arr);", "[\"a1\",\"mid\",{\"n\":\"obj2\"},\"c3\"]");
    T("var data = []; for (var i = 0; i < 40; i++) data.push({v: (i * 7) % 13, s: 'n' + i}); data.sort(function(a, b) { var t; for (var q = 0; q < 300; q++) t = 'x' + q + a.s; return a.v - b.v; }); var out = data[0].v + ',' + data[39].v + ',' + data.length;", "0,12,40");
    T("var o = {}; for (var i = 0; i < 200; i++) o['k' + i] = 'v' + i; for (var r = 0; r < 20000; r++) { var tmp = {a: [r]}; } var out = o.k0 + o.k199 + Object.keys(o).length;", "v0v199200");
    T("var s = 0; [1,2,3].forEach(function(x) { var t; for (var i = 0; i < 20000; i++) t = [i, 'q' + i]; s += x; }); var out = s;", "6");
    /* guards */
    T("var out = 0; while (true) { out++; }", "ERR[script took too long, killed it]");
    T("function f() { return f(); } var out; try { f(); } catch (e) { out = e.name; }", "RangeError");
    T("var s = 'x'; for (var i = 0; i < 40; i++) s += s;", "ERR[out of memory (strings)]");
    T("var out = ((", "ERR[SyntaxError: unexpected end of script (line 1)]");
    T("var out = class A {};", "ERR[SyntaxError: classes are not supported (line 1)]");
    /* GC: lots of garbage must not run the heap dry */
    T("var s = 0; for (var i = 0; i < 20000; i++) { var o = {a: i, b: [i, 'str' + i]}; var t = 'n' + i + 'm'; s += o.b[0] % 7 + t.length; } var out = s > 0;", "true");
    T("var keep = []; for (var i = 0; i < 300; i++) { keep.push({id: i, name: 'item' + i}); } var junk; for (var j = 0; j < 30000; j++) { junk = ['a' + j, {k: j}]; } var out = keep[299].name + keep.length + keep[0].id;", "item299300" "0");
    T("var str = ''; for (var i = 0; i < 3000; i++) { str += 'ab'; if (str.length > 100) str = str.slice(50); } var out = str.length;", "100");
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
