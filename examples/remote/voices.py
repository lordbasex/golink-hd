#!/usr/bin/env python3
# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
"""Records the worker's thoughts for TELETRABAJO: CÁMARA APAGADA in English,
Spanish and Portuguese with a speech model that runs on the computer
(Kokoro-82M, Apache 2.0, through kokoro-onnx), into source/voices/<lang>/<key>.wav.

The lines are the game's own: the `voiced` table in
src/3d/games/remote/remote.c (each text and its key) and their three
translations in remote_text.c, so a text changed there is recorded again
here. The voices and the speed are audio.json's "voices". build.py then
makes them sound thought (softer, closer, a little room) and packs them as
the effects "voice_<lang>_<key>".

  python3 -m venv /tmp/tts && /tmp/tts/bin/pip install kokoro-onnx soundfile
  # the model: kokoro-v1.0.int8.onnx and voices-v1.0.bin from
  # https://github.com/thewh1teagle/kokoro-onnx/releases (model-files-v1.0)
  /tmp/tts/bin/python examples/remote/voices.py MODEL_DIR [KEY ...]

espeak-ng (the phonemes) comes from the system when the one in the wheel
cannot be loaded: `brew install espeak-ng` / `apt install espeak-ng`.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
GAME = os.path.join(ROOT, "src", "3d", "games", "remote")
OUT = os.path.join(HERE, "source", "voices")
LANGS = ("en", "es", "pt")
KOKORO_LANG = {"en": "en-us", "es": "es", "pt": "pt-br"}


def norm(expr):
    return re.sub(r"\s+", "", expr)


def voiced():
    """(text expression, key) of every thought the game says aloud."""
    src = open(os.path.join(GAME, "remote.c"), encoding="utf-8").read()
    table = re.search(r"\} voiced\[\] = \{(.*?)\n\};", src, re.S).group(1)
    return [(norm(e), k) for e, k in re.findall(r"\{\s*([A-Z_+ ]+?),\s*\"([a-z_]+)\"\s*\}", table)]


def texts():
    """Every text of remote_text.c by its expression: [en, es, pt]."""
    src = open(os.path.join(GAME, "remote_text.c"), encoding="utf-8").read()
    s = r'"((?:[^"\\]|\\.)*)"'
    out = {}
    for e, en, es, pt in re.findall(r"\[([A-Z_0-9+ ]+)\]\s*=\s*\{\s*" + s + r",\s*" + s + r",\s*" + s + r"\s*\}", src):
        out[norm(e)] = [t.replace('\\"', '"') for t in (en, es, pt)]
    return out


def spoken(line):
    """What the voice reads: the line as shown, without what is only for the eye."""
    line = line.replace("...", ",").replace("Zzz", "")
    return re.sub(r"\s+", " ", line).strip()


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    import soundfile
    from kokoro_onnx import Kokoro
    from kokoro_onnx.config import EspeakConfig

    spec = json.load(open(os.path.join(HERE, "audio.json"), encoding="utf-8"))["voices"]
    model = sys.argv[1]
    only = set(sys.argv[2:])
    espeak = None
    for lib, data in (("/usr/local/lib/libespeak-ng.dylib", "/usr/local/share/espeak-ng-data"),
                      ("/opt/homebrew/lib/libespeak-ng.dylib", "/opt/homebrew/share/espeak-ng-data"),
                      ("/usr/lib/x86_64-linux-gnu/libespeak-ng.so.1", "/usr/lib/x86_64-linux-gnu/espeak-ng-data")):
        if os.path.exists(lib) and os.path.isdir(data):
            espeak = EspeakConfig(lib_path=lib, data_path=data)
            break
    k = Kokoro(os.path.join(model, "kokoro-v1.0.int8.onnx"), os.path.join(model, "voices-v1.0.bin"), espeak_config=espeak)
    lines = texts()
    for expr, key in voiced():
        if only and key not in only:
            continue
        if expr not in lines:
            sys.exit(f"{key}: {expr} is not in remote_text.c")
        for i, lang in enumerate(LANGS):
            os.makedirs(os.path.join(OUT, lang), exist_ok=True)
            text = spoken(lines[expr][i])
            samples, rate = k.create(text, voice=spec[lang], speed=spec.get("speed", 1.0), lang=KOKORO_LANG[lang])
            soundfile.write(os.path.join(OUT, lang, f"{key}.wav"), samples, rate)
            print(f"{lang} {key}: {text}")


if __name__ == "__main__":
    main()
