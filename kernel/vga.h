/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VGA_H
#define VGA_H
#include "io.h"
#include "serial.h"

/* 1.1-alpha-14: the screen size is a RUN-TIME value (what the boot loader's VBE mode really is; boot/stage2.asm picks it, 320x200 or
 * 640x480), so VGA_WIDTH / VGA_HEIGHT are variables that read like the constants they used to be. Everything that RESERVES memory for a
 * screen uses the maxima: the back buffer's region (memmap) and the program-window surfaces. The sizes are 320x200, 640x480 and 854x480;
 * the back buffer moved to 0x390000 (the free top of the net arena) so that the widest one fits below the HTML engine's arena. */
#define VGA_MAX_W  854
#define VGA_MAX_H  480
static int vga_w = VGA_MAX_W, vga_h = VGA_MAX_H;
#define VGA_WIDTH  vga_w
#define VGA_HEIGHT vga_h

/* The video mode itself (a real 640x480, 32-bit-per-pixel, direct-color
 * VBE mode -- true color, not a 256-entry palette) is found and set by
 * the bootloader's second stage in real mode -- BIOS calls fundamentally
 * can't be made from protected mode without extra VM86/real-mode-shim
 * machinery this kernel doesn't have, so mode-setting has to happen
 * before boot/stage2.asm ever switches to protected mode. Unlike the
 * old approach (a hardcoded VESA mode NUMBER for 640x400x8bpp, which
 * happens to be a stable convention only at that specific resolution/
 * depth), there's no equivalent stable number for a 32bpp truecolor
 * mode -- different VBE implementations assign it differently -- so
 * stage 2 actually walks the BIOS's own mode list looking for one that
 * matches, and leaves whichever mode it found sitting in the Mode Info
 * Block at physical address 0x9000 for the kernel to read here.
 *
 * PhysBasePtr (the LFB's physical base address) lives 40 bytes into
 * that block, BytesPerScanLine (the real hardware's row pitch, which
 * isn't guaranteed to equal VGA_WIDTH*4) at offset 16, and the
 * Red/Green/BlueFieldPosition bytes (how far to shift each 8-bit color
 * component into the final 32-bit pixel value) at offsets 32/34/36.
 * Since this kernel runs with paging disabled, "read a physical
 * address" is just "dereference a pointer" -- no mapping step needed. */
static u8 *vga_lfb_ptr = 0;
static u32 vga_pitch = 0;
/* 1.1-alpha-14: the COLOUR DEPTH is a run-time value too. boot/stage2.asm tries 32 bits per pixel first, then 24, 16 (65536 colours) and 256 colours
 * (never fewer). Everything the kernel draws goes into a 32-bit 0x00RRGGBB back buffer; vga_present() converts to what the hardware has.
 * vga_*_shift / vga_*_size are the HARDWARE's channel positions and widths from the mode info block (offsets 31..36). */
static u8  vga_bpp = 32;
static u8  vga_red_shift = 16, vga_green_shift = 8, vga_blue_shift = 0;
static u8  vga_red_size = 8, vga_green_size = 8, vga_blue_size = 8;
#define VGA_MEMORY vga_lfb_ptr

/* The backbuffer this whole kernel draws into (see further down) is
 * 640*480*4 = 1,228,800 bytes -- far too big to live in this kernel's
 * usual low-memory footprint the way the old 8bpp backbuf (250KB) just
 * barely could. Everything else this kernel owns (its own code/data,
 * the stack) stays under the 640KB conventional-memory line on
 * purpose; a 1.2MB array simply doesn't fit there twice over. Placed
 * instead at a fixed physical address well above 1MB, using the exact
 * same "paging is off, so a raw address IS a pointer" trick the LFB
 * pointer above already relies on -- 4MB is comfortably clear of this
 * kernel's own footprint (which tops out well under 1MB) and of any
 * BIOS/legacy reserved regions, assuming the machine has enough RAM to
 * begin with. That assumption is checked, not trusted blindly -- see
 * vga_verify_memory_safe() below, which the kernel calls before this
 * pointer is ever written through. */
