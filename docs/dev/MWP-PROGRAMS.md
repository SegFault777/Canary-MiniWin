# Programs (`.MWP`)

A **`.MWP`** ("MiniWin Program") is a small flat binary that the kernel copies to a fixed address and jumps into. This page documents
the interface **as it is today** (1.1-alpha-11, ABI version 3). The practical guide is [sdk/README.md](../../sdk/README.md); the table is
defined once, in [sdk/include/mwp_abi.h](../../sdk/include/mwp_abi.h), and read by both the kernel and programs. Version 1 (the table
below, from alpha-3) is unchanged; version 2 appends memory, file, time, mouse, icon and widget functions.

> **Protection depends on how the program was built.** A program built the classic way (ABI 1/2: the table below) runs in ring 0 with
> the kernel: a buggy one can still overwrite the kernel's data or hang the machine (paging, 1.1-alpha-11, only catches the null page,
> writes into the kernel's code and stack overflows, and only for a program in a window). A program built with `--ring3` (1.1-alpha-14,
> "ABI 3", see *Ring 3* below) runs in ring 3: it cannot touch the kernel at all.

## What a program is

* Raw x86 machine code, compiled freestanding (no libc, no startup files, no relocation) and **linked to run at `0x8F1000`**
  (`MWP_LOAD_ADDR`). The first byte (plus an optional entry offset, normally 0) is the first instruction; it is called with no
  arguments.
