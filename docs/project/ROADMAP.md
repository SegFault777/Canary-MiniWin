# Roadmap and known limits

## Where 1.1-alpha-9 stands

Since alpha-8: a **real file system** (MWFS: directories, `/` and `/mnt/<id>`, unlimited documents and programs, crash-safe by
construction, `part` to format/mount/check/edit partitions); **USB** keyboards, mice (wheel) and tablets on xHCI, EHCI and OHCI;
`os-inst <id> <arch>`; a MiniShell prompt that shows the current directory and Unix/Windows-style commands; **menu bars** in MiniShell,
MiniWeb and MiniFiles; scroll bars you can drag and the mouse wheel, also in Notepad and MiniShell; the apps renamed **MINIWEB** and
**MINISHELL**. See [CHANGELOG.md](CHANGELOG.md).

## Known limits

* **Storage drivers.** Legacy ATA (PIO, LBA28: IDE disks, and SATA disks the firmware shows in IDE/compatibility mode), AHCI SATA
  (DMA), LSI Logic / Fusion-MPT SCSI, NVMe, the boot floppy and ATAPI CD/DVD (read-only). **USB storage** has no driver, so `os-inst` and `part`
  cannot reach USB disks. All of it has only been run on QEMU.
* **USB.** Keyboards, mice and tablets only; no hubs (a device behind one is invisible), no UHCI, no interrupts; verified on QEMU's
  models, not on real hardware. The EHCI-to-companion hand-off for low/full-speed devices follows the specification but could not be
  exercised on QEMU.
* **Install targets.** An existing partition of at least 1 MB (create one with `part create`), on a BIOS machine; whole-disk and
  primary-partition installs can start by themselves (the latter only behind a blank MBR), logical and GPT partitions need a boot
  manager; no UEFI loader. `os-inst` takes `i386`; `x86_64` is refused because there is no 64-bit kernel.
* **Partition editing.** MBR disks only, primary partitions 1-4; GPT, extended and logical partitions can be listed, formatted and
  mounted but not edited.
* **Disk size.** LBA28: the first 128 GB; an MWFS volume is limited to 2^32 sectors, a root volume written by `os-inst` to 8 GB.
* **Files.** Documents are limited to 4096 bytes (Notepad's buffer), programs to 24576. File names are 31 characters
  (`A-Z a-z 0-9 . _ - ,`); paths 96. No timestamps, permissions or links yet. Directories cannot be moved between volumes, and a
  directory is not removed with its contents (`rm` takes empty ones only). Old disks (alpha-8 and before) are not converted.
* **Desktop.** Up to 64 documents and 32 programs get an index entry (the desktop, Notepad, MiniFiles); MiniFiles still shows a flat list
  of `/home/docs` and `/progs`. Notepad's menu bar is its own older code, not the shared widget.
* **A resolved ARP entry never expires**; the file system assumes a drive writes one sector atomically and in order.
* **TLS.** TLS 1.3 (x25519, P-256 and P-384 key shares via HelloRetryRequest; AES-128/256-GCM; RSA-PSS and ECDSA certificates) and TLS 1.2. No session resumption or 0-RTT, no revocation checking, no name constraints (certificates that make either critical are refused). **HTTP/3 over QUIC v1** (`quic.h`, `h3.h`: AES-128-GCM, QPACK without a dynamic table, loss recovery by PTO; no Retry / Version Negotiation / 0-RTT / migration / congestion control -- see the changelog).
* **Ring 3.** Everything runs in ring 0, with no memory protection (now planned for alpha-14).

## The kernel size budget

The kernel is loaded by real-mode BIOS calls into conventional memory (`0x10000`-`0x9FC00`), so image + `.bss` must fit in
about 576 KB, and the image has a 512 KB budget. Since alpha-2 the 245,784-byte Korean font ships LZ4-compressed (62,755
bytes, `kernel/lz.h`) and is inflated to `0x7D0000` at boot: the image is about 337 KB and `.bss` has about 186 KB of
headroom, so the budget is no longer the constraint it was. Still available if it ever is: more tables inflated the
same way (`tools/gen_hangul_font.py` has the compressor), moving `.bss` tables to high memory, loading the kernel above 1 MB.

## The plan (set by the author, revised 2026-10-31)

* **1.1-alpha-12** (done, see the [changelog](CHANGELOG.md)): MiniShell that follows its window, horizontal scroll bars, the disks in
  `/mnt`, nine Settings pages, kerning.
* **1.1-alpha-13** (done): the live ISO, the INSTALL window, the minimal `.img` and the full `.iso`, the Control Panel, the new icons.
* **1.1-alpha-14**: **ring 3**, **more paging**, **JavaScript in part (a little)**, **CSS3 (most of it)** and **HTML5 (most of it)**.
* **1.1-alpha-15**: **process protection**, **memory management**, **a kernel that behaves like a real OS kernel**, and **USB flash
  drives** (mass storage over the existing OHCI/EHCI/xHCI stack).
* **1.1-alpha-16**: the source tree changes shape: modules written as headers (like `css.h`) become directories of `.c` files
  (`css/dom.c`, `css/layout.c`, ...), everywhere -- together with some of the essential features of a modern OS. Among those: **more file
  systems, read-only first**: a **FAT family** reader (FAT12/16/32 and **exFAT**), an **ext family** reader (ext2/3/4), and a **minimal NTFS**
  reader; and a **stronger MWFS**, MiniWin's own file system. **Decided 2026-11-19: alpha-16 only READS foreign file systems** (FAT, ext and
  a partial NTFS); writing to FAT and ext2 comes in beta-9, writing to NTFS is not planned.