#include "memmap.h"
#define VGA_BACKBUF_PHYS_ADDR 0x390000u
#define VGA_LUT_ADDR          0x38F000u                          /* 4 KB: 12-bit colour -> palette index, for a 256-colour mode */
#define VGA_BACKBUF_BYTES ((u32)VGA_MAX_W * VGA_MAX_H * 4)     /* the RESERVATION: the largest screen */
/* Comfortable safety margin above the backbuffer's own footprint, for
 * the kernel's own code/data/stack (all under 1MB) plus headroom --
 * not a tight fit, just "enough that a machine failing this check is
 * unambiguously too small, not a borderline judgment call." */
#define VGA_MIN_RAM_KB ((MW_KERNEL_END / 1024u) + 1024u)       /* the kernel's 1 MB at 0x900000 ends at MW_KERNEL_END; at least 1 MB of heap behind it (the back buffer is below, 0x400000..0x52C000) */

/* ---- a 256-colour mode: the palette and the table that maps any colour to its nearest entry ----
 * Entries 0..15 are the sixteen COL_* colours, 16..231 a 6x6x6 colour cube, 232..255 a ramp of greys. The DAC takes 6 bits per channel. The table
 * is indexed by the colour's top 4 bits per channel (4096 entries), filled once here. */
static u8 *const vga_lut = (u8 *)VGA_LUT_ADDR;
static void vga_setup_palette(void) {
    static const u32 base16[16] = { 0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
                                    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF };
    u8 pal[256][3];
    for (int i = 0; i < 16; i++) { pal[i][0] = (u8)(base16[i] >> 16); pal[i][1] = (u8)(base16[i] >> 8); pal[i][2] = (u8)base16[i]; }
    for (int i = 0; i < 216; i++) { pal[16 + i][0] = (u8)((i / 36) * 51); pal[16 + i][1] = (u8)(((i / 6) % 6) * 51); pal[16 + i][2] = (u8)((i % 6) * 51); }
    for (int i = 0; i < 24; i++) { u8 v = (u8)(8 + i * 10); pal[232 + i][0] = v; pal[232 + i][1] = v; pal[232 + i][2] = v; }
    outb(0x3C6, 0xFF);
    outb(0x3C8, 0);
    for (int i = 0; i < 256; i++) { outb(0x3C9, (u8)(pal[i][0] >> 2)); outb(0x3C9, (u8)(pal[i][1] >> 2)); outb(0x3C9, (u8)(pal[i][2] >> 2)); }
    for (int k = 0; k < 4096; k++) {
        int r = (k >> 8) * 17, g = ((k >> 4) & 15) * 17, b = (k & 15) * 17, best = 0; u32 bd = 0xFFFFFFFFu;
        for (int i = 0; i < 256; i++) {
            int dr = r - pal[i][0], dg = g - pal[i][1], db = b - pal[i][2];
            u32 d = (u32)(dr * dr + dg * dg + db * db);
            if (d < bd) { bd = d; best = i; }
        }
        vga_lut[k] = (u8)best;
    }
}

static inline void vga_init_display(void) {
    {   /* the mode the loader set: XResolution / YResolution of the VBE mode info block it left at 0x9000 */
        int xr = *(u16 *)(0x9000 + 18), yr = *(u16 *)(0x9000 + 20);
        if (xr >= 320 && xr <= VGA_MAX_W && yr >= 200 && yr <= VGA_MAX_H) { vga_w = xr; vga_h = yr; }   /* 320x200, 640x480 or 854x480 */
    }
    vga_lfb_ptr = *(u8 **)(0x9000 + 40);
    vga_pitch = *(u32 *)(0x9000 + 16) & 0xFFFF; /* field is a u16; masked
                                                 * defensively in case
                                                 * whatever garbage sits
                                                 * past it in the block
                                                 * ever got misread as
                                                 * part of a wider access */
    if (vga_pitch == 0) vga_pitch = (u32)VGA_WIDTH * 4; /* paranoia fallback, shouldn't trigger */    /* (a mode of fewer bytes per pixel simply has a smaller pitch: vga_present() uses the real one) */
    vga_bpp = *(u8 *)(0x9000 + 25);
    if (vga_bpp != 24 && vga_bpp != 16 && vga_bpp != 8) vga_bpp = 32;
    vga_red_size    = *(u8 *)(0x9000 + 31);
    vga_red_shift   = *(u8 *)(0x9000 + 32);
    vga_green_size  = *(u8 *)(0x9000 + 33);
    vga_green_shift = *(u8 *)(0x9000 + 34);
    vga_blue_size   = *(u8 *)(0x9000 + 35);
    vga_blue_shift  = *(u8 *)(0x9000 + 36);
    if (vga_bpp == 32 || vga_bpp == 24) { if (!vga_red_size) vga_red_size = 8; if (!vga_green_size) vga_green_size = 8; if (!vga_blue_size) vga_blue_size = 8; }
    if (vga_bpp == 16 && (!vga_red_size || !vga_green_size || !vga_blue_size)) { vga_red_size = 5; vga_red_shift = 11; vga_green_size = 6; vga_green_shift = 5; vga_blue_size = 5; vga_blue_shift = 0; }
    if (vga_bpp == 8) vga_setup_palette();
}

