# The desktop and the apps

## The desktop

The desktop is a single list of icons at even spacing, running **top to bottom** on the left. When the next icon would not fit above
the taskbar, the list continues at the **top of the next column to the right**. In order:

1. the five apps: **NOTEPAD, SETTING, MINIWEB, MINISHELL, MINIFILES**;
2. one icon for every **saved document** (a page icon, named after the document);
3. one icon for every **installed program** (a "program" icon).

**Double-click** an icon to open it: an app or document opens a window, a program runs. Long names are shortened to seven letters and
a `~`; the part after the dot is shown on a second line. There is room for as many documents and programs as the disk holds
(the desktop shows up to 64 documents and 32 programs; the rest are reachable from MiniShell).

At the very bottom: a **status line** (hints and the result of what you just did) and the **taskbar** with the start button, one
button per open window and the clock.

## Windows

* **Move** a window by dragging its title bar; **resize** by dragging an edge or corner (the pointer changes to a double arrow).
* The three buttons at the right of the title bar **minimise**, **maximise/restore** and **close**. A minimised window stays on the
  taskbar; click its taskbar button to bring it back.
* Click anywhere on a window to bring it to the front.

## The start menu

Click the button at the bottom left. It lists the apps and a **Shut Down / Restart** pair. *Restart* reboots the machine; *Shut Down*
shows the classic "safe to turn off" screen and then powers the machine off where the firmware allows it (virtual machines and
ordinary PCs do; otherwise the screen simply stays).

**Programs.** The *Programs* row opens a list of your own folders and shortcuts. A folder opens the next list to its right (two levels of
folders at most); a shortcut starts the program. *Add...* at the bottom of the first list opens a small wizard, like the Windows 95 one:
pick the program (Notepad, Setting, MiniWeb, MiniShell, MiniFiles or a program of `/progs`), then **Select Program Folder** -- a tree that
starts at *Start Menu*; press **New Folder...**, type a name and Enter to make a folder inside the selected one -- and **Finish**. The
folders and shortcuts are ordinary files in `/home/start` on the root volume (a shortcut is a tiny text file, `app:NOTEPAD` or `prog:HELLO.MWP`,
named `NAME.LNK`), so MiniFiles and MiniShell can rename or delete them. On a live disc the root volume is in RAM: they last until the
next boot.

## Notepad

A plain-text editor; you can have several windows open at once.

