# go-link HD

go-link HD is a 2D game engine packaged as a [libretro](https://www.libretro.com/) core: a "virtual board" with modern console-class pictures (a 640 × 360 pixel art screen, or 4:3 and vertical ones, 32-bit color, alpha, blend modes, rotation and scale, lights, color grading, Mode 7 and pseudo 3D roads, skeletal animation) for games made with [Willy Maker](https://maker.go-link.org), for 1 to 8 players. It is written in portable C99 with no dependencies, so it runs in any libretro frontend (RetroArch, [go-link](https://github.com/lordbasex/go-link)'s device, and others) on every platform the libretro buildbot builds for.

Started with no content it plays a **built-in demo**: a platformer for 1 to 4 players, or (core option **Demo: Showcase**) a scene for each of the engine's effects. Given a **game package** (`.glhd`) it plays that game.

## The showcase

L and R (or Select) change scenes, with a pixelate and fade between them:

| Scene | What it shows | Buttons |
|---|---|---|
| Mode 7 | A kart on a track: a picture turned and scaled per row, trees standing on it, fog at the horizon | B go, Y brake, left and right turn |
| Road | A car on a pseudo 3D road: curves, hills hiding what is behind them, trees at the sides | B go, Y brake, left and right steer |
| Cave | A hero animated with bones (walk, idle and jump blended), outlined, with a shadow; darkness, flickering torches, its own light, bloom; bats that find their way to it with A*; an echo; a dialog with a portrait and typing, in the chosen language | left and right walk, B jump |
| Sea | Waves, underwater colors, muffled sound (low pass), bubbles, fish and weed turning | the D-pad swims |
| Colors | The demo's level in night, sepia, underwater, sunset and grey grading, zoomed from 0.5x to 2x, with bloom and blur | left and right colors, up and down zoom, Y bloom, B blur |

## Game packages (.glhd)

A package is a zip (stored or deflate) with:

| File | What |
|---|---|
| `manifest.json` | `format` (the package format: 1, or 2 with effects), `title`, `version`, `genre` (`platformer`), `players` (1 to 8, 4 by default), `screen` (`"16:9"` 640 × 360, `"4:3"` 480 × 360 or `"9:16"` 360 × 640), `level` (the level's file), `sky` (two colors, top and bottom, like `"#3a6ad0"`, optional) and `pictures` (the file of each picture below; each is optional: a missing one keeps the built-in demo's) |
| the level (`level.json`) | `width` (at least the screen's width in cells, up to 1024) and `height` (at least the screen's height, up to 64), `start` (`[column, row]`, the cell where the players stand), `rows` (one text per row, a letter per 16 × 16 cell: `.` empty, `#` ground, `B` brick, `=` one-way platform, `o` coin, `C` checkpoint, `F` goal, `E` an enemy) and, in format 2, `effects` (below) |
| `hero` | PNG of 16 × 24 frames (idle, walk, walk, jump), a row per player: 1 to 8 rows (a player without a row wears row player mod rows) |
| `enemy` | 48 × 16: walk, walk, squashed |
| `tiles` | 64 × 16: ground top, ground, brick, one-way platform |
| `coin` | 64 × 16: four frames of the spin |
| `checkpoint` | 32 × 32: not reached, reached |
| `goal` | 32 × 64 |
| `portrait` | Up to 64 × 64: the face in the level's dialogs (format 2) |
| `lut` | 256 × 16: a color grading table (16 slices of 16 × 16, the usual strip), for `"grade": "lut"` |

### Effects (format 2)

A level's `effects`, every key optional:

| Key | Value |
|---|---|
| `darkness` | 0 (none) to 256 (black) |
| `player_light`, `player_light_color` | The light each player carries: radius in pixels, color |
| `lights` | Up to 32: `{"x": column, "y": row, "radius": pixels, "color": "#ffaa55", "flicker": 0-64}` |
| `grade`, `grade_amount` | `none`, `night`, `sepia`, `underwater`, `sunset`, `grey` or `lut`; 0 to 256 |
| `bloom`, `bloom_threshold` | The glow's strength 0 to 256; how bright a pixel must be to glow |
| `waves` | `{"row": r, "amplitude": px, "wavelength": px}`: everything below row r ripples (water, heat) |
| `zoom` | `"auto"`: the camera zooms out (to 0.5x) when players spread apart |
| `outline`, `shadows` | A color around the characters; a shadow under each |
| `lowpass`, `echo` | The sound: muffled (1 to 255, lower is more) and an echo of 1 to 300 ms |
| `dialogs` | Up to 16: `{"column": c, "name": "Ana", "text": {"en": "...", "es": "...", "pt": "..."}}`, opened when a player reaches the column; Jump shows the whole text, then closes it |

PNGs may be RGBA, RGB, grey or palette (with transparency), 8 bits per channel, not interlaced. Numbers in JSON are integers. A package of a newer `format` is refused with a clear message ("made for a newer go-link HD"); unknown keys are ignored, so a newer package with extra data still plays where its format allows. A package is identified by its SHA-256, which save states carry: a save state of another game is refused. Limits: 256 MB per package, 64 MB per file unpacked, 4096 px per picture side.

`tools/glhd export-demo DIR` writes the built-in demo as a package folder, `tools/glhd pack DIR OUT.glhd` zips it and `tools/glhd check FILE.glhd` loads a package like the core does (its title and SHA-256, or why it cannot play). The tests play `tests/data/demo-deflate.glhd` (the demo zipped with deflate, its PNGs saved by an image library) and check it is the very same game as the built-in one, and that every cut or damaged copy fails cleanly.

## Controls (RetroPad)

| Button | Action |
|---|---|
| D-pad | Move |
| B or A | Jump (hold for a higher jump) |
| Y or X | Run |
| Down + Jump | Drop through a wooden platform |
| Start | Join the game (players 2 to 8, as many as the game takes) or pause |
| Left stick | Move, like the D-pad |
| L, R | Change scenes in the showcase |

The core reads every RetroPad button (L2, R2, L3 and R3 too), each face button on its own, and both analog sticks, on 8 ports.

## Core options

| Key | Values | Default |
|---|---|---|
| `golink_hd_music` | `enabled`, `disabled` | `enabled` |
| `golink_hd_demo` | `platformer`, `showcase` | `platformer` |
| `golink_hd_language` | `auto` (the frontend's), `en`, `es`, `pt` | `auto` |

## Building

```bash
make                 # this computer: golink_hd_libretro.so / .dylib / .dll
make platform=win    # another platform (unix, osx, win, emscripten)
make test            # the engine's tests, with the address and undefined behaviour sanitizers
make tools           # tools/hdrun (a headless frontend) and tools/glhd (package tools)
make bench           # milliseconds per frame of each scene
```

`tools/hdrun CORE [--content FILE.glhd] [--option key=value] --frames N --script tools/runs/walk.txt --shot 60,300 --out DIR` plays a button script on a built core, saves the chosen frames as PNG and prints a hash of all frames and sound, so two builds can be compared. `tools/runs/walk.expected` and `tools/runs/showcase.expected` hold the hashes every platform must give (checked in CI; macOS Clang, Linux GCC on x86_64 and arm64 give the same). A change that alters the picture or the sound on purpose updates it.

## Design rules

- **Deterministic.** Game logic uses only integers and 16.16 fixed point, never `float`, and one seeded random generator kept in the state. The same inputs give the same frames and the same sound on every CPU and in WebAssembly.
- **One state struct.** The whole game is `hd_state` (`src/hd.h`), made only of 32-bit integers. Art, level and sounds are built once at start and only read afterwards.
- **Save states are portable and versioned.** A save state is a header (`GLHD`, the layout version, the size, a checksum, the game's SHA-256) and the state's words in little endian. A save state of another version, of another game or a damaged one is refused, never misread, and the values loaded are brought back into safe ranges. `HD_STATE_VERSION` goes up whenever `hd_state` changes.
- **Software rendering** into a 640 × 360 XRGB8888 frame buffer, so every build draws the same pixels. The frontend scales it (×3 is 1080p, ×6 is 4K).
- **Sound** at 48 kHz stereo, 800 samples per frame, from a 32 channel mixer whose channels live in the state.
- **Budgets:** under 4 ms per frame on the host. `make bench` measures each scene (step, draw and sound) on a 2019 Intel Mac Pro: platformer 0.6 ms, Mode 7 0.5, road 0.3, cave (lights, bloom, A*, bones) 2.6, sea (waves, grade, low pass) 1.0, and colors with grading, zoom, bloom and blur all at once 4.1. Alone, on 640 × 360: grade 0.2 ms, bloom 1.5, blur 0.8, waves 0.15, pixelate 0.3, fade 0.4, lights 0.4.

## Layout

| Path | What |
|---|---|
| `src/libretro.c` | The libretro API: input in, one 60 Hz step, a frame and its sound out |
| `src/game.c` | The rules: movement, jumps, enemies, coins, checkpoints, camera, players joining |
| `src/draw.c` | The platformer's picture: parallax backdrop, tiles, characters, particles, HUD, zoom and the level's effects |
| `src/gfx.c`, `src/fx.c` | Sprites (blend modes, opacity, tint, flash, outline, rotation and scale, shadows) and whole-screen effects (fade, color grading, bloom, blur, waves, pixelate, lights) |
| `src/mode7.c`, `src/road.h` | Mode 7 floors and pseudo 3D roads |
| `src/bones.c` | Skeletal animation: bones, parts, keys interpolated, poses blended, mirrored |
| `src/text.c` | The font (UTF-8, Spanish and Portuguese accents) and dialog boxes |
| `src/path.c` | A* pathfinding on a grid |
| `src/showcase.c` | The showcase demo |
| `src/audio.c` | Sound effects made in code, the music sequencer and the mixer |
| `src/art.c`, `src/level.c` | The demo's pixel art and level, made in code |
| `src/save.c` | Save states |
| `src/content.c` | The loaded game: the built-in demo or a package |
| `src/zip.c`, `src/inflate.c`, `src/png.c`, `src/json.c`, `src/sha256.c` | Reading packages, with no outside libraries; every read is bounds checked |
| `libretro/libretro.h` | The libretro API header (MIT, the RetroArch team) |
| `dist/info/golink_hd_libretro.info` | The core's info file for libretro's core-info repository |

## License

MIT, see [LICENSE](LICENSE). `libretro/libretro.h` keeps its own MIT notice.
