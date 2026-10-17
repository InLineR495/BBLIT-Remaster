First public release: Bugs Bunny: Lost in Time on a modern renderer.

**What's in it**
- Any refresh rate (60/120/144 Hz) at the correct game speed: logic stays at its native 30 ticks, the frames in between are interpolated.
- Hor+ widescreen without stretching; HUD anchored to the screen edges.
- 2× draw distance (4× for characters) with smooth fade-in. Fog to the sky colour so the bigger world doesn't end in a hard edge; ground haze in "Wabbit on the run!" and "Magic Hare Blower".
- PS1-like brightness (the PC port rendered the world about half as bright).
- 4× MSAA, correct PS1 transparency, no cracks in cliffs and no holes when swinging the camera.
- Native XInput: analogue walk/run, camera on the right stick, jump buffer, Start = pause.
- Fixed: Bugs no longer sinks into pushable stones; the time machine no longer sticks through the title logo; hub holes no longer flicker; horizon silhouettes are dark as on the PS1.
- Hybrid CPUs: the main thread runs on the fast cores (Legion Go 2 at 10 W: 119–120 fps in the heaviest scenes).

**Install.** Extract the archive into the game folder (`ddraw.dll` and `remaster.ini` go into `BIN`) and run `BIN\bugs.exe /opengl /win /b0 /x800 /y600`. Under Linux/Proton, prepend `WINEDLLOVERRIDES="ddraw=n,b" PROTON_NO_NTSYNC=1 PROTON_NO_FSYNC=1 PROTON_NO_ESYNC=1 %command%`. Full instructions are in the README.

**Beta.** Tested on Linux (Proton). Windows is untested, so reports in the issues with `BIN\remaster.log` are very welcome. Requires an OpenGL 4.6 GPU and an unpatched `bugs.exe` 1.6 (772 096 bytes, SHA-256 `7342f72443aad4f6efb366449f123dbe2baf7d45cbd1f3e8512aa70506512139`).
