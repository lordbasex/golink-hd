#!/usr/bin/env python3
# Copyright (c) 2026 Federico Pereira <lord.basex@gmail.com>
"""Builds TELETRABAJO: CÁMARA APAGADA's package folder (examples/remote/game):
the manifest ("game": "remote") and its sound, from the recordings listed in
audio.json (each with where it comes from, its author and its license).

  python3 examples/remote/build.py fetch   # downloads the recordings into source/ (once)
  python3 examples/remote/build.py         # game/: the manifest, the effects and the tracks
  tools/glhd pack examples/remote/game remote.glhd && tools/glhd check remote.glhd

Every effect is cut (from "start", "length" seconds, or the "burst"-th sound
of a recording of several, the silence before and after removed), made mono
at 48 kHz and brought to the same loudness; a looped one gets its end
blended into its start, so it goes round without a click. Every track is
made stereo at 48 kHz, its loop blended the same way (unless it is
"seamless" already). Everything is stored as IMA ADPCM (tools/glhd adpcm), a
quarter of the size. CREDITS.md lists every recording.
"""
import array
import json
import os
import shutil
import subprocess
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SOURCE = os.path.join(HERE, "source")
OUT = os.path.join(HERE, "game")
GLHD = os.path.join(ROOT, "tools", "glhd")
RATE = 48000

EFFECT_PEAK = 12000  # every effect at the same loudness, under the music
LOOP_PEAK = 9000     # what goes on (a drill, a shower) quieter than what happens once
TRACK_PEAK = 20000
QUIET = 500          # quieter than this at an effect's start or end is silence
BLEND = 0.25         # seconds of a loop's end blended into its start


