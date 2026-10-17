# BBLIT Remaster

**English** · [Русский](README.ru.md)

**Bugs Bunny: Lost in Time (PC, 1999) on a modern renderer.** The game itself is untouched: same logic, levels, physics and save files. A new renderer draws it, smooth at any refresh rate, in widescreen, with a longer draw distance and modern gamepad controls.

The mod is a single `ddraw.dll` plus a `remaster.ini` settings file. No game files are modified: delete `ddraw.dll` and the game is back to stock.

> **Version 1.0-beta.** Tested on Linux (Steam + Proton, Legion Go 2 / Bazzite). The levels of the first era were tested thoroughly, the rest were spot-checked. Windows builds should work but haven't been tested on real hardware yet. If something goes wrong, please open an issue and attach `BIN\remaster.log`.

## Before / after

"Wabbit on the run!", same spot and camera. Top: the original (4:3, stock draw distance). Bottom: BBLIT Remaster (widescreen, 2× draw distance, ground haze).

![Original](docs/l01a-original.jpg)
![BBLIT Remaster](docs/l01a-remaster.jpg)

## What you get

**Smooth at any refresh rate.** The original is hard-locked to 30 fps: position, camera and animation advance once per frame, so raising the cap just speeds the game up. The mod keeps the game logic at its native 30 ticks per second and draws in-between frames. Character, camera and animation movement is interpolated, so 60, 90, 120 and 144 Hz all play at the correct speed. By default the frame rate follows your display's refresh rate.

**Widescreen without stretching (Hor+).** 16:9 and 16:10 show more of the world at the sides with correct proportions. HUD counters move to the screen edges, and menus stay centred.

**2× draw distance.** The world is visible twice as far, and characters and items four times as far as in the original. Distant objects fade in smoothly instead of popping in.

**Fog, because the world got bigger.** With the longer draw distance, the far edge of the world would end in a hard cut-off. So distant geometry blends smoothly into each level's sky colour, and the edge doesn't stand out.

"Wabbit on the run!" and "Magic Hare Blower" also get ground haze. It hides where the distant cliffs end; without it, for example, the dinosaur's neck hangs in mid-air. Other levels have no haze, because there it only darkened the horizon.

**Brightness as on the PS1.** The PC port divides vertex colours by 255, while the PS1 treats 0x80 as neutral, so the PC world was about half as bright as the PS1 one. The mod restores PS1-like brightness, slightly toned down (×1.8 instead of ×2) to avoid overexposure. This is adjustable with `ps1_light`.

**Clean image.**
- 4× MSAA.
- PS1 transparency (water, glass, effects) uses the proper blending modes instead of the PC port's alpha fakes, so it no longer flickers.
- The cracks where you could see the sky through bends in cliffs are closed.
- No holes or torn edges at the screen border when you swing the camera.
- Texels that are opaque on the PS1 stay opaque inside semi-transparent polygons, so the ship and palm-island silhouettes on the horizon are dark, as on the PS1.

**Modern gamepad controls (XInput).**
- Analogue movement: tilt the stick slightly to walk, fully to run.
- Camera on the right stick. LT/RT turn the camera towards the pressed trigger.
- Jump buffer: pressing jump just before landing still counts.
- Start opens and closes the pause menu. Back pauses and goes back in menus. B goes back in the pause menu.

**Fixes.**
- Bugs no longer sinks halfway into the stones he pushes. This bug dates back to the PS1 version: the stone's collision shape is offset from its model.
- The time machine's carrot no longer sticks through the logo on the title screen.
- The holes in the hub (LS01) no longer flicker or vanish: they are shallow pits under a solid ground mesh, now drawn over the ground as on the PS1.

**Handheld PCs.** On CPUs that have both fast cores and efficiency cores (such as the Z2 Extreme in the Legion Go 2), the game's main thread is pinned to the fast cores. On the Legion Go 2 at a 10 W TDP, this raised the heaviest scenes from 111–116 fps to 119–120 fps.

## Requirements

- **The PC version of Bugs Bunny: Lost in Time with an unpatched `bugs.exe` v1.6.** Tested with the Russian edition (CITY, translated by "Дядюшка Рисёч"). The supported `bugs.exe`:

  | | |
  |---|---|
  | Version | 1.6 |
  | Size | 772 096 bytes |
  | SHA-256 | `7342f72443aad4f6efb366449f123dbe2baf7d45cbd1f3e8512aa70506512139` |
  | MD5 | `6672ce79f18cf5110e00059d5f031a35` |
  | CRC32 of `.text` (checked by the mod) | `ecffb7c9` |

  To check your copy: on Windows run `certutil -hashfile bugs.exe SHA256` in the `BIN` folder, on Linux run `sha256sum bugs.exe`. If your `bugs.exe` is different, the mod switches itself off and the game runs as usual. `BIN\remaster.log` will then contain the line "не эталонный bugs.exe" ("not the reference bugs.exe"). Other releases with the same code (same `.text` CRC32) will also work. If yours doesn't, open an issue with its hash.
- A GPU with **OpenGL 4.6**: Mesa (AMD, Intel) on Linux, or a current AMD, NVIDIA or Intel driver on Windows.
- An XInput gamepad: Xbox-compatible, Steam Deck, Legion Go, or a DualSense through Steam Input. The keyboard works as in the original.

## Installation

Download `BBLIT-Remaster-1.0-beta.zip` from [Releases](../../releases) and extract it into the game folder. `ddraw.dll` and `remaster.ini` must end up in the `BIN` folder, next to `bugs.exe`.