/* Every real 32bpp direct-color VBE implementation this kernel has
 * ever been tested against -- QEMU/Bochs VBE, and every other emulator
 * and real card with any real-world install base -- lays out red at
 * bit 16, green at bit 8, blue at bit 0 (the universal 0x00RRGGBB
 * convention, the same one CSS hex colors and virtually every other
 * 24-bit color format in computing uses). The field positions ARE read
 * from the hardware above rather than just assumed, though, so this
 * function can tell the difference between "matches the convention
 * every COL_* constant below was written assuming" and "doesn't" --
 * and refuses to guess wrong silently if it's ever the latter. */
static inline int vga_color_layout_is_standard(void) {
    return 1;      /* alpha-14: vga_present() converts the 0x00RRGGBB back buffer to ANY channel layout (and to 24, 16 and 8 bits), so there is nothing to refuse */
}

/* Checks the memory-size figure boot/stage2.asm's INT15h/E801h probe
 * left at physical address 0x9200 (in KB) against what the truecolor
 * backbuffer at VGA_BACKBUF_PHYS_ADDR actually needs. Returns 1 if
 * it's safe to proceed, 0 if the probe failed outright (stored as 0)
 * or reported less than VGA_MIN_RAM_KB -- in which case writing
 * through the backbuffer pointer could scribble over real, actively-used
 * memory (or nothing mapped at all) instead of harmless free RAM.
 * Called once, early in kmain(), before anything ever touches
 * backbuf[]. */
static inline int vga_verify_memory_safe(void) {
    u32 total_kb = *(u32 *)0x9200;
    return total_kb >= VGA_MIN_RAM_KB;
}

/* A 0x00RRGGBB colour as the hardware's own pixel value (32, 24 and 16 bits per pixel). */
static inline u32 vga_hw_pack(u32 c) {
    u32 r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    return ((r >> (8 - vga_red_size)) << vga_red_shift) | ((g >> (8 - vga_green_size)) << vga_green_shift) | ((b >> (8 - vga_blue_size)) << vga_blue_shift);
}
static inline u8 vga_idx8(u32 c) { return vga_lut[((c >> 12) & 0xF00) | ((c >> 8) & 0xF0) | ((c >> 4) & 0xF)]; }

/* Straight to the screen, whatever the depth (the blue screen of fatal.h uses this; the colour is 0x00RRGGBB). */
static inline void vga_putpixel(int x, int y, u32 color) {
    if (x < 0 || y < 0 || x >= VGA_WIDTH || y >= VGA_HEIGHT) return;
    u8 *row = VGA_MEMORY + (u32)y * vga_pitch;
    if (vga_bpp == 8) row[x] = vga_idx8(color);
    else if (vga_bpp == 16) ((u16 *)row)[x] = (u16)vga_hw_pack(color);
    else if (vga_bpp == 24) { u32 v = vga_hw_pack(color); u8 *p = row + (u32)x * 3; p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); }
    else ((u32 *)row)[x] = vga_hw_pack(color);
}

/* ---------------------------------------------------------------------
 * Off-screen back buffer + double buffering.
 *
 * Redrawing the whole desktop straight into visible VGA memory every
 * single frame looks like absolute garbage -- flickery, tearing, the
 * works -- especially once windows can open/close/drag/minimize at any
 * moment from a mouse click. So every drawing function below targets
 * `backbuf` (just a chunk of RAM, invisible to the screen) and
 * vga_present() blasts the finished frame over to real VGA memory in one
 * shot at the very end of each loop. Draw all you want in private, only
 * show the final result.
 *
 * backbuf itself is a POINTER to the fixed physical address above, not
 * a plain static array the way the old 8bpp version was -- see
 * VGA_BACKBUF_PHYS_ADDR's own comment for why a buffer this size can't
 * live in the kernel's normal .bss. Every pixel is a full 32-bit direct
 * color value now (built via the RGB() macro below), not a palette
 * index into a 256-color lookup table.
 * ------------------------------------------------------------------- */
