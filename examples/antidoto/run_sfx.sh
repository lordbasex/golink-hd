#!/bin/sh
# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
# Runs make_sfx.py until every effect is made: when the M1 runs out of
# memory the script stops, and the next run goes on from where it was.
cd "$(dirname "$0")"
want=$(grep -c '^    ("' make_sfx.py)
tries=0
while [ "$(ls sfx/*.wav 2>/dev/null | wc -l)" -lt "$want" ] && [ $tries -lt 40 ]; do
  TORCHDYNAMO_DISABLE=1 ~/ai/MOSS-TTS/moss_soundeffect_v2/.venv/bin/python make_sfx.py >> sfx.log 2>&1
  tries=$((tries + 1))
done
echo "done: $(ls sfx/*.wav | wc -l) of $want" >> sfx.log