* **Typing:** the keyboard works as expected. **Right Alt** switches to the next enabled input method (English / Korean Hangul).
* **Menu bar (File, Edit, Help):**
  * **File:** *New* (empty page; asks before throwing away unsaved text), *Save* (keep the document's own name), *Save As...*,
    *Close*. Shortcuts: **Ctrl+N**, **Ctrl+S**, **Ctrl+W**.
  * **Edit:** *Undo* (one level; Undo again redoes), *Cut*, *Copy*, *Paste*, *Delete*, *Select All*, *Time/Date* (inserts the clock).
    **Ctrl+Z / X / C / V / A** work too. Notepad types at the **end** of the text and has no cursor to move, so a selection means *all
    of it*: Select All highlights the text, Cut/Copy/Delete act on it, typing over it replaces it, Backspace deletes it, and Paste
    appends the clipboard (or replaces everything when all is selected). The clipboard is shared with programs (`sys.clipboard_*`).
  * **Help:** *Keys* (the shortcuts) and *About Notepad*.
* **New documents are named `DOC.TXT`**, then `DOC (1).TXT`, `DOC (2).TXT`, ... (names may contain spaces inside and parentheses;
  in MiniShell put such a name in double quotes: `cat "DOC (1).TXT"`).
* **Save As:** a small dialog asks for the file name (1-31 letters, digits, `.`, `_`, `-`, `,`, space and parentheses; no leading or trailing
  space). Enter or *Save* saves a copy under the
  new name and leaves the original alone; *Cancel* or **Esc** changes nothing. A name that another document already has is refused.
* **Limits:** a document is up to 4 KB of text; how many you can keep is only limited by the disk. If the disk is full, saving says
  so instead of overwriting anything.
* **Scrolling:** text that is longer than the window scrolls: the scroll bar has arrows, a thumb you can **drag**, page jumps when you
  click the track, and the **mouse wheel** works over the window. Typing follows the end of the text.
* Saved documents get a desktop icon; double-click it to open the document again.

## Setting (the Control Panel)

SETTING.MWP opens the Control Panel: a grid of nine icons, like the Windows 95 one. Double-click an icon (or select it and press Enter)
to open its page; **Back** returns to the grid. The change takes effect at once (nothing is kept across a reboot yet).
**Date/Time** (a Windows 95 style property sheet: the *Date & Time* tab shows a month calendar and an analog clock with the time in digits; the *Time Zone* tab sets the hours from UTC, which the clock and the `tick` command use, and the 12- or 24-hour clock; **OK**, **Cancel** and **Apply** work as usual. The date and the time themselves are shown, not set), **Display** (12- or 24-hour clock; the desktop colour),
**Keyboard** (which input methods Right Alt cycles through), **Mouse** (pointer speed; double-click time), **Power** (restart / shut
down; click twice), **Regional Settings** (menus and messages in English or Korean), **Sounds** (whether programs' beeps are heard),
**System** (version, CPU, memory, screen) and **Font** (kerning: tighter letter pairs such as AV and To in menus and labels; the shell
and Notepad stay fixed-width). When the window is too small, the grid re-flows and gets a scroll bar.

### Fonts (Control Panel > Font)

The Fonts folder shows the fonts as icons: the Galmuri faces that come with MiniWin and the TrueType fonts you have installed. A double
click opens a *preview* of the font (its name, its size in pixels, sample lines drawn in the font itself); **Use as System Font** makes it the
font of the whole desktop at once. A Galmuri face is kept across restarts (the boot sector holds the choice; the live disc cannot keep it); so is a
TrueType font that was copied into `/lib/fonts` (see below): at the next start MiniWin builds its face again, which takes a moment and shows
a *Loading font...* bar. A TrueType font used from another disk lasts until the computer is restarted.

**Install New Font...** (a button, and in the File menu) opens **Add Fonts**: pick a drive (the system disk or a disk mounted under `/mnt`) and
a folder, click the font files (`.ttf`, `.ttc`, `.otf` with TrueType outlines) to select them -- a second click deselects, **Select All** takes
them all -- and press **OK**. With *Copy fonts to Fonts folder* on, each font is copied into `/lib/fonts` and is there after a restart (it needs
room on the system disk: the live disc has almost none); with it off the font is used from where it is, for this session only (it cannot be kept as the system font). A font made
with PostScript outlines (an `.otf` that is not TrueType) is refused. A TrueType font can be drawn at 7, 9, 11 or 14 pixels (the **Size**
button in its preview); the Hangul it lacks is taken from the Galmuri face of the same size. Rendering is not hinted, so ordinary
outline fonts look soft at these sizes; pixel fonts look exact. Up to eight installed fonts are listed. Building a face takes a moment.

## Scroll bars and menus

Every window that scrolls (Notepad, MiniWeb, MiniShell, MiniFiles' list) has the same scroll bar: click an arrow to move a line,
click the track above or below the thumb to move a page, **drag the thumb**, or turn the **mouse wheel** with the pointer over the window.

MiniShell, MiniWeb and MiniFiles have a **menu bar** under the title bar, with menus that fit each app (not copies of Notepad's):
MiniShell: *Shell, View, Tools, Help*; MiniWeb: *Page* (reload, address bar, top of page, close), *Sites*, *Help*; MiniFiles:
*File* (open/run/install, delete, refresh, close), *View* (folders, disks: the disks are the contents of `/mnt`, where **Mount** / **Unmount** / **Install** work on the selected disk or partition), *Help*. Click a title to open its drop-down, click an item
to choose it; clicking anywhere else closes it.

## MiniWeb

A small web browser: an address bar with a **GO** button, links you can click, and an HTML/CSS renderer that is deliberately
limited (no images, no scripts, no floating or absolutely positioned layout). It needs a network card ([FILES-AND-DISKS.md](FILES-AND-DISKS.md)
lists the supported ones) and a DHCP network such as QEMU's `-nic user`. HTTPS is supported with a built-in list of root certificates.
It is a demonstration of the network stack, not a replacement for a real browser.

## MiniShell

A text console: directories and files (`ls cd mkdir cp mv rm ...`), partitions (`part`), system information, installing and power; its
prompt shows the current directory. See [TERMINAL.md](TERMINAL.md).

## Right-click menus

The right mouse button (the *secondary* button: with *Left-handed* on in Setting > Mouse it is the left one) opens a menu for what is under the pointer; the help line of the item under the pointer shows in the status bar. A grey item does not apply now (or is not built yet). A click elsewhere closes the menu.

| where | items |
|---|---|
| the bare desktop | Arrange Icons, Line up Icons, Paste, Paste Shortcut, Undo Delete (grey: not built yet), **New >** (Folder is grey; Shortcut adds one to the Start menu; Text Document opens a new Notepad), **Properties** (opens Display) |
| a desktop icon | **Open**; Add to Zip, Send To, Cut, Copy, Create Shortcut, Delete, Rename, Properties are grey (not built yet) |
| MiniFiles, on a file or folder | **Open, Cut, Copy, Delete, Rename** (they follow the same rules as the buttons: not in read-only or system folders); Send To, Create Shortcut, Properties are grey |
| MiniFiles, on the bare list | **View >** (Folders, Disks, by Name, by Size, by Type, by Date), **Refresh, Paste, New > Folder**; Properties is grey |
| Notepad's text | Undo, Cut, Copy, Paste, Delete, Select All |
| any one-line field (MiniWeb's address, MiniShell's command line, Save As, the rename box) | Cut, Copy, Paste, Delete, Select All (a password field cannot be cut or copied) |
| MiniWeb, on a link | Open Link, Copy Link Address, Back, Forward, Refresh |
| MiniWeb, on the page | Back, Forward, Refresh, Home |
| **MiniShell, on the black output area** | **Copy All** (the whole window, the newest 4096 bytes if there is more), **Paste** (into the command line: the first line of the Clipboard), **Clear** |
| **Control Panel, on an icon** | **Open** |
| **Fonts folder, on a font** | **Open**, **Use as System Font**, **Install New Font...** |
| **Fonts folder, on the empty part** | **Install New Font...** |

A program written for MiniWin can open a menu of its own (`menu_popup`, ABI version 4).

## MiniFiles

The file manager: your documents and programs, and a **DISKS** tab with every disk and partition MiniWin can see: see
[MINIFILES.md](MINIFILES.md).
