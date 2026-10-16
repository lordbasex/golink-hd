#!/usr/bin/env python3
# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
"""Records the voices of TELETRABAJO: CÁMARA APAGADA in English, Spanish and
Portuguese, with speech models that run on the computer: the main
character's, for both (a man and a woman), into
source/voices/<m|w>/<lang>/<key>.wav: the thoughts, the boss's asks read
aloud (read_*), what the worker thinks of each (muse_*) and the answers
chosen in the dialogues (ans_<dialogue>_<answer>); and the boss's (boss_*:
its greeting, replies, comments and checks) into source/voices/b/, and into
source/voices/bw/ the lines whose words differ for the woman (her name).
build.py makes the thoughts sound inside the head and the boss through a
call.

The lines are the game's own: the `voiced` and `boss_voiced` tables in
src/3d/games/remote/remote.c (each text and its key) and their translations
in remote_text.c (with the woman's words where they differ, its `woman`
table), so a text changed there is recorded again here. Who says what is
audio.json's "voices": for each character (m, w, and b the boss) and
language an engine and a voice:

  kokoro  Kokoro-82M (Apache 2.0) through kokoro-onnx: "voice", "speed",
          "phonemes" (espeak-ng's language for the sounds, es-419 for a
          Latin American Spanish)
  piper   a Piper voice (its .onnx and .onnx.json): "model", "length_scale"
          (above 1 is slower)

  python3 -m venv /tmp/tts && /tmp/tts/bin/pip install kokoro-onnx piper-tts soundfile
  # MODEL_DIR holds kokoro-v1.0.int8.onnx and voices-v1.0.bin
  # (https://github.com/thewh1teagle/kokoro-onnx/releases, model-files-v1.0)
  # and each Piper voice's two files (https://huggingface.co/rhasspy/piper-voices)
  /tmp/tts/bin/python examples/remote/voices.py MODEL_DIR [KEY ...]

The phonemes always come from the system's espeak-ng (`brew install
espeak-ng` / `apt install espeak-ng`): the copies inside the wheels look for
their data where they were built.
"""
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
GAME = os.path.join(ROOT, "src", "3d", "games", "remote")
OUT = os.path.join(HERE, "source", "voices")
LANGS = ("en", "es", "pt")
ESPEAK = {"en": "en-us", "es": "es", "pt": "pt-br"}
STRING = r'"((?:[^"\\]|\\.)*)"'


def norm(expr):
    return re.sub(r"\s+", "", expr)


def voiced(name="voiced"):
    """(text expression, key) of every line the game says aloud: the main character's (voiced) or the boss's (boss_voiced)."""
    src = open(os.path.join(GAME, "remote.c"), encoding="utf-8").read()
    table = re.search(r"\} " + name + r"\[\] = \{(.*?)\n\};", src, re.S).group(1)
    return [(norm(e), k) for e, k in re.findall(r"\{\s*([A-Z_0-9+* ]+?),\s*\"([a-z_0-9]+)\"\s*\}", table)]


def texts():
    """Every text of remote_text.c by its expression: {"m": [en, es, pt], "w": [...]}."""
    src = open(os.path.join(GAME, "remote_text.c"), encoding="utf-8").read()
    out = {}
    for e, en, es, pt in re.findall(r"\[([A-Z_0-9+* ]+)\]\s*=\s*\{\s*" + STRING + r",\s*" + STRING + r",\s*" + STRING + r"\s*\}", src):
        line = [t.replace('\\"', '"') for t in (en, es, pt)]
        out[norm(e)] = {"m": line, "w": list(line)}
    table = re.search(r"\} woman\[\] = \{(.*?)\n\};", src, re.S).group(1)
    one = r'(NULL|' + STRING + r')'
    for m in re.finditer(r"\{\s*([A-Z_0-9+* ]+),\s*\{\s*" + one + r",\s*" + one + r",\s*" + one + r"\s*\}\s*\}", table):
        e = norm(m.group(1))
        if e not in out:
            continue  # written another way in the texts (RT_STATUS + 3): never a thought
        for i in range(3):
            t = m.group(3 + 2 * i)
            if m.group(2 + 2 * i) != "NULL":
                out[e]["w"][i] = t.replace('\\"', '"')
    return out


def spoken(line):
    """What the voice reads: the line as shown, without what is only for the eye."""
    line = re.sub(r"([?!])\.\.\.", r"\1", line).replace("...", ",").replace("Zzz", "")
    return re.sub(r"\s+", " ", line).strip()


def phonemes(text, lang):
    """espeak-ng's sounds for a text, clause by clause, its punctuation kept (the voice's pauses and questions)."""
    out = []
    for part, punct in re.findall(r"([^.,;:!?¿¡]+)([.,;:!?]?)", text):
        if part.strip():
            p = subprocess.run(["espeak-ng", "-q", "--ipa", "-v", lang, part], capture_output=True, text=True, check=True).stdout
            out.append(" ".join(p.split()) + punct)
    return " ".join(out)


class Speakers:
    def __init__(self, model):
        self.model = model
        self.kokoro = None
        self.piper = {}

    def say(self, spec, lang, text):
        """(samples as floats, rate)."""
        if spec["engine"] == "kokoro":
            if not self.kokoro:
                from kokoro_onnx import Kokoro

                self.kokoro = Kokoro(os.path.join(self.model, "kokoro-v1.0.int8.onnx"), os.path.join(self.model, "voices-v1.0.bin"))
            return self.kokoro.create(phonemes(text, spec.get("phonemes", ESPEAK[lang])), voice=spec["voice"],
                                      speed=spec.get("speed", 1.0), is_phonemes=True)
        if spec["engine"] == "piper":
            from piper import PiperVoice, SynthesisConfig

            name = spec["model"]
            if name not in self.piper:
                self.piper[name] = PiperVoice.load(os.path.join(self.model, name + ".onnx"))
            v = self.piper[name]
            ids = v.phonemes_to_ids(list(phonemes(text, v.config.espeak_voice)))
            audio = v.phoneme_ids_to_audio(ids, SynthesisConfig(length_scale=spec.get("length_scale")))
            return audio, v.config.sample_rate
        sys.exit(f"unknown engine {spec['engine']}")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    import soundfile

    spec = json.load(open(os.path.join(HERE, "audio.json"), encoding="utf-8"))["voices"]
    speakers = Speakers(sys.argv[1])
    only = set(sys.argv[2:])
    lines = texts()
    jobs = [(expr, key, who, who) for expr, key in voiced() for who in ("m", "w")]
    # the boss: once with the man's words, again where the woman's differ (her name)
    for expr, key in voiced("boss_voiced"):
        jobs.append((expr, key, "b", "b"))
        jobs.append((expr, key, "bw", "b"))
    for expr, key, folder_name, voice in jobs:
        if only and key not in only:
            continue
        if expr not in lines:
            sys.exit(f"{key}: {expr} is not in remote_text.c")
        for i, lang in enumerate(LANGS):
            words = lines[expr]["w" if folder_name in ("w", "bw") else "m"][i]
            if folder_name == "bw" and words == lines[expr]["m"][i]:
                continue
            folder = os.path.join(OUT, folder_name, lang)
            os.makedirs(folder, exist_ok=True)
            text = spoken(words)
            samples, rate = speakers.say(spec[voice][lang], lang, text)
            soundfile.write(os.path.join(folder, f"{key}.wav"), samples, rate)
            print(f"{folder_name} {lang} {key}: {text}")


if __name__ == "__main__":
    main()
