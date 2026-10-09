#!/bin/sh
# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
# Cuts every hero strip in source/ into an engine sheet in sprites/, all at
# the idle strip's size (HEIGHT pixels tall, default 80): every strip is
# scaled so the capsule's colored half measures what it does in the idle one.
#   examples/antidoto/cut.sh [HEIGHT]
set -e
cd "$(dirname "$0")"
H=${1:-80}
UV=${UV:-uv}
cut() { "$UV" run -q --with pillow --with numpy --with scipy ../../tools/sprites.py "$@"; }
mkdir -p sprites
for who in red blue; do
  [ -f source/${who}_idle.png ] || continue
  shell=$(cut source/${who}_idle.png sprites/${who}_idle.png --height "$H" --frames 6 | sed 's/.*"shell": \([0-9.]*\).*/\1/')
  for anim in walk12:12 run:8 jump:6 shoot:6 shoot_up:6 bored:8 dash:6 tired_idle:6 tired_run:8 dissolve:8 duck:6 hurt:4 win:6 super:8; do
    name=${anim%%:*}; frames=${anim##*:}
    [ -f source/${who}_$name.png ] || continue
    # a dissolve breaks the capsule apart: its size is measured on the first, whole, frame
    first=""; [ "$name" = dissolve ] && first="--shell-frames 1"
    cut source/${who}_$name.png sprites/${who}_$name.png --height "$H" --frames "$frames" --shell "$shell" $first
  done
done
# the enemies and the level's things: their own heights
for item in red_shot:4:18 blue_shot:4:18 red_shot_hit:6:40 blue_shot_hit:6:40 enemy_germ:6:44 enemy_germ_walk:8:44 enemy_spore:6:44 enemy_spitter:6:52 obj_vitamin:4:24 obj_leukocyte:4:64 obj_portal:4:120; do
  name=${item%%:*}; rest=${item#*:}; frames=${rest%%:*}; height=${rest##*:}
  [ -f source/$name.png ] || continue
  cut source/$name.png sprites/$name.png --height "$height" --frames "$frames"
done
