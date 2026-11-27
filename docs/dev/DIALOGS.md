# Dialogs are windows (1.1-alpha-14)

A question the system asks -- Notepad's "Save changes?", Save As and Help boxes, MiniWeb's About box, Display Properties' "Keep this screen size?"
and restart question, the Fonts folder's "this font is very small" question and Add Fonts -- is a **window of its own**. Before alpha-14 these were
drawn inside their owner and took every click and key until answered. The code is `kernel/ui/dialog.h`.

## What a dialog window does

* a title bar you can drag, the `_ [] X` buttons, a place in the z-order (clicking it brings it to the front), a button on the task bar with its title
  and icon, and it can be minimized and maximized like any window. A maximized dialog keeps its contents at their own size, in the middle;
* it cannot be resized (it has no grab margin: `ui/resize.h`);
* its **owner is disabled** while it is open: a click on the owner only brings the question forward (`dlg_activate`), keys go to the question
  (the front window), and a right click on the owner opens no menu. The owner window never rises above its question: `win_z_raise()` takes the
  question along. Every other window stays usable;
* **X** is the answer "cancel" (leave everything as it was). For "Keep this screen size?" it is *No* (the safe answer: go back); for the notice
  "cannot show that size" it is OK. Esc and Enter do what they always did when the dialog has the focus.

## How it works

| piece | where |
|---|---|
| the slots: `DLG_MAX` = 9 `window_t`s (`dlg_wins[]`), `dlg_nat_w/h[]` (the size of the contents) | `ui/window.h` |
| window ids `WIN_ID_DLG` (32) and up, `DLG_IS(id)`, `win_slot()`/`win_id_at()` (per-window tables are indexed by slot: z-order, taskbar order, minimize order), `dlg_owner_id()`, `dlg_ox()/dlg_oy()` | `apps/notepad/windows.h` |
| which question is open, the size/title/icon of each, `dlg_poll()`, drawing, clicks, keys, X | `ui/dialog.h` |

The questions themselves did not move: they are still `notepad_t.confirm_mode`, `web_about_open`, `disp_confirm_open`, `dp_ask`, `fnt_warn` and
`afd_on`. `dlg_poll()` runs once per pass of the main loop and at the start of every frame and opens or closes the window to match, so none of the
many places that ask or answer a question had to change. Window ids 32 and up are past every scroll-bar owner id (`SB_MAX_TARGETS` = 32 in
`ui/scrollbar.h` ignores them), so a dialog can never be mistaken for a window with a scroll bar.

**Slots.** `0..NOTEPAD_MAX-1`: the question of Notepad window *i*. `DLG_WEB`: MiniWeb's About box. `DLG_DISP`: "Keep this screen size?" and the
"cannot show that size" notice (the system's own question: **no owner**, it does not disable anything, and its countdown keeps running).
`DLG_DPASK`, `DLG_FNTASK`, `DLG_ADDFONT`: the Setting window's three questions (owner: Setting).

**Contents.** Everything inside a dialog is drawn and hit-tested by the code that always did it (`apps/notepad/dialogs.h`, `file_menu.h`,
`apps/web/view.h`, `apps/setting_display.h`, `setting_font.h`, `setting_addfont.h`), now measured from `dlg_ox(slot)`/`dlg_oy(slot)` -- the dialog's
top-left corner, or the point that centres the contents in a maximized dialog -- instead of from the owner's sheet. The frame, title bar and the three
buttons are drawn by `dlg_draw()`. A click on the dialog calls the page's own click function (`dp_click`, `fnt_click`, `afd_click`,
`disp_confirm_mouse`), and the button it presses is answered by the page's release function exactly as before.

**Add Fonts** sizes itself by the screen and the font alone (`afd_dims()`): the list gets fewer rows on a small screen, and when even that does not
fit the question is dropped with a message.

## Adding a dialog

1. Add a slot (`DLG_x` in `apps/notepad/windows.h`, raise `DLG_MAX` in `ui/window.h` and fix the `_Static_assert`) and its owner in `dlg_owner_id()`.
2. In `ui/dialog.h` add the cases to `dlg_want`, `dlg_kind_of`, `dlg_size_of`, `dlg_title`, `dlg_icon`, `dlg_cancel`, `dlg_draw`, `dlg_click`, `dlg_key`.
3. Make the dialog's contents measure from `dlg_ox/oy` and stop drawing a frame of their own.
