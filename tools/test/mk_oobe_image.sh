#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# usage: tools/test/mk_oobe_image.sh OUT.img  --  a copy of the image whose root volume has no SYSTEM.CFG (as an installer would leave it) but has the licence texts
cd "$(dirname "$0")/../.."
cp build/os-image.img "$1"
python3 tools/mwfs.py format "$1" --lba 2373 --sectors 507 --label MINIWIN >/dev/null
python3 tools/mwfs.py put "$1" LICENSE --lba 2373 --sectors 507 --path /etc/LICENSE.TXT >/dev/null
python3 tools/mwfs.py put "$1" LICENSE.ko.txt --lba 2373 --sectors 507 --path /etc/LICENSE_KO.TXT >/dev/null
