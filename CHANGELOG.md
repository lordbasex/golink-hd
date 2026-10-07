# Changelog

## [Unreleased]

### Added
- Game packages (`.glhd`, format 1): a zip with a manifest, a level and PNG pictures; read with the core's own zip, DEFLATE, PNG, JSON and SHA-256 code (no outside libraries, every read bounds checked). The core takes the package in memory or from its path. Packages of a newer format are refused with a clear message; unknown keys are ignored.
- `tools/glhd`: `export-demo`, `pack` and `check`; `tools/hdrun --content`.
- Tests: the demo as a deflate package plays the very same game as the built-in one; every cut or damaged copy of it fails cleanly or loads the same game; JSON and SHA-256 checks.
- The libretro core `golink_hd_libretro` in C99: a 640 × 360 XRGB8888 screen at 60 fps, 48 kHz stereo sound, four RetroPad ports, core options (v2 with a fallback to variables) and input descriptors; it starts with no content.
- A built-in platformer demo for 1 to 4 players: running, variable jumps with coyote time and jump buffering, one-way platforms, coins, enemies to stomp, checkpoints, pits, a goal and players joining with Start; parallax hills and clouds, particles, hit stop, screen shake, a music loop and sound effects, all made in code.
- Deterministic engine: integer and fixed point math only, one random generator in the state.
- Versioned, portable save states (`GLHD` header, little endian words, checksum, the game's SHA-256); other versions, other games and damaged data are refused, loaded values are kept in safe ranges.
- `make test`: determinism, play, save state and libretro API tests with the address and undefined behaviour sanitizers; `tools/hdrun`, a headless frontend that plays a button script and saves PNG frames.
