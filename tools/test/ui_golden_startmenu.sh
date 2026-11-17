#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Screen tour of the Start menu's Programs folders (1.1-alpha-14) -- the "Programs >" flyout, the Add... wizard (pick a program, Select
# Program Folder, New Folder..., Finish), and the folder / shortcut flyouts -- compared pixel for pixel with tools/test/golden_startmenu/*.png.
#   tools/test/ui_golden_startmenu.sh check     # compare (exit 1 on any difference)
#   tools/test/ui_golden_startmenu.sh record    # (re)write the goldens -- only when a visual change is INTENDED
# The clicks are coordinates: the Start button (20,471), the Programs row (80,405), the Add... row (230,427), the wizard's buttons at
# (483,396) (Next / Finish) and (150,396) (New Folder...). After the shortcut is made the first flyout has one row ("Games") at y=410.
set -uo pipefail
cd "$(dirname "$0")/../.."
MODE=${1:-check}; GOLDEN=${GOLDEN:-tools/test/golden_startmenu}; OUT=/tmp/ui_shots_startmenu
rm -rf $OUT; mkdir -p $OUT /tmp/www; cp tools/test/pages/ui_page.html /tmp/www/
LINES_OUT=1 tools/test/qemu_shot.sh ui_page.html \
  "wait 22" "move 20 471" "click" "wait 1" "shot $OUT/s40_start_menu.ppm" \
  "move 80 405" "click" "wait 1" "shot $OUT/s41_programs_empty.ppm" \
  "move 230 427" "click" "wait 2" "shot $OUT/s42_wizard_pick.ppm" \
  "move 483 396" "click" "wait 1" "shot $OUT/s43_wizard_folder.ppm" \
  "move 150 396" "click" "wait 1" "type Games" "wait 1" "shot $OUT/s44_new_folder.ppm" \
  "key ret" "wait 1" "shot $OUT/s45_folder_made.ppm" \
  "move 483 396" "click" "wait 1" "shot $OUT/s46_done.ppm" \
  "move 483 396" "click" "wait 1" "move 20 471" "click" "wait 1" "move 80 405" "click" "wait 1" "move 230 410" "click" "wait 1" "shot $OUT/s47_flyouts.ppm" \
  "move 380 410" "click" "wait 2" "shot $OUT/s48_launched.ppm" \
  >/dev/null 2>&1
python3 tools/test/ui_diff.py "$MODE" $OUT "$GOLDEN"
