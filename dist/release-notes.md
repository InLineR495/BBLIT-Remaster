First public release: Bugs Bunny: Lost in Time on a modern renderer.

**What's in it**
- Any refresh rate (60/120/144 Hz) at the correct game speed: logic stays at its native 30 ticks, and the frames in between are drawn from the game's own geometry with movement, camera and animation interpolated. This is not frame generation: no images are guessed, physics and the rest of the game logic are untouched.
- Hor+ widescreen without stretching; HUD anchored to the screen edges.
- 2× draw distance (4× for characters) with smooth fade-in. Fog to the sky colour so the bigger world doesn't end in a hard edge; ground haze in "Wabbit on the run!" and "Magic Hare Blower".
- PS1-like brightness (the PC port rendered the world about half as bright).
- 4× MSAA, correct PS1 transparency, no cracks in cliffs and no holes when swinging the camera.
- Native XInput: analogue walk/run, camera on the right stick, jump buffer, Start = pause.
- Fixed: Bugs no longer sinks into pushable stones; the time machine no longer sticks through the title logo; hub holes no longer flicker; horizon silhouettes are dark as on the PS1; a leaf in front of the camera no longer cuts holes in what is behind it; no seams along leaf edges and inside hub holes; level-entrance vortices spin smoothly.
- Hybrid CPUs (Linux/Proton): the main thread runs on the fast cores (Legion Go 2 at 10 W: 119–120 fps in the heaviest scenes).
- No disc needed: the CD check of the European and North American releases is switched off in memory (the Russian release never had it). The game files are not modified.
- HUD counters stay within a 16:9 frame on ultrawide screens (`hud_aspect`); startup logos fill the whole screen.
- "Hey What's Up, Dock?": diving into the burrow by the floating barrels no longer throws Bugs back; no palm tree "stuck" on the ship, the floating platform is drawn as ground, Bugs' shadow falls on the barrel, and the helm and the drawbridge no longer pop in and out.

**Install.** Extract the archive into the game folder (`ddraw.dll` and `remaster.ini` go into `BIN`) and run `BIN\bugs.exe /opengl /win /b0 /x800 /y600`. Under Linux/Proton, prepend `WINEDLLOVERRIDES="ddraw=n,b" PROTON_NO_NTSYNC=1 PROTON_NO_FSYNC=1 PROTON_NO_ESYNC=1 %command%`. Full instructions are in the README.

**Editions.** Europe 1.0, North America 1.6, Russian 1.6 and Spanish 1.0 `bugs.exe` (772 096 bytes each; the SHA-256 table is in the README). The mod recognises the file at start-up and stays off with any other, including patched copies.

**Beta.** Tested on Linux (Proton) with the Russian, European and North American editions; the Spanish edition has only been tried on Windows by a contributor. Windows is otherwise untested, so reports in the issues with `BIN\remaster.log` are very welcome. Requires an OpenGL 4.6 GPU (older drivers down to 3.3 should also work).
