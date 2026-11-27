# The first-run setup and the logon prompt (`kernel/apps/oobe.h`, `kernel/syscfg.h`)

*What the user sees is in [INSTALLING.md](../user/INSTALLING.md); this is how it works.*

## When it runs

`oobe_tick()` is called from `render_frame()` once, at the first frame (after `disp_tick()` has applied the system font):

1. a **live disc or a floppy** (`sys_is_live()` = `live_boot` or booted from a floppy): nothing is read or written, no setup, `sys_user` stays `"LIVE"`;
2. otherwise `syscfg_load()` reads `/etc/SYSTEM.CFG`. **None** -> the setup (`oobe_start_setup()`); **found** -> its values are applied, and if
   `/etc/USER.PWD` exists the logon prompt (`oobe_start_logon()`) covers the screen until the password is typed.

While `oobe_open` is 1 the setup/logon owns the mouse (`oobe_mouse()`, hooked in `ui/input.h` before every other mouse branch) and the keyboard
(`oobe_key()`), and `oobe_draw()` (last thing in `render_frame()` before the pointer) paints the whole screen.

## Pages

`OB_LANG, OB_IME, OB_LICENSE, OB_USER, OB_PASS, OB_DESK, OB_SCREEN, OB_ZONE, OB_DATE`, in four phases (`ob_phase()`). Language, input methods and desktop
colour are applied at once; the time zone when leaving its page; everything else at Finish (`ob_finish()`): user name, `upw_store()`, `syscfg_save()`, the
clock (`rtc_write()`: the date typed is local time, converted with `tz_minutes_for()`), then, if another screen size was chosen, the boot byte and
`disp_black_and_restart()`.

The **licence page** loads `/etc/LICENSE.TXT` (English; its 72-column line breaks inside a paragraph are turned into spaces so the box fills) or
`/etc/LICENSE_KO.TXT` (when `sys_language` is Korean), wraps it into lines for the box (`ob_wrap_license()`: line starts and lengths in two heap arrays, at most
`OB_LMAX` = 3200 lines) and shows them with a `lbx_t` (scroll bar, wheel). **Next** is disabled and Enter refuses until "I accept" is chosen; a missing text
shows the address of the licence instead and still asks for acceptance. The texts are files because they are about 80 KB: in the kernel (packed, about 35 KB even
compressed) they would eat most of the room that is left in its 512 KB budget; the installer copies them (`inst_copy_licenses()` in `os_inst.h`).

## The look

Whole-screen, after the Windows 98 first-run pages: `ob_backdrop()` draws the header (name and version, `ob_text2x()` for the big letters), the phase bar, the
blue panel (a gradient and, per phase, two of the large icons magnified with `ob_icon_scaled()`: MiniWin has no photographs) and the thin line;
`ob_gbtn()` draws the gold, rounded Back and Next buttons. Below 480 pixels of width the panel is dropped, below 400 of height the header is. The colours are
fixed (white page, dark red heading), not the theme's.

## Settings that now survive a restart

`syscfg_touch()` (a forward declaration in `sys/lang.h`) is called by Settings after the language, an input method, the desktop colour (Setting and Display) or
the time zone (Date/Time) changes; it rewrites `SYSTEM.CFG` if the machine is set up. Not kept yet: the time format (12/24 hours), Sounds, Mouse and Keyboard
settings.

## Testing it

The stock image has a `SYSTEM.CFG`, so the test scripts boot straight to the desktop. To see the setup, make a copy whose root volume has none -- `tools/test/mk_oobe_image.sh /tmp/x.img` does exactly this:

    cp build/os-image.img /tmp/x.img
    python3 tools/mwfs.py format /tmp/x.img --lba 2373 --sectors 507 --label MINIWIN
    python3 tools/mwfs.py put /tmp/x.img LICENSE --lba 2373 --sectors 507 --path /etc/LICENSE.TXT
    python3 tools/mwfs.py put /tmp/x.img LICENSE.ko.txt --lba 2373 --sectors 507 --path /etc/LICENSE_KO.TXT

(then `IMG=/tmp/x.img tools/test/qemu_plain.sh ...`; in 640x480 Next is at (580,460), Back at (486,460)). `tools/test/qemu_osinst.sh` checks the real path: an
installed disk has both licence texts and no `SYSTEM.CFG`, and its first boot shows the setup's white page.