/* The pointer the drawing code writes through. It normally holds the real backbuffer; while a program runs in a window
 * (apps/progwin.h) it is pointed at that program's private 640x480 surface instead, so every drawing call the program makes --
 * the kernel's own bb_* functions behind the syscall table -- lands in the surface with no change to any of them. vga_present()
 * always copies from the REAL backbuffer (MW_REAL_BACKBUF), never from whatever this currently points at. */
#define MW_REAL_BACKBUF ((u32 *)VGA_BACKBUF_PHYS_ADDR)
static u32 *mw_bb_ptr = (u32 *)VGA_BACKBUF_PHYS_ADDR;
#define backbuf (mw_bb_ptr)

/* Packs 8-bit red/green/blue components into one direct-color pixel
 * value, honoring whatever field positions vga_init_display() read
 * from the hardware -- though in practice, thanks to
 * vga_color_layout_is_standard()'s check at boot, this only ever
 * actually runs with the universal 16/8/0 layout every COL_* constant
 * below already assumes. Kept as a real shift-based function rather
 * than inlining the assumption directly, so the one place this kernel
 * ever builds an arbitrary (non-constant) color has a single correct
 * way to do it. */
static inline u32 RGB(u8 r, u8 g, u8 b) {
    return ((u32)r << 16) | ((u32)g << 8) | (u32)b;           /* the back buffer is always 0x00RRGGBB; vga_present() converts */
}

/* 1.1-alpha-14: a CLIP RECTANGLE for everything drawn into the back buffer (pixels, rectangles, the icon blits). It is the whole screen
 * until a window sets it around its content (bb_clip_set) -- a window dragged to its smallest height used to draw its icon names and
 * lines straight over the desktop below it -- and the window clears it again (bb_clip_off) before drawing anything that may leave its
 * rectangle (menus, dialogs). Limits are exclusive. */
static int bb_cx0 = 0, bb_cy0 = 0, bb_cx1 = 4096, bb_cy1 = 4096;
static inline void bb_clip_set(int x, int y, int w, int h) { bb_cx0 = x; bb_cy0 = y; bb_cx1 = x + (w > 0 ? w : 0); bb_cy1 = y + (h > 0 ? h : 0); }
static inline void bb_clip_off(void) { bb_cx0 = 0; bb_cy0 = 0; bb_cx1 = 4096; bb_cy1 = 4096; }

static inline void bb_putpixel(int x, int y, u32 color) {
    if (x < bb_cx0 || y < bb_cy0 || x >= bb_cx1 || y >= bb_cy1 || (unsigned)x >= VGA_WIDTH || (unsigned)y >= VGA_HEIGHT) return;
    backbuf[y * VGA_WIDTH + x] = color;
}

static inline void bb_fillrect(int x, int y, int w, int h, u32 color) {
    if (x < bb_cx0) { w -= bb_cx0 - x; x = bb_cx0; }
    if (y < bb_cy0) { h -= bb_cy0 - y; y = bb_cy0; }
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > bb_cx1) w = bb_cx1 - x;
    if (y + h > bb_cy1) h = bb_cy1 - y;
    if (x + w > VGA_WIDTH)  w = VGA_WIDTH - x;
    if (y + h > VGA_HEIGHT) h = VGA_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        u32 *row = &backbuf[(y + j) * VGA_WIDTH + x];
        for (int i = 0; i < w; i++) row[i] = color;
    }
}

static inline void bb_rect(int x, int y, int w, int h, u32 color) {
    for (int i = 0; i < w; i++) { bb_putpixel(x+i, y, color); bb_putpixel(x+i, y+h-1, color); }
    for (int j = 0; j < h; j++) { bb_putpixel(x, y+j, color); bb_putpixel(x+w-1, y+j, color); }
}

