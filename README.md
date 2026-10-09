# go-link HD

go-link HD is a 2D game engine, a library with its own small API (`include/golink_hd.h`): a "virtual board" with modern console-class pictures (a 640 × 360 pixel art screen, or 4:3 and vertical ones, 32-bit color, alpha, blend modes, rotation and scale, lights, color grading, Mode 7 and pseudo 3D roads, skeletal animation) for games made with [Willy Maker](https://maker.go-link.org), for 1 to 8 players. It is written in portable C99 with no dependencies. [go-link](https://github.com/lordbasex/go-link)'s device runs it from Go to play its games in rooms (next to MAME, which go-link runs through libretro), and Willy Maker's play mode will run it in WebAssembly.

Created, it plays a **built-in demo**: a platformer for 1 to 4 players, or (`golinkhd_load_demo(e, 1)`) the **showcase**, a scene for each of the engine's effects. Given a **game package** (`.glhd`) it plays that game.

## The API

```c
#include "golink_hd.h"

golinkhd_config cfg = { GOLINKHD_API_VERSION, my_log, NULL };
golinkhd_engine *e = golinkhd_create(&cfg, &error);
golinkhd_load(e, package, size, &error);    /* or golinkhd_load_demo(e, 0 or 1) */
for (;;)                                    /* 60 times a second */
{
   golinkhd_pad pads[8];                    /* buttons (GOLINKHD_*) and sticks of each player */
   golinkhd_frame_out out;
   golinkhd_frame(e, pads, 8, &out);        /* out.pixels: XRGB8888; out.audio: 800 stereo samples at 48 kHz */
}
golinkhd_state_save(e, buf, golinkhd_state_size(e));
golinkhd_destroy(e);
```

Build it as a shared library (`make`: `libgolinkhd.so`, `.dylib` or `golinkhd.dll`, exporting only `golinkhd_*`) or a static one (`make static`). Compatibility: a host asks for the API version it was written for and the engine serves it and every older one; functions are only ever added. This version runs one engine per program.

The idea of a small contract between an engine and its host (load a game, run one frame, hand over a picture and its sound, serialize the state) comes from [libretro](https://www.libretro.com/), whose API this engine followed at first and which go-link still uses to run MAME. go-link HD now has its own, so it can grow with what go-link needs: events for the room, views per player, several engines in one program.

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

`tools/glhd export-demo DIR` writes the built-in demo as a package folder, `tools/glhd pack DIR OUT.glhd` zips a package folder (the manifest, its level and the pictures it names) and `tools/glhd check FILE.glhd` loads a package like the core does (its title and SHA-256, or why it cannot play). The tests play `tests/data/demo-deflate.glhd` (the demo zipped with deflate, its PNGs saved by an image library) and check it is the very same game as the built-in one, and that every cut or damaged copy fails cleanly.

### Physics and sprites (format 3)

Format 3 adds two keys to the manifest, both optional; a package of format 1 or 2 plays as before.

**`physics`**: the players' hitbox and movement, so a game can have a hero of its own size. The hitbox is in pixels; speeds are hundredths of a pixel per frame and accelerations hundredths of a pixel per frame squared. Every key is optional and the built-in game's value stays for the ones left out:

| Key | Built-in | Range |
|---|---|---|
| `hitbox` | `[10, 22]` | 4 to 128 wide, 4 to 192 tall |
| `enemy_hitbox` | `[14, 12]` | 4 to 128 each |
| `walk`, `run` | 250, 400 | 10 to 2000 |
| `accel`, `air_accel` | 30, 18 | 1 to 500 |
| `friction`, `air_friction` | 25, 5 | 1 to 500, 0 to 500 |
| `gravity`, `gravity_hold` (while jump is held going up) | 45, 28 | 1 to 500 |
| `fall_max` | 700 | 50 to 3000 |
| `jump`, `jump_cut` (the speed a released jump is cut to) | 640, 200 | 50 to 3000, 0 to 3000 |
| `bounce`, `bounce_held` (off an enemy's head) | 450, 700 | 50 to 3000 |

**`sprites`**: the heroes' own pictures, of any size, an animation per state and a skin per player:

```json
"sprites": {"hero": {
  "players": ["red", "blue"],
  "skins": {"red": {
    "idle": {"file": "red_idle.png", "frame": [56, 84], "fps": 8, "feet": 2},
    "run": {...}, "jump": {...}, "hurt": {...}, "bored": {...}, "win": {...}}}}}
```

- `players`: the skin of player 1, 2…; the list repeats for the rest (two skins: odd players wear the first, even the second).
- Each animation is one PNG with its frames in a row, `frame` pixels each (4 to 512), so the frame count is the picture's width over the frame's; `fps` 1 to 60 (10 by default); `feet`, the empty pixels under the feet in every frame. The character's feet stand on the hitbox's bottom, centered on it, and it faces right (the engine mirrors it).
- The states: `idle` (required), `run` (on the ground and moving), `jump` (in the air: the frames go from rising to falling with the speed, leaving out the first and the last, take-off and landing, when there are 4 or more), `hurt` (once, when hit), `bored` (after 6 seconds standing still with no button held: once, then idle for 4 seconds, again and again), `win` (the stage is cleared). A missing one shows `idle`.
- `from` and `frames` use only part of a picture's frames (`{"file": "germ.png", "frame": [44, 48], "from": 0, "frames": 4}`), so one strip can give several animations.
- The level's things have their own animations too, all optional: `coin` (any pickup), `checkpoint` with `off` and `on`, `goal`, and `enemy` with `walk` and `squashed` (it faces left, like the built-in one; its hitbox is the physics' `enemy_hitbox`). Each stands on its cell's bottom (an enemy on its hitbox's bottom), centered.
- All the sprites together may hold 64 million pixels.

**`textures`**: a tile kind painted as a picture laid over the whole level, so a floor keeps a painting's detail on the level's grid of 16 pixels:

```json
"textures": {"ground_top": "flesh_top.png", "ground": "flesh.png", "brick": "cell.png", "platform": "fold.png"}
```

Each picture's sides are multiples of 16, up to 1024; a cell of that kind shows its own 16 × 16 piece of it (a ground top 16 pixels tall is the same band on every top cell). See-through pixels stay see-through.

**`layers`**: painted pictures behind the level, or in front of it, repeated across it:

```json
"layers": [{"file": "far.png", "speed": 20, "y": -20},
           {"file": "mid.png", "speed": 55, "y": 200},
           {"file": "near.png", "speed": 130, "y": 0, "front": true}]
```

Back to front in the list's order, up to 8. `speed` is the share of the camera's movement in hundredths (0 stays still, 100 moves with the level, more passes faster in front), 0 to 400; `y` is the picture's top in level pixels at speed 100 (it moves up and down at its speed too); `front` draws it over the characters. Pictures may be opaque or see-through; with layers, the built-in clouds and hills are not drawn and the manifest's sky fills what no layer covers. Layers and sprites share the 64 million pixels.

**`sounds`** and **`music`**: a game's own sound, as WAV files (PCM, 16 bits, 48000 Hz, mono or stereo):

```json
"sounds": {"jump": "jump.wav", "coin": "gem.wav", "hurt": "ouch.wav"},
"music": {"file": "colon.wav", "volume": 180, "loop_from": 0}
```

`sounds` replaces the built-in effects it names (`jump`, `coin`, `stomp`, `hurt`, `join`, `check`, `clear`, `pause`; up to 10 seconds each, mixed to mono, placed left or right by the game). `music` plays over and over instead of the built-in tune, in stereo (up to 10 minutes), at `volume` 0 to 256 (200 by default), starting again at `loop_from` milliseconds; its position is in the save state, so a loaded state goes on exactly where it was, and `golinkhd_set_music` turns it off like the built-in tune.

**`screens`**: pictures over the whole screen, scaled to it (keeping their shape, cropped from the middle):

```json
"screens": {"title": "start.png", "intro": "level1.png", "ending": "end.png", "intro_seconds": 6}
```

`title` shows before the game, with PRESS START blinking; `intro` is the level's card after start, for `intro_seconds` (1 to 30, 5 by default) or until someone **holds jump** for three quarters of a second (a ring of dots fills while it is held, and "hold A to skip" is written in the game's language); whoever presses start or jump on the title or the intro joins when it ends. `ending` covers the second half of the stage clear.

