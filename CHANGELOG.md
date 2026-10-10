# Changelog

## [Unreleased]

### Added
- Package format 3: `physics` (the players' hitbox, speeds, gravity and jump) and `sprites` (the heroes' own pictures of any size: idle, run, jump, hurt, bored and win animations, a skin per player) and `layers` (painted pictures behind or in front of the level, repeated across it at their own speed), `sounds` and `music` (WAV effects in place of the built-in ones, and a stereo song played over and over, its position in the save state). Save states are version 5 (each player's frames standing still).
- ANTÍDOTO (`examples/antidoto`), a game whose art and music are made with AI: the heroes' strips, `cut.sh` and `recolor.py` (Azul is Rojo recolored), `build.py` and `make_tracks.py`; `tools/sprites.py` cuts an image AI's strip into an engine sheet; `docs/howto/ai-art-and-audio.md` logs every prompt and command.
- Format 3 also has the enemy's hitbox, sprites for the level's things (pickup, checkpoint, goal, enemy), `from`/`frames` to use part of a strip, `stride` (a run animation that follows the distance walked), `textures` (a tile kind painted as a picture laid over the level) and `screens` (title, a level intro skipped by holding jump, ending).
- `tools/glhd pack` packs every file the manifest names: sprites, layers, textures and sounds.
- The `yawn` sound: a package's hero standing still yawns when its bored look starts (and with each yawn of a puppet); ANTÍDOTO has all its 24 effects.
- `resolution` (format 3): 720p and 1080p, the picture drawn 2 or 3 times bigger from pictures made for it while the rules stay on the logical screen; the big passes (layers, grading) on up to 4 cores, the same pixels; a solid layer covering the screen skips the sky under it; `tools/hdrun --time` (each frame's wall-clock time: average and worst); a test. ANTÍDOTO's scripts take `RES=2` or `RES=3` (cut.sh, parts.py, textures.py, build.py) and cut its art again from the originals.
- `levels`: several levels in one package, each with its own level file, sky, layers, textures, intro and music; clearing one goes on to the next with the players' coins and health, the ending after the last; the state keeps the level played (save states too); `glhd pack` packs every level's files and `glhd check` counts them; a test. ANTÍDOTO has its six: colon, intestine, stomach, lungs, heart and brain (the first one hand-made, the others made from a fixed seed, harder as the body goes up).
- `super` (in `weapon`): the super attack, charged by the shots' hits (a bar in the HUD), throws a fan of granules that fall in arcs and cost `damage` hits; the player stands still and unhurt; the skin's `super` and `granule` pictures, the `super` sound. With a weapon the run button no longer runs. ANTÍDOTO: X shoots, Y opens the capsule (10 granules after 6 hits). `tools/sprites.py --separate` splits frames whose sprays reach into each other's columns by their blobs (a straight cut showed as a box around the spray). ANTÍDOTO's effects are quieter.
- `health`: hits instead of coins; the last one knocks the player out (the skin's `knockout` animation, the `knockout` sound), back at the checkpoint with all its hits; HP in the HUD, red and blinking when `worn`; a puppet's optional `worn` body and tired eyes. ANTÍDOTO: 3 hits, the worn capsule (faded, scuffed, a bandage) with the last one, the dissolve at none. `tools/sprites.py --shell-frames N` measures the size on the first frames only (a dissolve breaks the capsule apart).
- `weapon`: the players shoot (a button held fires every `rate` frames; shots fly straight, stop at walls and hit enemies, which take `enemy_health` hits, flash white and pop on the last one); a skin's `shot` and `shot_hit` pictures, the `shoot` and `hit` sounds (built-in ones too); a puppet takes its aiming pose while it shoots. ANTÍDOTO's finger pistol fires Y-shaped antibodies (Azul's recolored), a germ takes 3, and two germs wait before the first gap.
- `rig`: a skin can be a rubber-hose puppet (a body with 6 faces, 6 gloves, 4 shoes; arms and legs drawn as hoses) posed again on every frame from the player's state, so the walk is as smooth as the screen with 16 small pictures: planted feet, swinging arms, gloves turned along the forearm, bounce, breath, stretch and squash, blinks, shouts, hurt and yawn faces. ANTÍDOTO's heroes use it (`examples/antidoto/parts.py` cuts the part sheets; Azul's are recolored).
- `art_scale` (format 3) and `golinkhd_set_resolution` (API 2): the host picks the picture's size (360p, 720p, 1080p) and pictures painted bigger are made that size as the package loads (by area, weighted by alpha, in integers: the same pixels on every computer), so one package painted for 1080p plays at every size; `tools/hdrun --res`; a test. Every picture's transparency is cleaned as it loads (nearly solid becomes solid, nearly empty becomes empty, the soft edge stays): an image AI leaves the inside of a character at alpha 250-254.
- ANTÍDOTO's floors are seamless: `textures.py` lays each texture's extra strip over the opposite edge along the cut where the two are most alike (no stroke shows twice) and resizes it as a repeating picture (a hard seam showed every 6 cells in HD).
- Texture joins (format 3): a floor's and a wall's top band (`ground_top`, `brick_top`) is laid over the inside, so cells of any height meet; a wall on a floor is drawn as a step of it, a floating wall takes `brick_bottom`; ends (`<kind>_left`, `<kind>_right`) where a run stops, a half cell each, and steps' inner corners; a test. ANTÍDOTO's six zones have all of them (rounded, inked ends; platforms and floating walls outlined).
- ANTÍDOTO's floors drawn to repeat by the image AI (`source/tex_<zone>_flesh.png`, `tex_<zone>_top.png`): `textures.py` uses them as they are, only the small step left at their edges spread over a few pixels, so no join is cut or invented.
- `enemies` (format 3): a spore (`S`, flies bobbing after the nearest player) and a spitter (`P`, stands and lobs arcs at players in front of it), with their sprites (`spore`, `spitter`, `spit`); the enemies' shots in the state.
- A level's `boss` (format 3, `X`): it wakes up on screen (its music, a roar, a health bar with its name, the camera on its arena) and does its attacks in turn after a windup (`advance`, `jump` with a shock wave, `charge`, `spit`, `brood`: small minions flying in from everywhere), angry at half its health; beaten, the goal opens. Sounds `spit`, `roar`, `boss_hit`, `boss_down`.
- `dash` (format 3): a button throws the player forward, gravity off and through enemies, once in the air; the skin's `dash` animation; the `dash` sound.
- WAV files may be IMA ADPCM (a quarter of the size), decoded with integers as the package loads; `tools/glhd adpcm` makes one from a PCM WAV.
- A package's pictures may hold 96 million pixels (64 before): every level's textures and bosses count.
- ANTÍDOTO: six bosses drawn by the image AI (FAGO REX the spider creeps at the heroes and back and lets its brood out, LOMBRIZ VÍRICA, ÁCIDO BARÓN, GRIPÓN, REY CÁPSIDE, NEUROVIRUS), each zone's arena with a checkpoint, the zones' own enemies (spitters from the intestine, spores from the stomach), the dash on A, the music in ADPCM (the 1080p package from about 228 MB to about 158 MB, two more songs in it).
- A picture may be 8192 pixels on a side (4096 before; its decoded size is still capped): a boss's strip at 1080p is wider than 4096.
- Tests: the new enemies, a boss, the dash and ADPCM.
- A boss fight shows the whole boss: it wakes up once its whole picture is on the screen, the camera stays at 1x and at the arena's floor (following the players' jumps up cut its feet off), and the boss stays inside what the camera shows, after the camera moves too.
- `tools/sprites.py --separate`: a piece drawn in one stroke goes whole with one frame, a loose piece (a drop, a loose arm) with the frame drawn nearest it, and a spray keeps its drops (a boss's attack was cut, pieces of it in the next frame).
- `tools/hdrun --every N --audio FILE.raw`: every Nth frame as PNG and the run's sound, to make a video with ffmpeg.

### Added
- The API test games (`examples/`, written by `examples/make-api-test.py`): a 16:9 game that uses every picture, letter and effect of the package format with a dialog at each stop, and 4:3 and 9:16 ones; CI packs, checks and plays them (`tools/runs/api.txt`, `api.expected`).
- `tools/hdrun --check` (the game's info, a state saved halfway and played again, a restart played again) and `--music off`.

### Fixed
- A player walking on flat ground was in the air every other frame (the floor was looked for at the feet's whole pixel, so sinking half a pixel missed it), which drew a jump frame every other frame; and on the ground the speed crept past the top speed by 0.05 pixels a frame (the two bugs hid each other). The floor is looked for with the fraction, and the speed stops at the top speed. Found by measuring a video of ANTÍDOTO frame by frame; the demo's and the API test's hashes changed (identical on macOS and Linux).
- The goal and checkpoints count when a player passes their column near their height (from 2 rows below to 8 above, so jumping over still counts): a level that climbs on a 9:16 screen no longer clears when the player runs under its goal. Found by the 9:16 API test in a go-link room.
- Centered texts (the title, STAGE CLEAR!, PAUSE) get smaller when they would not fit a 4:3 or 9:16 screen; a long title was cut.
- `tools/glhd pack` packs the files the manifest names (its level and pictures, the portrait and the LUT too), not a fixed list that needed every demo picture.

## [0.2.0] - 2026-10-08

The first public version; go-link 0.2.8 ships it inside the device.

### Changed
- **Its own API instead of libretro's:** the engine is a library (`libgolinkhd`, shared or static) with a small, versioned API in `include/golink_hd.h` (`golinkhd_create`, `golinkhd_load`, `golinkhd_frame`, save states, settings); only `golinkhd_*` leaves the library. The libretro adapter, its header and its info file are gone; libretro's API, which the engine followed at first, is credited as the idea behind it. Opposite directions held together now cancel in the engine, for every host. `tools/hdrun` hosts the built library through the API (`--demo`, `--language`); the platform hashes are unchanged.

### Added
- Up to 8 players (a package says how many, 4 by default) on 8 ports; both analog sticks; L, R, L2, R2, L3, R3 and each face button.
- The screen per game: 16:9 (640 × 360), 4:3 (480 × 360) or 9:16 (360 × 640).
- Graphics: blend modes (add, multiply, screen), opacity, tint, flash, outlines, sprites turned and scaled, shadows; Mode 7 floors and pseudo 3D roads; skeletal animation (bones, parts, keys, blends, mirrored).
- Whole-screen effects: fades, color grading (presets and LUT strips), bloom, blur, waves, pixelate, 2D lights with darkness; the camera's zoom (0.5x to 2x, automatic when players spread apart).
- Text in UTF-8 with Spanish and Portuguese accents; dialog boxes with a portrait and typing; the core option Language.
- Sound: a low pass filter and an echo on the mix, kept in save states.
- A* pathfinding.
- Package format 2: a level's effects (light, darkness, grading, bloom, waves, zoom, outlines, shadows, sound, dialogs in three languages), a portrait and a LUT picture; format 1 packages still play.
- The showcase demo (core option Demo): Mode 7, road, cave, sea and colors scenes; its hash is checked on every platform like the platformer's.
- `make bench` (milliseconds per frame of each scene) and tests for every effect, the showcase (the same twice, save states in the middle) and format 2.
- Game packages (`.glhd`, format 1): a zip with a manifest, a level and PNG pictures; read with the core's own zip, DEFLATE, PNG, JSON and SHA-256 code (no outside libraries, every read bounds checked). The core takes the package in memory or from its path. Packages of a newer format are refused with a clear message; unknown keys are ignored; every picture is optional (a missing one keeps the built-in art).
- `tools/glhd`: `export-demo`, `pack` and `check`; `tools/hdrun --content`.
- Tests: the demo as a deflate package plays the very same game as the built-in one; every cut or damaged copy of it fails cleanly or loads the same game; JSON and SHA-256 checks.
- The libretro core `golink_hd_libretro` in C99: a 640 × 360 XRGB8888 screen at 60 fps, 48 kHz stereo sound, four RetroPad ports, core options (v2 with a fallback to variables) and input descriptors; it starts with no content.
- A built-in platformer demo for 1 to 4 players: running, variable jumps with coyote time and jump buffering, one-way platforms, coins, enemies to stomp, checkpoints, pits, a goal and players joining with Start; parallax hills and clouds, particles, hit stop, screen shake, a music loop and sound effects, all made in code.
- Deterministic engine: integer and fixed point math only, one random generator in the state.
- Versioned, portable save states (`GLHD` header, little endian words, checksum, the game's SHA-256); other versions, other games and damaged data are refused, loaded values are kept in safe ranges.
- `make test`: determinism, play, save state and libretro API tests with the address and undefined behaviour sanitizers; `tools/hdrun`, a headless frontend that plays a button script and saves PNG frames.
