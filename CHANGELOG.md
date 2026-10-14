# Changelog

## [Unreleased]

### Added
- A model types for real (`actor3d_place_model`): it sits forward on the seat, the left hand on the keyboard and the right on the mouse, palms down (a twist along the forearm).
- TELETRABAJO's desk made by artists when the package brings it: CreativeTrio's desk with its drawers, monitors (the code still glows on them, set back so the keyboard shows), a keyboard with its keys and a mouse on its pad at the front edge, a computer under the desk with its light on, Quaternius's office chair and desk lamp (all CC0, from Poly Pizza); a package holds up to 32 models; the credits' heading is "3D models".
- Furniture made by artists in TELETRABAJO's flat when the package brings it (Quaternius's CC0 house interior: the sofa, the armchair, the chairs, the bed and nightstands, the toilet, the washing machine and dryer, the fridge), fit into the built pieces' places; the flat is built again when a package's models change; `model3d_add`, `model3d_bounds`, `r3d_solid_box`; `hdm.py` reads OBJ furniture; `tools/remotebots --content` checks every place of a package's flat on foot.
- 3D models made by artists: `src/3d/model3d.h` reads `.hdm` files (glTF turned into integers by `examples/remote/models/hdm.py`: colored triangles, bones, clips), plays their clips with integer quaternions and skins them on the processor; a package's `"models"`; people made by artists take the same poses (`actor3d_place_model`); `r3d_triangle`, `r3d_tri_pose`, `r3d_truncate`. TELETRABAJO's worker and flatmate are Quaternius's CC0 characters when the package brings them.
- TELETRABAJO: CÁMARA APAGADA, more of the boss: production down (a hotfix deployed at the desk, with its progress), "can you work this weekend?" (answered at the desk: yes calms the boss and sells the weekend, no costs a little suspicion, no answer counts as yes), a video call with the camera on (the boss sees the cat, the flatmate, the worker asleep), a window that shows every call, the boss's chatter between events; the guitar held with one hand on the neck and the other strumming (`actor3d_pose.strum`).
- TELETRABAJO: CÁMARA APAGADA, phase 6 (polish): balanced with bots that play like people (`make remotebots`, `tools/remotebots.c`): the boss's suspicion moves now (working calms 1 every 3 minutes, answering half as much, being away weighs 2 a minute); things in the hands (a mug, a pan, a controller, a guitar, a watering can, the parcel, headphones); water in the shower; every text checked to fit in its three languages; `docs/3d.md` guide for the next 3D game, steps 12 to 18.
- People can hold things: `actor3d_pose.prop` (between both hands or in the right one, turned by `prop_pitch` and `prop_roll`); `room3d_prop`.
- TELETRABAJO: CÁMARA APAGADA, phase 5 (the sound), recorded and not synthesized, all in the public domain (CC0):
  - chill-out and lo-fi music for the title, the day, the boss's calls (the day's goes on where it was after a call) and the results;
  - 43 effects heard from what changes in the game: the alarm, the computer ringing, messages, the bell, the drill and the headphones, the washing machine, the cat (meows, purring, walking on the keys), steps, typing, snoring, every activity's sound (coffee, frying, the TV, the console, the guitar, birds, the shower...), a heartbeat when the boss is close to firing, the menus and the end's jingles;
  - sounds placed by where they are (louder near the worker, left or right as the camera sees it), loops that fade in and out;
  - a language line on the title (English, Español, Português) and the credits rolling (from the title's menu and after a day won), in the three languages;
  - the package is made by `examples/remote/build.py` from `examples/remote/audio.json` (every recording's source, author and license; `CREDITS.md`).
- Packages: `"game": "remote"` plays a game coded in the engine instead of levels, and `"audio"` brings effects and music tracks by name for it (`hd_effect`, `hd_track`); `hd_play_at` (volume, place, speed) and `hd_loop` (a sound kept on its own channel while it lasts); `music_track` in the state (it was `music_boss`: 0 and 1 mean the same); `"credits"` can be one list for each language.
- The font has `<` and `>`.
- **3D** ([docs/3d.md](docs/3d.md)): low-poly worlds drawn in software with integers only, so every computer draws the same pixels. Boxes, cylinders, balls and quads with textures painted in code (parquet, tiles, fabric, rugs, wood, marble, the city's facades, posters); light baked once on a grid of 5 cm cubes (the sun's soft shadows, the sky's light that misses corners, lamps); a filmic curve, softened edges, decals (soft shadows under what moves); drawing on up to 4 cores with runs out of view skipped and the nearest first. Big faces get light maps (a texel every 5 cm: sharp sun shadows and corners, softened once); textures seen bigger than their texels are read between four of them; see-through glass; people (`actor3d.h`): 11 parts posed every frame (walking, sitting, typing, waving). The demo `golinkhd_load_demo(e, 2)` (`hdrun --demo 3d`) is a lived-in flat on the fifth floor of a city block, about 30,000 triangles and two people: 4.8 ms a frame at 360p, 9.4 at 720p, 17.1 at 1080p (`make bench`; 3D games run at 720p). A test plays it twice and through a save state. `make NO_3D=1` builds without it; 2D games are drawn exactly as before (ANTÍDOTO's frames and sound checked the same).
- **TELETRABAJO: CÁMARA APAGADA** ([docs/game-remote-work.md](docs/game-remote-work.md)), the first 3D game, a remote worker's day in the demo's flat (`golinkhd_load_demo(e, 3)`, `hdrun --demo remote`). Phase 1, walking:
  - the worker walks and runs with the stick relative to the camera, turns toward where it goes, slides along walls and steps into doorways;
  - a follow camera turned with L/R or the right stick rises over walls before they come between it and the worker.
  - The game's state lives in the 2D coin bitmap's words (`HD_GAME3D`), so `hd_state` and its version do not change. It is 9.6 ms a frame at 720p, and a test plays it twice and through a save state.
- `src/3d/play3d.h`, walking in 3D worlds for any game:
  - a walk grid made from the baked solids (`play3d_grid`, `play3d_fits`);
  - moving with slides around corners and into doorways (`play3d_move`);
  - the stick relative to a camera (`play3d_stick`);
  - the follow camera (`play3d_follow`, `play3d_eye`).

  Its pieces underneath are `r3d_solid_at` and `r3d_reach` (the bake's grid for a game's rules). `src/3d/room3d.h` shares the demo's flat, light and people.
- TELETRABAJO: CÁMARA APAGADA, phase 4 (the screens):
  - the title: the house turning, the game's name, a menu, the session's record; after 10 seconds a demo where a bot plays with the game's own rules;
  - how to play in three pages; the morning (the alarm, getting up, the camera flying to the worker), skippable;
  - the pause (continue, how to play, restart the day, back to the title), with the clock stopped;
  - the end: the fired screen typed out on black, the numbers counting up, the title by score, "new record";
  - every day gets its own seed from how long the player took; `hd_remote_new_day` for tests and tools.
- TELETRABAJO: CÁMARA APAGADA, phase 3 (the day):
  - a clock from 09:00 to 18:00, the boss's suspicion and the chat status it sees (online, away, on a call, in a meeting, offline);
  - the boss's events from the day's seed (the daily meeting at 10:00, surprise calls, urgent messages, screen shares): be awake at the desk in time, then stay on the call while the boss hears the house (the washing machine, the bell, the flatmate, snoring);
  - the house's events: a delivery at the door, the cat on the keyboard (it logs the worker off if left there), the neighbor's drill (headphones on the desk), the internet down, the flatmate home early (in the way, talking);
  - a cat that wanders the house, jumps on the bed and the desk, and can be petted;
  - the phone in the HUD with the last messages and the countdown, a suspicion bar;
  - the end of the day: fired, hours missed or survived, with the bonuses, the overtime cost and a title by score; START for another day.
- `src/3d/pet3d.h`: a cat of 8 parts posed every frame (trotting, sitting, lying as a loaf, its tail moving). `play3d_route`: the shortest way through the walk grid, for what a game moves itself and for bots.
- Fixes to the house: a parcel and the umbrella stand closed the way to the front door; the pouf stood between the sofa and the armchair. A test now walks to every place the game uses at the worker's size.
- TELETRABAJO: CÁMARA APAGADA, phase 2 (doing things):
  - 13 activities around the house, each with its prompt, pose, length, energy and points, stopped early with the main button; the points are cut to a fifth after the second time;
  - work at the desk at full or half speed, asleep on the keyboard when out of energy;
  - closing lines with humor in three languages;
  - a HUD with the clock, work, energy, points, the prompt or the activity's progress, and messages;
  - the desk's screens scroll code while working (`tex3d_terminal`);
  - `actor3d` poses `hold`, `crouch` and `lie`.

  Fixes to the house: the fridge, a reading armchair, the arcade cabinet, the amplifier, a sideboard and a bookshelf were placed or turned wrong, and every dining chair faced away from its table. A test covers the rules.
- 3D quality:
  - round pieces lit smoothly: a normal at each corner of cylinders and balls, also for people;
  - `r3d_bevel`: furniture and bodies with soft 45-degree edges;
  - bloom from what gives light, in its own color, and a vignette (`r3d_view.bloom`, `vignette`), done in the same pass as the softened edges;
  - what moves is lit from a table of 128 × 128 normals.

  The house:
  - plants made of leaves;
  - door frames, open doors with panels and handles, light switches;
  - skirting boards on every wall, cut at doors;
  - curtains in soft folds;
  - posters and screens at 256 texels (a terminal with its title bar).
- The demo's flat became a 200 m² house (1600 × 1250 cm). It has a bedroom, a bathroom, an office, a game room, a laundry, a hall, a 9 × 2 m terrace, and a living room with separate dining and lounge areas. The family kitchen has:
  - an L of counters with the washing machine, dishwasher, sink, hob, oven and hood, and a coffee corner;
  - the fridge and pantry, an island with stools, and a table for four;
  - a bar to the living room;
  - the everyday things on the counters.

  The TV unit is its own piece (`tv_unit`). The demo is about 43,000 triangles, 11 ms a frame at 720p.
- The demo's flat, laid out for walking: the dining table against the north wall with three chairs; the sofa and coffee table 20 cm south; the bathroom's door at 600..690 with the sink beside the toilet and the tiles cut at the door (the sink and its tiles stood across the old door).
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
- `health` takes `invulnerable` (the frames a hit player cannot be hurt again, 90 by default), `lives` (each knockout or last fall costs one; 0 keeps them unlimited) and `continue` (the seconds a player out of lives has to press start and come back with all of them, its countdown over its own part of the screen while the others play on; out of time it leaves the game and start brings it back; with nobody left, GAME OVER and the title); lives in the HUD. Save states are version 7.
- A level with `same_art: true` is another act of the level before it: its sky, layers, textures and music (not loaded again, no intro), with its own map and boss.
- Levels may be 1792 cells wide (1024 before) and hold 224 enemies of their own (48 before); 32 more are kept for what a boss lets out.
- Tests: lives and continue, acts sharing their art.
- Four more enemies (format 3): a roller (`R`) rolls at the players and on, a hopper (`H`) leaps at them, a puffer (`U`) puffs a fan of spores up that fall on them, a splitter (`K`) splits into two smaller halves when beaten; their sprites; the puffer's spores in the state.
- A boss's `evolve`: beaten once, it evolves into its second form (its own health, hitbox, attacks and pictures) and must be beaten again; the form in play is in the state.
- `credits`: lines rolling up after the last level's ending, skipped with start.
- Tests: the four enemies, a boss that evolves, the credits.
- ANTÍDOTO: bacteria, parasites, fungi and cancer cells drawn by the image AI, a mix of them per zone; a mid-boss at the end of each zone's first act (CAPITÁN COLI, LA TENIA, HONGÓN, MOHO NEGRO, TUMORÓN, EL PRIÓN); NEUROVIRUS evolves into NEUROVIRUS OMEGA; the credits.
- `tools/hdrun --every N --audio FILE.raw`: every Nth frame as PNG and the run's sound, to make a video with ffmpeg.
- `tools/sprites.py --align shape` (and `--align-ref N`): each frame is slid over the reference frame, feet on the same line, until their bodies (the silhouette without thin limbs, lines and drops) overlap the most, so a creature whose body is not one strongly colored blob stays still from frame to frame. ANTÍDOTO 0.1.1 cuts every enemy, mid-boss, boss and minion this way: their bodies jumped sideways between frames (the old way followed the brightest color, a tongue, an eye, a red nose or a scarf; GRIPÓN moved up to 270 pixels at 720p), and the bacterium's roll no longer shows its uncurled frame once a turn.

### Added
- The API test games (`examples/`, written by `examples/make-api-test.py`): a 16:9 game that uses every picture, letter and effect of the package format with a dialog at each stop, and 4:3 and 9:16 ones; CI packs, checks and plays them (`tools/runs/api.txt`, `api.expected`).
- `tools/hdrun --check` (the game's info, a state saved halfway and played again, a restart played again) and `--music off`.

### Fixed
- A small x inside a word ("next", "conexión") was drawn as the times sign of "x3"; it is the letter now.
- A save state taken on a level's intro loaded as a stage clear (its phase was cut down).
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
