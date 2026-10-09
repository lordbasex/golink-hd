# Changelog

## [Unreleased]

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