static inline void vga_present(void) {
    /* Row-by-row since the hardware's scanline pitch (vga_pitch, in BYTES) is whatever the BIOS reported; the back buffer itself is tightly packed
     * 0x00RRGGBB. The usual case -- 32 bits in the standard red-16 / green-8 / blue-0 order -- is a plain copy; the other depths are converted
     * pixel by pixel (24: three bytes, 16: the hardware's 5-6-5 fields, 8: the nearest palette entry through vga_lut). */
    if (vga_bpp == 32 && vga_red_shift == 16 && vga_green_shift == 8 && vga_blue_shift == 0 && vga_red_size == 8 && vga_green_size == 8 && vga_blue_size == 8) {
        u32 pixels_per_row = vga_pitch / 4;
        for (int y = 0; y < VGA_HEIGHT; y++) {
            const u32 *src = &MW_REAL_BACKBUF[(u32)y * VGA_WIDTH];
            u32 *dst = (u32 *)(VGA_MEMORY + (u32)y * vga_pitch);
            for (u32 i = 0; i < pixels_per_row && i < (u32)VGA_WIDTH; i++) dst[i] = src[i];
        }
        return;
    }
    for (int y = 0; y < VGA_HEIGHT; y++) {
        const u32 *src = &MW_REAL_BACKBUF[(u32)y * VGA_WIDTH];
        u8 *dst = VGA_MEMORY + (u32)y * vga_pitch;
        if (vga_bpp == 8) { for (int i = 0; i < VGA_WIDTH; i++) dst[i] = vga_idx8(src[i]); }
        else if (vga_bpp == 16) { u16 *d = (u16 *)dst; for (int i = 0; i < VGA_WIDTH; i++) d[i] = (u16)vga_hw_pack(src[i]); }
        else if (vga_bpp == 24) { for (int i = 0; i < VGA_WIDTH; i++) { u32 v = vga_hw_pack(src[i]); dst[i * 3] = (u8)v; dst[i * 3 + 1] = (u8)(v >> 8); dst[i * 3 + 2] = (u8)(v >> 16); } }
        else { u32 *d = (u32 *)dst; for (int i = 0; i < VGA_WIDTH; i++) d[i] = vga_hw_pack(src[i]); }
    }
}

/* Direct-to-screen variants -- these still exist but nothing in the
 * kernel actually uses them anymore now that everything goes through the
 * back buffer above. Kept around in case some future feature needs to
 * punch a pixel straight onto the glass without the whole double-buffer
 * dance. */
static inline void vga_fillrect(int x, int y, int w, int h, u32 color) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            vga_putpixel(x + i, y + j, color);
}

static inline void vga_rect(int x, int y, int w, int h, u32 color) {
    for (int i = 0; i < w; i++) { vga_putpixel(x+i, y, color); vga_putpixel(x+i, y+h-1, color); }
    for (int j = 0; j < h; j++) { vga_putpixel(x, y+j, color); vga_putpixel(x+w-1, y+j, color); }
}

static inline void vga_clear(u32 color) {
    for (int y = 0; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            vga_putpixel(x, y, color);
}

/* True 24-bit RGB values now, not palette indices -- the exact same 16
 * colors the old DAC-programmed EGA/VGA palette used (the universal
 * "Windows 16-color" / ANSI-terminal 16-color RGB values, the same
 * numbers essentially every reference table for this classic palette
 * agrees on), just expressed directly instead of through a 4-bit
 * lookup. Every one of the 150+ places in kernel.c that reference
 * COL_BLACK, COL_WHITE, and so on didn't need to change at all for
 * this cutover -- they were always symbolic names, never raw numbers,
 * so only the definitions here and the underlying draw primitives'
 * types (u8 -> u32) had to move. */
#define COL_BLACK       0x000000u
#define COL_BLUE        0x0000AAu
#define COL_GREEN       0x00AA00u
#define COL_CYAN        0x00AAAAu
#define COL_RED         0xAA0000u
#define COL_MAGENTA     0xAA00AAu
#define COL_BROWN       0xAA5500u
#define COL_LGRAY       0xAAAAAAu
#define COL_DGRAY       0x555555u
#define COL_LBLUE       0x5555FFu
#define COL_LGREEN      0x55FF55u
#define COL_LCYAN       0x55FFFFu
#define COL_LRED        0xFF5555u
#define COL_LMAGENTA    0xFF55FFu
#define COL_YELLOW      0xFFFF55u
#define COL_WHITE       0xFFFFFFu

#endif
