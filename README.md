# sonicexe-runner

A small, purpose-built runner for **Sonic.EXE** (GameMaker: Studio 1.x, bytecode v13) so it can run natively
on ARM Linux / ArkOS handhelds. It reads the game's own `data.win` directly (no x86 code, no Wine) and
re-implements the ~43 GML functions the game actually uses.

## Status (please read)

| Part | State |
|---|---|
| `data.win` loader, bytecode VM, events, collision, views, tiles, backgrounds, text | **Tested** headless on x86 (software renderer): boots → title → data select → Hill stage; Tails runs/jumps; all 34 rooms run clean under ASan/UBSan |
| SDL2 video/input/audio backend (`main_sdl.c`) | **Compiles cleanly, not yet run** (no SDL2 or ARM toolchain where this was developed). Expect to fix small things on first run |
| Audio (SDL2_mixer) | Not run yet. All 30 embedded sounds are 16-bit stereo PCM; 14 external `.ogg` files verified present |
| Texture memory | Lazy-loaded + LRU-evicted pages (`texbudget`). Rooms peak at ~64-144 MB in tests; needs tuning on real hardware |

Known approximations: `action_effect` (particle effects) is a no-op; collision ignores sprite rotation;
a second overlapping music stream is approximated (see `a_music` in `main_sdl.c`).

## 1. Get the game files
You need `SONIC.EXE` (the original installer). The runner reads the unpacked files, and `extract_sonic`
unpacks it anywhere (PC or device):

    make extract_sonic && ./extract_sonic SONIC.EXE gamefiles/

(or just drop `SONIC.EXE` into the game folder on the device). On first launch the launcher unpacks it, showing progress
on screen, then **deletes `SONIC.EXE` and the Windows-only files** to save space (only if extraction succeeded; a damaged
SONIC.EXE is detected before anything is written and is kept). Use `--game-only` to skip the Windows-only files by hand.

## 2. Build
**On the device / any ARM64 Linux** (needs `build-essential libsdl2-dev libsdl2-mixer-dev zlib1g-dev`):

    make

**Cross-build on a PC** with docker + qemu-user-static (Ubuntu 20.04 arm64 so glibc matches ArkOS):

    ./build-arm64-docker.sh

If SDL2_mixer is unavailable: `make sonicexe-noaudio`.

## 3. Install on ArkOS
Copy `ports/Sonic.EXE.sh` to `/roms/ports/` (or `/roms2/ports/`) and make a folder `sonicexe/` beside it containing:
`sonicexe`, `extract_sonic`, `sonicexe.cfg`, and either `SONIC.EXE` or the unpacked game files
(`data.win` + the `.ogg` files). Refresh the gamelist; "Sonic.EXE" appears under **Ports**.

Log output goes to `sonicexe/log.txt`.

## Controls
D-pad **and** left stick = arrows, button 0 = Space (jump), button 1 = X (interact), **Start** = Enter (confirms Data Select),
**Select** = Esc, L1/R1 = A/S. **Hold Start+Select (or L2+R2) for about 0.7 s to quit** (log.txt shows `[input] quit combo ...` lines when it is detected). Keyboard works too (F10 quits).
Default raw layout is the Odroid-GO Super (d-pad 8-11, Select 12, Start 13); the stick has a hysteresis dead zone.
The in-game greeting uses `name=` from `sonicexe.cfg` (default `player`); `wrap_text=1` (default) wraps long names onto several lines.
Remap in `sonicexe.cfg`. For controllers SDL doesn't recognise, run with `JOYDEBUG=1` to print raw button numbers.

## If the controller does nothing
* The **title screen is a non-interactive intro** (about 10 s, then it moves on by itself). The **Data Select** screen
  needs **Start** (Enter). In-game: D-pad/stick, **A** = jump.
* The log shows `[input] ...` lines for every button SDL receives. No lines at all = SDL isn't delivering events
  (the launcher sets `SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1` for this). Lines present but wrong action = fix the mapping:
  set `rawpad=1` in `sonicexe.cfg` and use the `joy_*` entries with the numbers from the log.

## Loading
Texture atlases are large (the biggest decodes to 64 MB). To hide the wait: the title screen's textures are decoded on a
background thread while SDL/GL start up, the next room's textures are prefetched while you play, and **music/sounds that a
room starts are held until that room's first frame is on screen**, so picture and music always appear together.

## Tuning for handhelds
* Slow / stutters: set `vsync=0`, lower `texbudget` (e.g. 96), set `fps=1` and watch `log.txt`.
* Texture decode causes a short hitch the first time each atlas page is used (the 4096x4096 pages are 64 MB each).
  The next planned step is a one-time converter that downsizes/compresses pages (e.g. RGBA4444) to halve memory.
* 480x320 screens: `linear=1` looks better than nearest-neighbour at 0.75x.

## Source layout
`src/data.c` data.win parser + PNG decoder · `src/engine.c` VM, events, collision, drawing, GML functions ·
`src/main_sdl.c` SDL2 backend · `src/main_test.c` headless harness · `src/extract_sonic.c` installer unpacker.

Headless test: `make test_runner && ./test_runner data.win -f 1300 -k "600:13:1,604:13:0,700:32:1,704:32:0" -d 1200 -o /tmp/f`
(`-k frame:vk:down|up`, `-d` dump PPM frames, `-r N` start in room N, `-v` verbose).

## Notes
This only runs data you already have; it contains no game assets. Sonic.EXE is a fan game by MY5TCrimson;
check with the author before redistributing the game files together with the runner.