def decode(path, channels, start=0.0, length=None):
    """A recording as 16-bit samples at 48 kHz (interleaved when stereo), through ffmpeg."""
    cmd = ["ffmpeg", "-v", "error", "-ss", str(start), "-i", path]
    if length:
        cmd += ["-t", str(length)]
    cmd += ["-f", "s16le", "-acodec", "pcm_s16le", "-ar", str(RATE), "-ac", str(channels), "-"]
    raw = subprocess.run(cmd, check=True, capture_output=True).stdout
    pcm = array.array("h")
    pcm.frombytes(raw[: len(raw) // 2 * 2])
    return pcm


def trim(pcm):
    """The silence before and after cut."""
    a, b = 0, len(pcm)
    while a < b and abs(pcm[a]) < QUIET:
        a += 1
    while b > a and abs(pcm[b - 1]) < QUIET:
        b -= 1
    return pcm[a:b]


def level(pcm, peak):
    top = max((abs(v) for v in pcm), default=0) or 1
    return array.array("h", (max(-32767, min(32767, int(v * peak / top))) for v in pcm))


def fade_out(pcm, channels, seconds=0.03):
    """A short fade at the end, so a cut never clicks."""
    n = min(len(pcm) // channels, int(seconds * RATE))
    out = array.array("h", pcm)
    for i in range(n):
        for c in range(channels):
            k = len(out) - (i + 1) * channels + c
            out[k] = int(out[k] * i / n)
    return out


def burst(pcm, n, gap=0.08):
    """The n-th sound (from 1) of a recording of several (footsteps, ticks): sounds are split by gaps of quiet."""
    quiet = int(gap * RATE)
    bursts, start, still = [], None, 0
    for i, v in enumerate(pcm):
        if abs(v) >= QUIET:
            if start is None:
                start = i
            still = 0
        elif start is not None:
            still += 1
            if still >= quiet:
                bursts.append((start, i - still + 1))
                start, still = None, 0
    if start is not None:
        bursts.append((start, len(pcm)))
    a, b = bursts[min(n, len(bursts)) - 1]
    return pcm[a:b]


def blend_loop(pcm, channels):
    """A loop's last BLEND seconds faded into its first ones, then dropped: the end leads into the start."""
    n = min(int(BLEND * RATE), len(pcm) // channels // 3)
    frames = len(pcm) // channels - n
    out = array.array("h", pcm[: frames * channels])
    for i in range(n):
        w = i / n
        for c in range(channels):
            head = out[i * channels + c]
            tail = pcm[(frames + i) * channels + c]
            out[i * channels + c] = int(head * w + tail * (1 - w))
    return out


def write_wav(path, pcm, channels):
    import wave

    with wave.open(path, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())


def adpcm(pcm, channels, path):
    """A WAV in IMA ADPCM (a quarter of the size), through tools/glhd."""
    tmp = path + ".pcm.wav"
    write_wav(tmp, pcm, channels)
    subprocess.run([GLHD, "adpcm", tmp, path], check=True, stdout=subprocess.DEVNULL)
    os.remove(tmp)


def source_path(entry):
    return os.path.join(SOURCE, entry["file"])


def download(url, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    req = urllib.request.Request(url, headers={"User-Agent": "golink-hd-build"})
    with urllib.request.urlopen(req) as r, open(path + ".part", "wb") as f:
        shutil.copyfileobj(r, f)
    os.replace(path + ".part", path)


def fetch(sounds):
    """Every recording into source/ (a pack's zip is unpacked next to itself); what is there already is kept."""
    import zipfile

    for name, e in sounds.items():
        if os.path.exists(source_path(e)):
            continue
        if "zip" in e:
            z = os.path.join(SOURCE, e["zip"])
            if not os.path.exists(z):
                print("fetching", e["url"])
                download(e["url"], z)
            zipfile.ZipFile(z).extractall(z[: -len(".zip")])
        elif e.get("login"):
            print(f"{name}: download {e['page']} (it needs a login) as {source_path(e)}")
        else:
            print("fetching", name, e["url"])
            download(e["url"], source_path(e))


def build(spec):
    if os.path.exists(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    effects, tracks = {}, {}
    for name, e in spec["effects"].items():
        loop = e.get("loop", False)
        pcm = decode(source_path(e), 1, e.get("start", 0), e.get("length"))
        if "burst" in e:
            pcm = burst(pcm, e["burst"])
        if not loop:
            pcm = fade_out(trim(pcm), 1)
        pcm = level(pcm, LOOP_PEAK if loop else EFFECT_PEAK)
        if loop:
            pcm = blend_loop(pcm, 1)
        if len(pcm) > 30 * RATE:
            sys.exit(f"{name}: {len(pcm) / RATE:.1f} s, an effect is at most 30 s (give it a \"length\")")
        out = f"sfx_{name}.wav"
        adpcm(pcm, 1, os.path.join(OUT, out))
        entry = {"file": out}
        if loop:
            entry["loop"] = True
        if "volume" in e:
            entry["volume"] = e["volume"]
        effects[name] = entry if len(entry) > 1 else out
    for name, t in spec["tracks"].items():
        pcm = level(decode(source_path(t), 2, t.get("start", 0), t.get("length")), TRACK_PEAK)
        if not t.get("seamless"):
            pcm = blend_loop(pcm, 2)  # a loop made so already goes round as it is
        out = f"music_{name}.wav"
        adpcm(pcm, 2, os.path.join(OUT, out))
        tracks[name] = {"file": out, "volume": t.get("volume", 200)}
    manifest = {
        "format": 3,
        "title": "TELETRABAJO: CÁMARA APAGADA",
        "version": spec.get("version", "0.5.0"),
        "game": "remote",
        "players": 1,
        "audio": {"effects": effects, "tracks": tracks},
        "credits": game_credits(spec),
    }
    with open(os.path.join(OUT, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1, ensure_ascii=False)
    credits(spec)
    print(f"{OUT}: {len(effects)} effects, {len(tracks)} tracks")


# The credits that roll in the game (the title's menu, and after a day won), in its three languages.
HEADINGS = {
    "idea": {"en": "IDEA, DESIGN AND CODE", "es": "IDEA, DISEÑO Y PROGRAMACIÓN", "pt": "IDEIA, DESIGN E PROGRAMAÇÃO"},
    "engine": {"en": "ENGINE", "es": "MOTOR", "pt": "MOTOR"},
    "engine_line": {"en": "our own C engine, no outside libraries", "es": "motor propio en C, sin librerías externas", "pt": "motor próprio em C, sem bibliotecas externas"},
    "assisted": {"en": "ASSISTED PROGRAMMING", "es": "PROGRAMACIÓN ASISTIDA", "pt": "PROGRAMAÇÃO ASSISTIDA"},
    "music": {"en": "MUSIC", "es": "MÚSICA", "pt": "MÚSICA"},
    "by": {"en": "by", "es": "por", "pt": "por"},
    "sounds": {"en": "SOUND EFFECTS", "es": "EFECTOS DE SONIDO", "pt": "EFEITOS SONOROS"},
    "license": {"en": "music and sounds in the public domain (CC0)", "es": "música y sonidos de dominio público (CC0)", "pt": "música e sons de domínio público (CC0)"},
    "plays": {"en": "PLAYED WITH", "es": "SE JUEGA CON", "pt": "SE JOGA COM"},
    "logo": {"en": "GO-LINK LOGO", "es": "LOGO DE GO-LINK", "pt": "LOGO DO GO-LINK"},
    "family": {"en": "MY FAMILY", "es": "MI FAMILIA", "pt": "MINHA FAMÍLIA"},
    "special": {"en": "SPECIAL THANKS", "es": "AGRADECIMIENTOS ESPECIALES", "pt": "AGRADECIMENTOS ESPECIAIS"},
    "thanks": {"en": "THANKS", "es": "GRACIAS", "pt": "OBRIGADO"},
    "thanks_lines": {
        "en": ["to everyone who supports me every day", "to those who tried the game", "and to you for playing it"],
        "es": ["a todos los que me apoyan día a día", "a quienes probaron el juego", "y a ti por jugarlo"],
        "pt": ["a todos que me apoiam todos os dias", "a quem testou o jogo", "e a você por jogá-lo"],
    },
    "end": {"en": "THE END", "es": "FIN", "pt": "FIM"},
}


def game_credits(spec):
    out = {}
    for lang in ("en", "es", "pt"):
        h = {k: v[lang] for k, v in HEADINGS.items()}
        lines = ["# TELETRABAJO:", "# CÁMARA APAGADA", "", f"# {h['idea']}", "Federico Pereira", "",
                 f"# {h['engine']}", "go-link HD", h["engine_line"], "", f"# {h['assisted']}", "Claude Code (Anthropic)", "",
                 f"# {h['music']}"]
        for t in spec["tracks"].values():
            lines += [t["title"].split(" (")[0], f"{h['by']} {t['author']}", ""]
        lines.append(f"# {h['sounds']}")
        authors = []
        for e in spec["effects"].values():
            if e["author"] not in authors:
                authors.append(e["author"])
        lines += authors + ["", h["license"], "",
                            f"# {h['plays']}", "go-link", "Pion WebRTC", "libvpx (VP8)", "Opus", "Fyne", "",
                            f"# {h['logo']}", "Noelia Berná", "",
                            f"# {h['family']}", "Lorena Barbosa", "Nicolás Pereira", "",
                            f"# {h['special']}", "Pablo Miranda", "Nicolas Gudino", "Germán Venturino", "Paul Estrella", "",
                            f"# {h['thanks']}"] + h["thanks_lines"] + ["", f"# {h['end']}", ""]
        for l in lines:
            assert len(l.encode()) < 64, l
        out[lang] = lines
    return out


def credits(spec):
    lines = [
        "# TELETRABAJO: CÁMARA APAGADA: the sound's credits",
        "",
        "Generated by `examples/remote/build.py` from `audio.json`. Every recording was cut, leveled, made 48 kHz",
        "and, for the music, looped and stored as IMA ADPCM.",
        "",
        "| In the game | Recording | Author | License | Source |",
        "|---|---|---|---|---|",
    ]
    for kind in ("tracks", "effects"):
        for name, e in spec[kind].items():
            lines.append(f"| {kind[:-1]} `{name}` | {e.get('title', '')} | {e.get('author', '')} | {e['license']} | {e.get('page', e['url'])} |")
    with open(os.path.join(HERE, "CREDITS.md"), "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    with open(os.path.join(HERE, "audio.json")) as f:
        spec = json.load(f)
    if len(sys.argv) > 1 and sys.argv[1] == "fetch":
        fetch({**spec["tracks"], **spec["effects"]})
    else:
        build(spec)


if __name__ == "__main__":
    main()
