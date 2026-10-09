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

**`weapon`**: the players shoot. Held, the button fires again every `rate` frames; a shot flies straight ahead from the muzzle, stops at a wall and hits the first enemy it touches (a few pixels of grace around its hitbox), which flashes white and is pushed back a little; an enemy takes `enemy_health` hits and pops on the last one. Without `weapon` nobody shoots (the built-in game).

```json
"weapon": {"button": "run", "rate": 8, "speed": 900, "range": 420, "muzzle": [30, -33], "enemy_health": 3}
```

| Key | Default | Range |
|---|---|---|
| `button` | `"run"` (the second action, Y or X) | `"run"`, `"a"`, `"b"`, `"x"`, `"y"`, `"l"`, `"r"` |
| `rate` | 10 frames between shots | 2 to 120 |
| `speed` | 700 (hundredths of a pixel a frame) | 100 to 4000 |
| `range` | 400 pixels | 16 to 4000 |
| `muzzle` | half the hitbox's width plus 4 in front, half its height up | pixels in front of the hitbox's middle and from its feet (up is negative), -256 to 256 |
| `enemy_health` | 1 | 1 to 100 |

With a weapon the run button no longer runs.

`super` (inside `weapon`) is the super attack. Every hit of a player's shots charges it (a bar under the HUD line, gold and blinking when full); with `charge` hits its button throws `granules` in a fan `spread` degrees wide, a little above straight ahead, each a bit faster or slower, falling in arcs and costing an enemy `damage` hits. The player stands still (in the air too) and cannot be hurt for `frames`; the granules leave `release` frames into it. The skin's `super` animation is played once over it and its `granule` pictures draw the granules (else small pellets in the player's color).

```json
"super": {"button": "y", "charge": 6, "granules": 10, "spread": 70, "speed": 650, "range": 260, "damage": 2, "frames": 48, "release": 22}
```

`button` as the weapon's (`"r"` by default), `charge` 1 to 200 (8), `granules` 1 to 24 (10), `spread` 0 to 180 degrees (70), `speed` 100 to 4000 (650), `range` 16 to 4000 pixels (260), `damage` 1 to 100 (2), `frames` 10 to 240 (48), `release` 1 to 240, before `frames` (22).

A skin's `shot` (facing right, looped) and `shot_hit` (the burst where it hits, played once over 18 frames) draw its shots, centered on them; without them a shot is a small glowing pellet in the player's color. A puppet (`rig`) takes its aiming pose while it shoots: the near arm straight ahead at the hip, the finger pistol level.

**`health`**: the players have hits instead of losing coins. An enemy's touch (or a fall) costs one; the last one knocks the player out: it stops where it is for `knockout` frames (the skin's `knockout` animation is played once over them, else it blinks), then comes back at the checkpoint with all its hits. The HUD shows HP next to the coins, red and blinking with `worn` or fewer left, when a puppet also shows its `worn` body (the rig's optional sheet of the same 6 faces worn out) with tired eyes standing still. Without `health` a hit costs coins (the built-in game).

```json
"health": {"hits": 3, "worn": 1, "knockout": 100}
```

`hits` 1 to 99 (3 by default), `worn` 0 to `hits` (1), `knockout` 10 to 600 frames (90).

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
- `stride` (pixels, for `run`): the frames follow the distance walked instead of the time, one whole cycle every `stride` pixels, so the steps match the ground at any speed (walking and running look different without two animations).
- `rig` (in place of the animations) makes the skin a rubber-hose puppet posed again on every frame: a body picture with its faces, gloves and shoes, the arms and legs drawn by the engine as black hoses. The walk follows the distance walked (feet planted on the ground, heel then toe, the arms swinging against the legs, the body bouncing), standing it breathes with its arms hanging, it stretches in the air and squashes on landing, blinks, shouts when it jumps, makes a face when hurt and yawns when bored; the gloves turn the way the forearm points.

  ```json
  "rig": {
    "body": {"file": "red_body.png", "frame": [39, 54], "feet": 2},
    "hand": {"file": "red_hands.png", "frame": [24, 35]},
    "foot": {"file": "red_feet.png", "frame": [27, 20], "feet": 2},
    "limb": 4, "leg": 24, "arm": 15, "stride": 84, "lift": 8, "bob": 3
  }
  ```

  `body` holds 6 faces in a row (normal, blink, shout, hurt, tired, yawn), facing right, standing on its bottom; `hand` 6 gloves (open, fist, finger pistol, wave, pointing up, palm), the wrist's cuff on the cell's left edge at mid height and the fingers to the right; `foot` 4 shoes (flat, toe down, heel down, in the air), the toe to the right and the ankle a third of the way across. A sheet with fewer pictures uses its last one for the rest. `limb` is the hoses' width (1 to 32), `leg` and `arm` their length (4 to 200), `stride` the pixels of a whole walk cycle (two steps), `lift` how high a foot rises (0 to 100), `bob` how much the body bounces (0 to 50). `examples/antidoto/parts.py` cuts an image AI's part sheets into these.
- `from` and `frames` use only part of a picture's frames (`{"file": "germ.png", "frame": [44, 48], "from": 0, "frames": 4}`), so one strip can give several animations.
- The level's things have their own animations too, all optional: `coin` (any pickup), `checkpoint` with `off` and `on`, `goal`, and `enemy` with `walk` and `squashed` (it faces left, like the built-in one; its hitbox is the physics' `enemy_hitbox`). Each stands on its cell's bottom (an enemy on its hitbox's bottom), centered.
- All the sprites together may hold 64 million pixels.