Run **`BIN\bugs.exe` directly**, not the `BugsBunny.exe` launcher, with these arguments: `/opengl /win /b0 /x800 /y600`.
- `/opengl` selects the engine's OpenGL mode; the mod only works in it.
- `/win` starts in a window; the mod turns it into borderless fullscreen.
- `/b0` goes straight to the main menu and skips the intro video.
- `/x800 /y600` sets the engine's internal viewport; the mod still renders at your display resolution.

### Linux (Steam + Proton, Steam Deck, Bazzite)

1. In Steam, choose "Add a Game" → "Add a Non-Steam Game…" and select `BIN/bugs.exe` of your installed game.
2. In the shortcut's Properties → Compatibility, choose Proton Experimental (the tested version).
3. Set the launch options:
   ```
   WINEDLLOVERRIDES="ddraw=n,b" PROTON_NO_NTSYNC=1 PROTON_NO_FSYNC=1 PROTON_NO_ESYNC=1 %command% /opengl /win /b0 /x800 /y600
   ```
   - `WINEDLLOVERRIDES="ddraw=n,b"` makes Wine load the mod's `ddraw.dll` instead of its built-in one.
   - `PROTON_NO_NTSYNC/FSYNC/ESYNC=1` are **required**: with Wine's newer synchronisation primitives the game freezes, and so does the unmodded original.
4. Use the "Gamepad" Steam Input layout. Don't bind buttons to F1–F12.
5. In Game Mode you can limit the refresh rate in the performance menu, and the game adapts to it.

### Windows

> Not yet tested on real Windows. If you try it, please report how it went in the issues.

1. Copy `ddraw.dll` and `remaster.ini` into the game's `BIN` folder.
2. Create a shortcut to `BIN\bugs.exe` and add the arguments after the quotes in the shortcut's "Target" field:
   ```
   "C:\...\Bugs Bunny\BIN\bugs.exe" /opengl /win /b0 /x800 /y600
   ```
3. Always start the game through this shortcut. The launcher doesn't pass `/opengl /win`.
4. If your driver control panel forces vertical sync off, set it back to "application-controlled". The mod paces its frames to vsync.

### Checking that it works

After launch, `remaster.log` appears in `BIN`, and its first lines contain `BBLIT Remaster 1.0-beta`. The picture fills the whole wide screen, and the right stick turns the camera.

### Uninstalling

Delete `ddraw.dll` from `BIN` (and `remaster.ini` / `remaster.log` if you like). On Linux, also remove the launch options.

## Controls (gamepad)

| Button | Action |
|---|---|
| Left stick | Move: slight tilt to walk, full tilt to run |
| Right stick | Turn the camera |
| LT / RT | Turn the camera (L2/R2 on PS1) |
| A / B / X / Y | ✕ / ○ / □ / △ of the PS1 version |
| LB / RB | L1 / R1 |
| Start | Open/close pause; "Press Enter" on the main menu |
| Back | Pause; "back" in menus |
| B in pause | Back |

## Settings

All settings live in `BIN\remaster.ini` and are commented there. The `[live]` section is re-read once per second, so you can edit it while playing. The most useful keys:

| Key | Section | Default | What it does |
|---|---|---|---|
| `fps` | `[remaster]` | 0 | 0 = display refresh rate, -1 = nearest multiple of 30 below it, a number = your own cap |
| `msaa` | `[remaster]` | 4 | Anti-aliasing: 0, 2, 4, 8 |
| `cam_speed`, `cam_invert` | `[remaster]` | 52, 0 | Right-stick camera speed and inversion |
| `draw_dist` | `[live]` | 200 | World draw distance, % of the original (100 = stock) |
| `actor_dist` | `[live]` | 400 | Character and item draw distance, % |
| `haze` | `[live]`, `[levelN]` | 0 / 100 | Ground haze strength, %; per-level overrides go in `[levelN]` sections |
| `ps1_light` | `[live]` | 18 | World brightness ×10: 20 = exactly as on the PS1, 10 = like the PC port, 0 = PC port path |
| `xinput` | `[remaster]` | 1 | 0 = original gamepad handling |
| `big_cores` | `[remaster]` | 1 | Pin the main thread to the fast cores on hybrid CPUs |

## Known beta limitations

- Textures are the original ones, with no replacement or upscaling.
- At a low TDP, the heaviest scenes occasionally (about once per second) repeat one frame.
- Windows is untested.
- Only an unpatched `bugs.exe` 1.6 is supported.

## Building from source

You need the MinGW-w64 cross-compiler for i686:

```
# Ubuntu/Debian: sudo apt install gcc-mingw-w64-i686
# macOS:         brew install mingw-w64
./build.sh          # → build/ddraw.dll
```

GitHub Actions builds every push. A `v*` tag publishes a release with the ready-to-use archive.

How it works:
- `ddraw.dll` is a proxy. It forwards DirectDraw to the system DLL and installs hooks into `bugs.exe` 1.6, after checking the version and the CRC32 of the code.
- The engine's OpenGL calls are recorded into snapshots, one per logic tick.
- The mod's own renderer (OpenGL 4.6, shaders) draws the frames between snapshots with interpolation.
- XInput input is fed into the game's own gamepad parser.

Source comments are in Russian. Reverse engineering was done with Ghidra and [REA](https://github.com/morluto/rea).

## License

MIT, see [LICENSE](LICENSE). Bugs Bunny and Looney Tunes are trademarks of Warner Bros. This mod contains no game files; you need your own copy of the game.
