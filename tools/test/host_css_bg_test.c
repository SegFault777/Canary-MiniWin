/* SPDX-License-Identifier: GPL-3.0-only */
/* host_css_bg_test.c -- CSS background colours, checked on real pixels: the page is laid out and painted by kernel/render.h into a small
 * framebuffer and the colour of a chosen pixel is compared. tools/test/run_host_css_bg.sh */
#include "rd_host.h"
#define FB_W 220
#define FB_H 140
static unsigned int fb[FB_W * FB_H];
#define RD_PUTPIXEL(x, y, c) do { int _x = (x), _y = (y); if (_x >= 0 && _x < FB_W && _y >= 0 && _y < FB_H) fb[_y * FB_W + _x] = (c); } while (0)
#define RD_FILLRECT(x, y, w, h, c) do { int _x0 = (x), _y0 = (y), _x1 = _x0 + (w), _y1 = _y0 + (h); for (int _y = _y0 < 0 ? 0 : _y0; _y < _y1 && _y < FB_H; _y++) for (int _x = _x0 < 0 ? 0 : _x0; _x < _x1 && _x < FB_W; _x++) fb[_y * FB_W + _x] = (c); } while (0)
#define RD_COLOR(rgb) (rgb)
#include "../../kernel/render.h"

static int checks, fails;
/* Paints `html` into a FB_W x FB_H viewport and returns the pixel at (x, y). */
static unsigned int px(const char *html, int x, int y) {
    for (int i = 0; i < FB_W * FB_H; i++) fb[i] = 0x123456;           /* a colour nothing should leave behind */
    rd_load_html((const u8 *)html, (u32)strlen(html));
    rd_relayout(FB_W, FB_H, 1);
    rd_paint(0, 0, FB_W, FB_H, 0);
    return fb[y * FB_W + x] & 0xFFFFFF;
}
#define EXPECT(html, x, y, want, what) do { unsigned int got = px(html, x, y); checks++; if (got != (want)) { fails++; printf("FAIL: %-44s got %06X want %06X\n", what, got, (unsigned)(want)); } } while (0)
/* The engine scales CSS px by the font cell / 16 (about 0.69), so a 60x40 CSS px box is 41x28 device pixels: sample inside (0..40, 0..27). */
#define BOX(style) "<body style='margin:0'><div style='width:60px;height:40px;" style "'></div>"

