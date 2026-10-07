# go-link HD

go-link HD is a 2D game engine packaged as a [libretro](https://www.libretro.com/) core: a "virtual board" with modern console-class pictures (a 640 × 360 pixel art screen, 32-bit color, alpha, many layers and sprites) for games made with [Willy Maker](https://maker.go-link.org). It is written in portable C99 with no dependencies, so it runs in any libretro frontend (RetroArch, [go-link](https://github.com/lordbasex/go-link)'s device, and others) on every platform the libretro buildbot builds for.

This first version plays a **built-in demo**: a platformer for 1 to 4 players. Start the core with no content. Game packages (`.glhd`, exported by Willy Maker) come in a later version.

## Controls (RetroPad)

| Button | Action |
|---|---|
| D-pad | Move |
| B or A | Jump (hold for a higher jump) |
| Y or X | Run |
| Down + Jump | Drop through a wooden platform |
| Start | Join the game (players 2 to 4) or pause |

## Core options

| Key | Values | Default |
|---|---|---|
| `golink_hd_music` | `enabled`, `disabled` | `enabled` |

## Building

```bash
make                 # this computer: golink_hd_libretro.so / .dylib / .dll
make platform=win    # another platform (unix, osx, win, emscripten)
make test            # the engine's tests, with the address and undefined behaviour sanitizers
make tools           # tools/hdrun, a headless frontend
```

`tools/hdrun CORE --frames N --script tools/runs/walk.txt --shot 60,300 --out DIR` plays a button script on a built core, saves the chosen frames as PNG and prints a hash of all frames and sound, so two builds can be compared. `tools/runs/walk.expected` holds the hash every platform must give (checked in CI; macOS Clang, Linux GCC on x86_64 and arm64 give the same). A change that alters the picture or the sound on purpose updates it.

## Design rules

- **Deterministic.** Game logic uses only integers and 16.16 fixed point, never `float`, and one seeded random generator kept in the state. The same inputs give the same frames and the same sound on every CPU and in WebAssembly.
- **One state struct.** The whole game is `hd_state` (`src/hd.h`), made only of 32-bit integers. Art, level and sounds are built once at start and only read afterwards.
- **Save states are portable and versioned.** A save state is a header (`GLHD`, the layout version, the size, a checksum) and the state's words in little endian. A save state of another version or a damaged one is refused, never misread, and the values loaded are brought back into safe ranges. `HD_STATE_VERSION` goes up whenever `hd_state` changes.
- **Software rendering** into a 640 × 360 XRGB8888 frame buffer, so every build draws the same pixels. The frontend scales it (×3 is 1080p, ×6 is 4K).
- **Sound** at 48 kHz stereo, 800 samples per frame, from a 32 channel mixer whose channels live in the state.
- **Budgets:** under 4 ms per frame on the host (about 1.2 ms on a 2019 Intel Mac today).

## Layout

| Path | What |
|---|---|
| `src/libretro.c` | The libretro API: input in, one 60 Hz step, a frame and its sound out |
| `src/game.c` | The rules: movement, jumps, enemies, coins, checkpoints, camera, players joining |
| `src/draw.c` | The software renderer: parallax backdrop, tiles, sprites with flip and alpha, particles, text |
| `src/audio.c` | Sound effects made in code, the music sequencer and the mixer |
| `src/art.c`, `src/level.c` | The demo's pixel art and level, made in code |
| `src/save.c` | Save states |
| `libretro/libretro.h` | The libretro API header (MIT, the RetroArch team) |
| `dist/info/golink_hd_libretro.info` | The core's info file for libretro's core-info repository |

## License

MIT, see [LICENSE](LICENSE). `libretro/libretro.h` keeps its own MIT notice.
