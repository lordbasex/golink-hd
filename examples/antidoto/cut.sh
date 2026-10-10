#!/bin/sh
# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
# Cuts every hero strip in source/ into an engine sheet in sprites/, all at
# the idle strip's size (HEIGHT pixels tall, default 80): every strip is
# scaled so the capsule's colored half measures what it does in the idle one.
#   examples/antidoto/cut.sh [HEIGHT]
# RES=2 (720p) or RES=3 (1080p) cuts everything that many times bigger into
# sprites_x2/ or sprites_x3/, from the same pictures.
set -e
cd "$(dirname "$0")"
RES=${RES:-1}
H=$(( ${1:-80} * RES ))
OUT=sprites; [ "$RES" -gt 1 ] && OUT=sprites_x$RES
UV=${UV:-uv}
cut() { "$UV" run -q --with pillow --with numpy --with scipy ../../tools/sprites.py "$@"; }
mkdir -p $OUT
for who in red blue; do
  [ -f source/${who}_idle.png ] || continue
  shell=$(cut source/${who}_idle.png $OUT/${who}_idle.png --height "$H" --frames 6 | sed 's/.*"shell": \([0-9.]*\).*/\1/')
  for anim in walk12:12 run:8 jump:6 shoot:6 shoot_up:6 bored:8 dash:6 tired_idle:6 tired_run:8 dissolve:8 duck:6 hurt:4 win:6 super:8; do
    name=${anim%%:*}; frames=${anim##*:}
    [ -f source/${who}_$name.png ] || continue
    # a dissolve breaks the capsule apart and a super opens it: their size is measured on the first, whole, frame
    # (and a super's spray reaches into the next frames' columns: split by blobs, never by a straight cut)
    first=""; case $name in dissolve) first="--shell-frames 1" ;; super) first="--shell-frames 1 --separate" ;; esac
    cut source/${who}_$name.png $OUT/${who}_$name.png --height "$H" --frames "$frames" --shell "$shell" $first
  done
done
# the enemies and the level's things: their own heights
for item in red_granule:4:11 blue_granule:4:11 red_shot:4:18 blue_shot:4:18 red_shot_hit:6:40 blue_shot_hit:6:40 enemy_germ:6:44 enemy_germ_walk:8:44 enemy_spore:6:44 enemy_spitter:6:52 obj_spit:1:14 obj_vitamin:4:24 obj_leukocyte:4:64 obj_portal:4:120; do
  name=${item%%:*}; rest=${item#*:}; frames=${rest%%:*}; height=${rest##*:}
  [ -f source/$name.png ] || continue
  cut source/$name.png $OUT/$name.png --height "$(( height * RES ))" --frames "$frames"
done
# the bosses, big (two thirds of the screen), and their minions: the same sheets, small; split by blobs
# (a spit or a sneeze reaches into the next frame's column: a straight cut left pieces of it in the wrong frame)
for z in colon intestine stomach lungs heart brain; do
  [ -f source/boss_$z.png ] || continue
  cut source/boss_$z.png $OUT/boss_$z.png --height "$(( 220 * RES ))" --frames 6 --separate
  cut source/boss_$z.png $OUT/boss_${z}_minion.png --height "$(( 56 * RES ))" --frames 6 --separate
done