int main(void) {
    /* the canvas: body, html, the bgcolor attribute */
    EXPECT("<body style='background:#0000FF'>x", 200, 120, 0x0000FF, "body background paints the whole canvas");
    EXPECT("<body style='background-color:#0000FF'>x", 200, 120, 0x0000FF, "body background-color");
    EXPECT("<html style='background:#00FF00'><body>x", 200, 120, 0x00FF00, "html background");
    EXPECT("<html style='background:#00FF00'><body style='background:#FF0000'>x", 200, 120, 0x00FF00, "html wins over body for the canvas");
    EXPECT("<body bgcolor='#FF0000'>x", 200, 120, 0xFF0000, "bgcolor attribute");
    EXPECT("<body bgcolor='red'>x", 200, 120, 0xFF0000, "bgcolor=red");
    EXPECT("<style>body{background:navy}</style><body>x", 200, 120, 0x000080, "background from a <style> rule");
    EXPECT("<style>.c{background:#336699}</style><body class='c'>x", 200, 120, 0x336699, "class rule on body");
    EXPECT("<body>x", 200, 120, 0xFFFFFF, "no background: white");
    /* colour syntaxes, on a box */
    EXPECT(BOX("background:#abc"), 10, 10, 0xAABBCC, "#rgb");
    EXPECT(BOX("background:#A1B2C3"), 10, 10, 0xA1B2C3, "#rrggbb");
    EXPECT(BOX("background:rgb(10,20,30)"), 10, 10, 0x0A141E, "rgb(10,20,30)");
    EXPECT(BOX("background:rgb(10 20 30)"), 10, 10, 0x0A141E, "rgb(10 20 30) space syntax");
    EXPECT(BOX("background:rgb(100%,0%,50%)"), 10, 10, 0xFF0080, "rgb with percentages (50% is 128)");
    EXPECT(BOX("background:hsl(120,100%,50%)"), 10, 10, 0x00FF00, "hsl(120,100%,50%)");
    EXPECT(BOX("background:hsl(0,100%,50%)"), 10, 10, 0xFF0000, "hsl(0,100%,50%)");
    EXPECT(BOX("background:navy"), 10, 10, 0x000080, "named: navy");
    EXPECT(BOX("background:rebeccapurple"), 10, 10, 0x663399, "named: rebeccapurple");
    EXPECT(BOX("background:LightGrey"), 10, 10, 0xD3D3D3, "named, mixed case");
    EXPECT(BOX("background:#FF000080"), 10, 10, 0xFF7F7F, "#rrggbbaa over white (50% red)");
    EXPECT(BOX("background:rgba(0,0,255,0.5)"), 10, 10, 0x8080FF, "rgba over white (half, rounded up)");
    EXPECT(BOX("background:transparent"), 10, 10, 0xFFFFFF, "transparent shows the page");
    EXPECT(BOX("background:#F00 !important;background:#00F"), 10, 10, 0xFF0000, "!important");
    /* the shorthand */
    EXPECT(BOX("background:url(x.png) no-repeat #FFFF00"), 10, 10, 0xFFFF00, "shorthand: colour after url()");
    EXPECT(BOX("background:#FFFF00 url(x.png) repeat-x"), 10, 10, 0xFFFF00, "shorthand: colour first");
    EXPECT(BOX("background:red url(x.png)"), 10, 10, 0xFF0000, "shorthand: named colour");
    { unsigned int top = px(BOX("background:linear-gradient(red,blue)"), 2, 1), bot = px(BOX("background:linear-gradient(red,blue)"), 2, 26);
      checks++; if ((top >> 16) < 0xE0 || (top & 255) > 0x20) { fails++; printf("FAIL: gradient top should be red, got %06X\n", top); }
      checks++; if ((bot & 255) < 0xE0 || (bot >> 16) > 0x20) { fails++; printf("FAIL: gradient bottom should be blue, got %06X\n", bot); } }
    EXPECT(BOX("background-color:#00F;background:none"), 10, 10, 0xFFFFFF, "background:none resets the colour");
    EXPECT(BOX("background:#00F;background-color:#F00"), 10, 10, 0xFF0000, "later longhand wins");
    EXPECT(BOX("background-color:#F00;background-color:bogus"), 10, 10, 0xFF0000, "an invalid value is ignored");
    /* box model and inheritance */
    EXPECT(BOX("background:#00F;padding:5px"), 44, 10, 0x0000FF, "the background covers the padding");
    EXPECT(BOX("background:#00F;border:4px solid #F00"), 1, 10, 0xFF0000, "the border is over the background");
    EXPECT(BOX("background:#00F"), 60, 10, 0xFFFFFF, "outside the box nothing is painted");
    EXPECT("<body style='margin:0'><div style='background:#F00;width:60px;height:40px'><p style='margin:0;background:#00F'>hi</div>", 3, 3, 0x0000FF, "a child paints over its parent");
    EXPECT("<body style='margin:0'><div style='background:#F00;width:60px;height:40px'><p style='margin:0'>hi</div>", 35, 24, 0xFF0000, "the parent shows where the child does not reach");
    EXPECT("<body style='margin:0'><div style='background:#F00;width:60px;height:40px'><p style='margin:0;background:inherit'>hi</div>", 3, 3, 0xFF0000, "background:inherit");
    EXPECT("<body style='margin:0'><table style='border-collapse:collapse'><tr><td style='background:#00F;width:40px;height:30px'></td></tr></table>", 10, 10, 0x0000FF, "table cell background");
    EXPECT("<body style='margin:0'><table style='border-collapse:collapse'><tr style='background:#0F0'><td style='width:40px;height:30px'></td></tr></table>", 10, 10, 0x00FF00, "table row background");
    EXPECT("<body style='margin:0'><span style='background:#FF0'>hello</span>", 4, 6, 0xFFFF00, "inline background behind text");
    EXPECT("<body style='margin:0;background:#EEE'><div style='background:currentColor;color:#F00;width:60px;height:40px'></div>", 10, 10, 0xFF0000, "background:currentColor, color declared after");
    EXPECT("<body style='margin:0;background:#EEE'><div style='color:#0F0;background-color:currentcolor;width:60px;height:40px'></div>", 10, 10, 0x00FF00, "background-color:currentcolor, color before");
    EXPECT("<body style='margin:0'><style>div{background:currentColor}.k{color:#00F}</style><div class=k style='width:60px;height:40px'></div>", 10, 10, 0x0000FF, "currentColor with the colour from another rule");
    printf("css-bg: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