`tools/sprites.py` cuts an image AI's strip (frames in a row on a transparent background) into such a sheet; [docs/howto/ai-art-and-audio.md](docs/howto/ai-art-and-audio.md) shows the whole path, from the prompts to the package, for [ANTÍDOTO](examples/antidoto).

### The API test games

`examples/` holds three packages made to check everything at once (written by `python3 examples/make-api-test.py`, pictures drawn in colors no built-in picture has, so a picture that failed to load shows as the demo's):

| Folder | What it checks |
|---|---|
| `api-test` | 16:9, 4 players: every picture (8 player rows, the portrait, a LUT), every letter of the level, every effect, and a dialog at each stop (English, Spanish, Portuguese) saying what should be seen there |
| `api-test-43` | The 4:3 screen, 2 players |
| `api-test-916` | The vertical 9:16 screen, 1 player, a level that climbs |

CI packs and checks the three and plays `api-test` with `tools/runs/api.txt` and `--check`, against `tools/runs/api.expected`. `tools/hdrun --check` tries the rest of the API on the run: it prints the game's info, saves the state halfway and plays the rest again from it, restarts and plays it all again, and fails when anything differs; `--music off` turns the music off.

## Controls

| Button | Action |
|---|---|
| D-pad | Move |
| B or A | Jump (hold for a higher jump) |
| Y or X | Run |
| Down + Jump | Drop through a wooden platform |
| Start | Join the game (players 2 to 8, as many as the game takes) or pause |
| Left stick | Move, like the D-pad |
| L, R | Change scenes in the showcase |

A pad (`golinkhd_pad`) carries every button of a modern controller (L2, R2, L3 and R3 too, and each face button on its own) and both analog sticks, for 8 players.

## Settings

`golinkhd_set_music(e, on)` and `golinkhd_set_language(e, "en" | "es" | "pt")` (texts and dialogs).

## Building

```bash
make                 # the shared library for this computer: libgolinkhd.so / .dylib / golinkhd.dll
make static          # libgolinkhd.a
make platform=win    # another platform (unix, osx, win)
make test            # the engine's tests, with the address and undefined behaviour sanitizers
make tools           # tools/hdrun (a headless host of the library) and tools/glhd (package tools)
make bench           # milliseconds per frame of each scene
```

`tools/hdrun LIBRARY [--content FILE.glhd] [--demo showcase] [--language es] --frames N --script tools/runs/walk.txt --shot 60,300 --out DIR` plays a button script on the built library, saves the chosen frames as PNG and prints a hash of all frames and sound, so two builds can be compared. `tools/runs/walk.expected` and `tools/runs/showcase.expected` hold the hashes every platform must give (checked in CI; macOS Clang, Linux GCC on x86_64 and arm64 give the same). A change that alters the picture or the sound on purpose updates it.

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
| `include/golink_hd.h`, `src/api.c` | The API: create, load, one frame (controllers in, picture and sound out), save states |
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

## License

MIT, see [LICENSE](LICENSE).
