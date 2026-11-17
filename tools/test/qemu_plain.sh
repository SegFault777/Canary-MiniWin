#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Boots a PRODUCTION image (no autotest code: the kernel is already close to its size budget, and qemu_shot.sh's extra code no longer fits)
# in QEMU and runs a monitor script (wait / move / click / type / key / shot / raw ...).
# usage: [IMG=build/os-image.img] tools/test/qemu_plain.sh "wait 22" "move 50 84" "click" "shot /tmp/a.ppm"
set -uo pipefail
cd "$(dirname "$0")/../.."
IMG=${IMG:-build/os-image.img}
for p in $(pgrep -f "^qemu-system-i386 -m 128 -drive file=$IMG"); do kill $p 2>/dev/null; done
rm -f /tmp/mon.sock /tmp/serial_plain.log
(setsid qemu-system-i386 -m 128 -drive file=$IMG,format=raw -rtc base=2026-01-02T03:04:05,clock=vm -display none \
   -serial file:/tmp/serial_plain.log -no-reboot -monitor unix:/tmp/mon.sock,server,nowait </dev/null >/tmp/qemu_plain.log 2>&1 &)
for i in $(seq 1 50); do [ -S /tmp/mon.sock ] && break; sleep 0.2; done
python3 tools/test/qemu_mon.py /tmp/mon.sock "$@"
for p in $(pgrep -f "^qemu-system-i386 -m 128 -drive file=$IMG"); do kill $p 2>/dev/null; done
true