**`textures`**: a tile kind painted as a picture laid over the whole level, so a floor keeps a painting's detail on the level's grid of 16 pixels:

```json
"textures": {"ground_top": "flesh_top.png", "ground": "flesh.png", "brick": "cell.png", "platform": "fold.png"}
```

Each picture's sides are multiples of 16, up to 1024; a cell of that kind shows its own 16 × 16 piece of it (a ground top 16 pixels tall is the same band on every top cell). See-through pixels stay see-through.

Optional pictures make the joins clean:

- `ground_top` and `brick_top` are **bands laid over the inside**: a floor or a wall draws its inside (`ground`, `brick`) in every cell, laid over the whole level so cells always meet whatever their height, and the band over the cells with nothing solid above (its picture's lower edge is part of it). A wall standing on a floor is drawn as a step of the floor (the floor's pictures); a wall that floats takes `brick_bottom` (its inside with each cell's bottom edged) where nothing holds it up.
- **Ends**: `<kind>_left` and `<kind>_right` (for `ground_top`, `ground`, `brick_top`, `brick`, `brick_bottom`, `platform`), the same size as the kind's picture with each cell's piece cut as the end of a run, are drawn where the run stops: a floor at a pit, a platform's tips, a band against a higher wall. Each half of a cell takes its own side's end, so a column one cell wide gets both. Beside a lower step, the higher cell's side is drawn above the step's band and the inside goes on under it.

`examples/antidoto/textures.py` makes all of them from one tileset picture of an image AI: seamless (the extra strip past one edge laid over the opposite one along the cut where the two are most alike), the ends rounded off, inked and shaded.

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

`sounds` replaces the built-in effects it names (`jump`, `coin`, `stomp`, `hurt`, `join`, `check`, `clear`, `pause`, `shoot`, `hit` (a shot hits an enemy that does not pop), `knockout`, `super`, `yawn` (a package's hero standing still, when its bored look starts); up to 10 seconds each, mixed to mono, placed left or right by the game). `music` plays over and over instead of the built-in tune, in stereo (up to 10 minutes), at `volume` 0 to 256 (200 by default), starting again at `loop_from` milliseconds; its position is in the save state, so a loaded state goes on exactly where it was, and `golinkhd_set_music` turns it off like the built-in tune.

**`resolution`**: `"360p"` (the default), `"720p"` or `"1080p"`. The game's rules stay on the logical screen (640 x 360 on 16:9, cells of 16 pixels, the same physics and levels), and the picture is drawn 2 or 3 times bigger: 1280 x 720 or 1920 x 1080 (the API's frame and `golinkhd_get_info` say so). The package's pictures are made for that size: sprites, layers and screens as big as they should look at it, and textures whose sides are multiples of 32 (720p) or 48 (1080p), a cell being 32 or 48 pixels of it; a puppet's hose sizes (`limb`, `leg`, `arm`, `lift`, `bob`) are in its pixels too, its `stride` in the game's. The built-in art, the text, the HUD and the effects are drawn bigger by the engine. The big passes (layers, grading) run on up to 4 cores, the same picture pixel for pixel. Measured on an Intel Mac with ANTÍDOTO: 1.1 ms a frame at 360p, 2.4 at 720p and 5.6 at 1080p (the worst frames 1.8, 5.7 and 14.9; 16.6 is 60 frames a second).

**`art_scale`**: how many times the logical screen the pictures were painted, 1 to 6 (by default the `resolution`'s: 1, 2 or 3; 6 is 2160p). Every pixel number that goes with the pictures (a sprite's `frame` and `feet`, a puppet's hoses, textures' multiples of 16) is in their pixels. The host picks the drawing's size (`golinkhd_set_resolution`, else the package's `resolution`), never bigger than `art_scale`, and pictures painted bigger are made that size as the package loads (each new pixel the average of the ones it covers, weighted by alpha), so one package painted for 1080p plays at 360p, 720p and 1080p. Every picture's transparency is cleaned as it loads: alpha 247 and over becomes solid and 8 and under empty, the soft edge in between stays. Image AIs cut their pictures out of a background themselves and leave the inside of a character at alpha 250-254 (the background showing through a little) and a faint haze around it; cleaned, a character is drawn without blending.

**`levels`**: a game of several levels (up to 16) in one package, in place of `level`. Each one has its own level file (with its effects), `sky`, `layers`, `textures`, `intro` picture and `music`; everything else (heroes, enemies' and things' sprites, sounds, physics, weapon, health, the title and the ending) is shared. Clearing a level shows STAGE CLEAR, then the next one's intro, and the players go on with their coins and health; the ending comes after the last one, then the title. A save state keeps the level it was taken in.

```json
"levels": [
  {"level": "colon.json", "sky": ["#3a1420", "#7a3a3a"], "layers": [...], "textures": {...},
   "intro": "intro_colon.png", "music": {"file": "colon.wav", "volume": 180}},
  {"level": "stomach.json", "intro": "intro_stomach.png", "music": {"file": "stomach.wav"}}
]
```

A level without `sky` uses the manifest's; without `layers`, `textures`, `intro` or `music` it has none (the built-in scenery and tune, no intro).

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

`golinkhd_set_music(e, on)` and `golinkhd_set_language(e, "en" | "es" | "pt")` (texts and dialogs). API 2 adds `golinkhd_set_resolution(e, 360 | 720 | 1080)`: the picture's size for the next `golinkhd_load` (0 is the package's own; see `art_scale`). A host that loads the library at run time looks the function up and goes without it on an older engine.

## Building

```bash
make                 # the shared library for this computer: libgolinkhd.so / .dylib / golinkhd.dll
make static          # libgolinkhd.a
make platform=win    # another platform (unix, osx, win)
make test            # the engine's tests, with the address and undefined behaviour sanitizers
make tools           # tools/hdrun (a headless host of the library) and tools/glhd (package tools)
make bench           # milliseconds per frame of each scene
```

`tools/hdrun LIBRARY [--content FILE.glhd] [--demo showcase] [--language es] [--res 720] --frames N --script tools/runs/walk.txt --shot 60,300 --out DIR` plays a button script on the built library, saves the chosen frames as PNG and prints a hash of all frames and sound, so two builds can be compared. `tools/runs/walk.expected` and `tools/runs/showcase.expected` hold the hashes every platform must give (checked in CI; macOS Clang, Linux GCC on x86_64 and arm64 give the same). A change that alters the picture or the sound on purpose updates it.

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