* At most **24,576 bytes** (`PROG_MAX_BYTES`), stored as an ordinary file in `/progs` of the root file system (the
  entry offset is kept in the file's inode, see [DISK-LAYOUT.md](DISK-LAYOUT.md)); the file itself is pure code and data. Any number
  of programs fit (the desktop and MiniFiles show the first 32). `cp` can copy a program in from a mounted disk.
* One program runs at a time, and it must return (or loop calling `key_poll`) -- there is no scheduler.

## The syscall table

Programs are built separately from the kernel, so they cannot know the kernel's function addresses. Instead the kernel fills in a
table of function pointers at the fixed address **`0x8F0000`** (`MWP_SYSCALL_TABLE_ADDR`) before it jumps. The layout is the C struct
`mwp_syscalls_t` in `sdk/include/mwp_abi.h` (the kernel's `kernel/mwp.h` includes it; `programs/mwp_api.h` is now a shim for old programs):

| member | meaning |
|---|---|
| `put_pixel(x, y, color)` | set one pixel in the back buffer |
| `fill_rect(x, y, w, h, color)` | filled rectangle |
| `draw_rect(x, y, w, h, color)` | rectangle outline |
| `draw_string(x, y, s, color)` | text, Latin only |
| `draw_string_ko(x, y, s, color)` | text, Latin and Hangul mixed (UTF-8) |
| `present()` | show the back buffer on the screen -- **nothing is visible until this is called** |
| `key_poll()` | one pending key as an ASCII byte, `0` if none; never blocks |
| `screen_w`, `screen_h` | screen size (640 x 480) |

Colours are 32-bit `0x00RRGGBB`. The table is read-only for programs and its order is the ABI: **never reorder it**, only append. The
members above are version 1; version 2 (alpha-10) and version 3 (alpha-11) append memory, files, time, mouse, icons, widgets, the
clipboard, `args`, `set_size`, `set_icon`, `theme_color`, `beep`, `run_flags` and `set_title` -- see `sdk/include/mwp_abi.h` and the table in
[sdk/README.md](../../sdk/README.md).

## The metadata trailer (alpha-11)

A `.mwp` is a raw binary with no header, so an optional **trailer** goes at the END of the file (`tools/mwp_pack.py`, run by
`tools/build_mwp.sh`): the program's own **icon** (16x16 and 32x32, from a PNG), a **title**, and the oldest **ABI version** it needs.
An old kernel never looks at the end of the file; a new one finds it from the last 32 bytes (magic `MWPX`):

```
[ program bytes ... ][ title\0 ][ icon16 RGBA ][ icon32 RGBA ][ 32-byte trailer ]
trailer: "MWPX", u16 version=1, u16 flags (1 icon16, 2 icon32, 4 title), u32 payload_len, u32 icon16_off, u32 icon32_off, u32 title_off, u32 min_abi, u32 0
```

The loader copies only what the kernel needs and refuses a program whose `min_abi` is newer than its own table. Every offset is validated
against the file's length (`kernel/mwp_trailer.h`, tested on damaged files by `tools/test/run_host_mwp.sh`), so a hostile trailer cannot make
the kernel read outside the file. The icons show on the desktop, in the title bar and task bar of the program's window, and in MiniFiles'
`/progs` list (`kernel/mwp_meta.h`). `build_mwp.sh` takes `NAME.png` beside `NAME.c` as the icon and the comments `/* @mwp title: Name */` and
`/* @mwp abi: 3 */` for the rest; or pass `--icon`, `--title`, `--abi`. A program can also set its window icon at run time (`sys.set_icon`).
A program file may now be up to 32 KB (`PROG_MAX_BYTES`) including the trailer.

## Running in a window (alpha-11)

`run NAME [args]`, a double-click and MiniFiles start a program **in a window**; `run -f NAME` gives it the whole screen. See the top comment of
`kernel/apps/progwin.h`: the program is a coroutine (`kernel/coro.h`) on its own guarded stack that hands the CPU back at `present`, `key_poll`
and `sleep_ms`; it draws into a private surface; one program window at a time. With paging on, a CPU exception inside it (null pointer, a
write into the kernel's code, stack overflow, divide by zero, bad instruction) stops that program only -- `sdk/examples/crash.c` provokes each.

## Build and install a program

```sh
tools/build_mwp.sh programs/greeter.c        # -> build/greeter.mwp (flat binary, linked at 0x8F1000)
python3 tools/install_mwp.py build/os-image.img 0 GREETER.MWP build/greeter.mwp   # the 0 is a leftover slot number, ignored; installing again replaces
```

`tools/install_all.sh` does this for the sample program (`programs/greeter.c`, as `/progs/GREETER.MWP`). The program then shows up as a desktop
shortcut, in MiniFiles and in MiniShell's `apps`, and starts with a double-click or `run greeter` -- in a **window** (since alpha-11; `run -f greeter` for the whole screen). How that works, and its limits: `kernel/apps/progwin.h`, `kernel/coro.h`.

## Writing one

Start from the SDK ([sdk/README.md](../../sdk/README.md): `miniwin.h`, a small C library, drawing helpers and a UI toolkit, five examples including a
calculator), or from `programs/greeter.c`. Keep to what a freestanding program can do: no libc (the SDK's `mw_libc.h` has the common pieces,
including `mw_snprintf`), no global constructors, no floating point and no 64-bit division (no libgcc); the stack is 128 KB when it runs in a window. Linker script: `kernel/mwp_link.ld` (the load address is kept in three places that must agree --
`memmap.h`, the linker script and `mwp_api.h`; `mwp.h` asserts the first).

## Ring 3 (alpha-14)

`tools/build_mwp.sh prog.c --ring3` (or a comment line `/* @mwp ring3 */` in the source) builds a program for ring 3: the file is marked
`MWP_FLAG_RING3` (trailer flag 8, minimum ABI 4) and compiled with `-DMW_RING3`, which makes `miniwin.h` include
[`mw_abi3.h`](../../sdk/include/mw_abi3.h). **The source does not change**: `sys.fill_rect(...)` is still `sys.fill_rect(...)`, but `sys` is now
a table in the program's own memory whose entries are tiny stubs that execute `int 0x80` (eax = a `MW_SYS_*` number, ebx ecx edx esi edi =
arguments, the result in eax; a call with six arguments passes a pointer to an int array). The kernel side is [kernel/ring3.h](../../kernel/ring3.h).

What the CPU then enforces, and `examples/ring3.c` lets you try (keys 1-7):
- a read or write of anything that is not the program's own pages -- the kernel's code or data, the syscall table page (0x8F0000), the null
  page, the video memory -- is a **page fault**; `cli`, `in`, `out`, `lgdt`, `mov cr3` are a **general protection fault**; a runaway recursion
  hits the **guard page** under the program's 64 KB stack. Each stops *only that program*; the status line names the fault and the address.
- every pointer a program passes to the kernel is checked first (`pg_user_ok()`, `r3_str()`): a pointer into the kernel gets an error, not a read.

What the program owns: the loader's slot (0x8F1000-0x900000, the part after the file zeroed at launch -- so *initialised* globals work, there is
still no .bss), its stack, and heap blocks from `mem_alloc` (**whole 4 KB pages**, zero-filled, at most 64 blocks / 4 MB; all freed when it ends).
Small differences: `screen_w/screen_h` are refreshed after `present`, `key_poll` and `sleep_ms`; `dir_list` sees the first 8 KB of a listing;
`err_text` writes into a 96-byte buffer of the program. Limits, plainly: one program window at a time, one shared identity-mapped address space
(the user bit marks what is the program's; there is no separate page directory per program yet), and a ring-3 program runs only in a window
(`run -f` refuses it). The call numbers and their arguments are in `mwp_abi.h` (`MW_SYS_*`).

## What is coming

The plan is to turn this into a proper SDK (a `sdk/` directory with headers, a linker script, a build helper and examples), a richer
API (windows, files, events, networking) and per-program address spaces (ring 3 itself arrived in alpha-14). The table above and the
`MW_SYS_*` numbers are the stable contract.