* After alpha-16 comes the **beta series**: it closes the gaps listed in [Beta: closing the gaps](#beta-closing-the-gaps) one step at a time.

## Beta: closing the gaps

The author's decision (2026-11-18): after alpha-16 the beta releases remove, step by step, what MiniWin still lacks. The list is the author's;
**the order below was confirmed by the author on 2026-11-19** (it follows what depends on what). Every step keeps the rule of the alphas: the host
tests grow with the feature, the docs and the changelog say what is true, and nothing is called done that has only run on QEMU.

### What is missing today

| gap | state today |
|---|---|
| IPv6 | IPv4 only (`ip.h`, `arp.h`, `dhcp.h`, `dns.h` know no IPv6) |
| IP fragmentation / reassembly | none: we send every datagram as one frame with DF set and `ip.h` does not reassemble |
| TCP sliding window | stop-and-wait with a fixed number of retries; the peer's window is respected (`tcp.h`), our own is at most the 32 KB receive buffer, no window scaling, no congestion control, no SACK |
| Standard socket API | MiniWin's own calls (`net.h`, the MWP network calls); no BSD `socket()`/`bind()`/`listen()`/`accept()` |
| `select` / `poll` | none; everything is polled in the main loop |
| CLI / TUI browser | MiniWeb is graphical only |
| Web image decode | `img.h` decodes PNG, baseline JPEG and the first GIF frame; progressive JPEG, animated GIF, WebP, BMP/ICO and SVG are missing |
| Cookies / sessions | none: `http.h`/`https.h` send and keep no cookies, `document.cookie` reads as an empty string, a site that insists on cookies ends in "Too many redirects" |
| JSON / XML API | JavaScript has `JSON.parse` / `JSON.stringify` (`js.h`); there is no XML parser (`DOMParser`), no `fetch` / `XMLHttpRequest` for scripts, and no JSON or XML parser a native program can call |
| SSH / Telnet | none |
| SMP | one CPU; the others are never started |
| Scheduler | none: one program runs at a time and the kernel never enables interrupts (everything is polled) |
| General synchronization primitives | none (a single thread of control needs none yet) |
| IPC | none |
| Copy-on-write | none |
| Swap | none |
| FAT / ext2 | none until the alpha-16 readers; those are read-only |
| ELF | programs are `.MWP` only |
| POSIX compatibility | none |

### The order

* **beta-1: interrupts and a scheduler.** IDT, PIC/APIC timer, interrupt-driven timekeeping, a process/thread table, a preemptive
  round-robin scheduler with priorities, sleeping and waking. Builds on alpha-14 (ring 3) and alpha-15 (process protection). Everything
  below needs this, because blocking calls need something to switch to.
* **beta-2: synchronization and IPC.** Spinlocks, mutexes, semaphores, condition variables and wait queues; then pipes, message queues
  and shared memory between programs.
* **beta-3: the TCP/IP core.** IP fragmentation and reassembly (with a reassembly timeout and a memory cap), then a real TCP sliding
  window: window scaling, a send window with several segments in flight, fast retransmit, congestion control, delayed ACKs.
* **beta-4: IPv6.** Addresses and the dual-stack choice, neighbour discovery (replaces ARP), ICMPv6, SLAAC and DHCPv6, `AAAA` in DNS,
  IPv6 under UDP, TCP, TLS and QUIC, extension headers (fragmentation from beta-3 carries over).
* **beta-5: sockets.** The BSD socket API (`socket`, `bind`, `listen`, `accept`, `connect`, `send`/`recv`, non-blocking mode, socket
  options) over TCP and UDP, IPv4 and IPv6, in the kernel and in the SDK; then `select` and `poll` on top of the wait queues of beta-2.
  The existing network calls and MiniWeb move onto it.
* **beta-6: the web grows up.** Cookies and sessions (`Set-Cookie`, `Cookie`, `document.cookie`, the cookie rules that matter: domain,
  path, `Secure`, `HttpOnly`, `SameSite`, expiry), JSON and XML as an API (`fetch` and `XMLHttpRequest` for scripts, `DOMParser` and XML
  parsing, a JSON and an XML parser in the SDK for native programs; `JSON.parse` / `JSON.stringify` exist already), and the rest of
  image decoding (progressive JPEG, animated GIF, WebP, BMP/ICO, SVG).
* **beta-7: terminal-side networking.** A CLI / TUI browser inside MiniShell (text layout from the same DOM, links, forms, no pictures),
  and Telnet and SSH clients (SSH needs key exchange, a host-key check, ciphers and a pseudo-terminal; the crypto already in the kernel
  -- X25519, AES-GCM, SHA-2, ECDSA, RSA -- covers part of it, Ed25519 and ChaCha20-Poly1305 are to be added).
* **beta-8: memory.** Copy-on-write pages, then swap (a swap file or partition, page-out and page-in from the fault handler, a simple
  replacement policy).
* **beta-9: file systems, writing.** FAT12/16/32 and ext2 become writable (alpha-16 only has the read-only readers, by decision), with
  the same rule as MWFS: a damaged or hostile disk costs files, never memory. NTFS stays read-only.
* **beta-10: ELF.** An ELF32 loader for static executables and a way to build them (the SDK's toolchain); `.MWP` stays.
* **beta-11: POSIX compatibility.** A libc subset on the syscall layer: file descriptors, `open`/`read`/`write`/`close`, `fork`
  (copy-on-write), `exec` (ELF), `wait`, `pipe`, `dup`, signals in a basic form, `errno`, `select`/`poll`, sockets. "Compatible" means
  that portable programs build and run, not that MiniWin becomes a Unix.
* **beta-12: SMP.** Starting the other CPUs, local APIC and IPIs, per-CPU data and run queues, locks made safe for several CPUs, TLB
  shootdowns. Last, because every earlier step is easier to get right on one CPU.

Why this order: the scheduler and the locks come first because blocking sockets, `select`, pipes and `fork` cannot exist without them;
the TCP and IP work sits before IPv6 and sockets so they are built on the finished stack; copy-on-write must exist before `fork`; ELF
before POSIX; SMP last. One open question remains: which of the cookies and image items can move earlier, because they do not depend on the kernel work.

The notes below are older and kept for their details.

## The next alphas

**1.1-alpha-10** is done (see the [changelog](CHANGELOG.md)): the explorer-style MiniFiles, icons in the start menu, task bar, title
bars and dialogs, the memory manager, the SDK and MWP API version 2. What it did not get to:

* **Save As rebuilt like the explorer** (*Save in:*, up one level, file list, *File name:*, *Save as type:*, Save/Cancel). MiniFiles'
  tree and list code is the model for it.
* **Kerning** for the text renderer (the Latin font is monospaced; "MINIWEB" shows it).
* All UI pieces (buttons, scroll bars, fields, menus, dialogs) as proper modules, and the headers of `kernel/` out of one crowded
  folder into subdirectories.

**1.1-alpha-11** has **programs that run in a window**, **paging** with fault containment, Notepad's menus, program icons, API version 3 and SDK 2.0
(see the [changelog](CHANGELOG.md)). Still planned, in the author's words: **ring 3** (user mode: user segments and a TSS with a kernel stack,
user/supervisor bits on exactly the pages a program owns, `int 0x80` gates in front of the syscall table with pointer validation, one address
space per program -- which also lifts "one program window at a time"); the **built-in apps as dynamically loaded programs** (needs the SDK to
grow first: sockets, Hangul input, settings, a text-and-HTML view -- MiniFiles-style apps are the easiest to start with); and the web engine
work, **HTML5 and CSS3 implemented completely, JavaScript in part**. `kernel/paging.h` and `kernel/apps/progwin.h` are the base for ring 3.

Other ideas, no order: more virtual-machine / hardware compatibility (NICs: e1000e, vmxnet3, PCnet, virtio-net; storage: virtio-scsi,
VMware Paravirtual SCSI, NVMe namespaces beyond the first); USB hubs and USB storage; GPT/UEFI boot; Korean labels in MiniFiles' list.
