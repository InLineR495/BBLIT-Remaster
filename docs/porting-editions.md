# Porting other editions and languages

Language is not an executable layout. Releases in one language can use
different binaries, and different languages can share code. Do not select
addresses from a language name, `bugs.ini`, the Windows version resource,
file size, or one short signature. Identify the actual executable.

## Supported editions and code layouts

One `ddraw.dll` supports every edition listed in `src/addr.h` (`BB_PROFILES`).
At start-up the DLL computes the CRC32 of `.text` and reads the marker qword;
an exact match selects the edition and its code layout, anything else leaves
the mod off. The sources always use code addresses of layout A (Russian 1.6);
`BB_CODE()` translates them into the host's layout through `BB_CODES`.

| Layout | Build | Editions |
| --- | --- | --- |
| A | 1.6, 26 Sep 1999 | North America (Infogrames), Russian (CITY) |
| B | 1.0, 12 Oct 1999 | Europe multi-language (Infogrames), Spanish |

Layout B was mapped by aligning normalised disassemblies of both builds (code
targets and import slots replaced by symbols, data addresses kept). Functions
are the same and shifted by inserted code; the only changed function the mod
calls (the video-menu callback) differs in register allocation only. All data
addresses the mod uses are identical: both builds reference the same 1983 data
addresses, and layout B adds one new variable. Layout B imports one more
function (`OemKeyScan`), so its import slots move. The Russian 1.6 is the North
American 1.6 with the startup CD check removed by CITY and one resource string
translated; the North American 1.6 and the European 1.0 keep the check, and the
DLL removes it in memory (`BB_CD_CHECK_*` in `addr.h`).

## Adding an edition

1. Record SHA-256, PE entry point, section layout and `.text` CRC using
   `python -I tools/verify_exe.py <bugs.exe>`. Keep game assets and the EXE
   local; a contribution needs fingerprints and address evidence, not game files.
2. Confirm the unmodified game starts with `/opengl /win /b0 /x800 /y600` and
   working directory `BIN`. Distinguish asset/path errors and Windows
   proxy-loader errors from rejected executable fingerprints.
3. If its code matches an existing layout (the verifier reports all sites
   matching for that layout), add a `BB_PROFILES` row with its CRC, marker and
   layout, then test.
4. Otherwise map a new layout: add a column to `BB_CODES` and `BB_IATS` for
   every address there. Use callers, nearby instructions and global accesses to
   establish identity; short prologues match many functions. Verify global
   addresses and structure offsets too: identical data in layouts A and B is
   not a rule for other builds.
5. `tools/check_addr.py` (run by `build.sh`) rejects code addresses in the
   sources that bypass `BB_CODE()`/`BB_A()` or are missing from `BB_CODES`.
   Relative call sites are retargeted automatically. Keep per-site byte checks
   and an exact accepted fingerprint; never add a force flag.
6. Test incrementally: startup, title, menus, pause, gameplay, transitions,
   save/load, controls, interpolation, draw distance and renderer fallback.
   Look for rejected writer slots, missing hooks, GL errors and crashes in the
   log, and state whether Windows and/or Proton were tested.

## Language-independent UI placement

Screen position and string length do not establish that text is a HUD counter.
The save menu demonstrates the problem: short rows near the top-left can be
misclassified and shifted independently of their centred panel. The correction
keeps frontend/loading/language-selection and pause menus centred, requires an
icon/bar in an edge-anchored gameplay group, and treats text-only groups as
centred UI. No translated labels or fixed character counts are used.

The HUD cache also includes menu/gameplay state so a mode change invalidates
cached placement even when the snapshot pair is reused. `tools/test_hud_policy.c`
covers menu level IDs, pause values, short/long standalone labels and actual
HUD groups. These are policy regression tests; translated assets, unusual
in-game dialogs and complete playthroughs still need visual testing.

Run the dependency-free policy test with a native C compiler:

```sh
gcc -std=c11 -Wall -Wextra -Werror tools/test_hud_policy.c -o build/test_hud_policy
./build/test_hud_policy
```

On 32-bit Windows (or using a 32-bit compiler on 64-bit Windows), the proxy
smoke test loads the real system `d3dim.dll`, checks its five proxy imports,
and exercises lazy/cached acquire/release forwarding from an unsupported host:

```sh
i686-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -municode tools/test_ddraw_proxy.c -o build/test_ddraw_proxy.exe
build/test_ddraw_proxy.exe "C:/absolute/path/to/build/ddraw.dll"
```

This test passed on the contributor's Windows system. It
does not exercise rendering, internal lock/unlock signatures or gameplay.

## Startup logo backgrounds

The startup logo loop runs before the first recorded renderer snapshot. Fill
its bars just before the engine's intercepted `wglSwapBuffers` call. Do not
infer the drawing path from the presence of DirectDraw or GDI imports.
The logo image can have a narrow black raster border: sampling at one pixel
from the viewport edge therefore returns black even on a white-background logo.
The implementation samples four points 5% inside the viewport and extends a
colour only if the samples agree within a small tolerance. The thin dark
raster border is then measured on each edge (its width scales with the output
resolution, about 3 px at 1200 lines) and painted over together with the bars,
also where the image touches the window edge. The viewport, aspect
and central artwork remain unchanged. It restores GL colour/scissor/enable
and client pixel-pack state, and stops applying once snapshots are published.

The hidden-context Windows integration test draws a simulated image with that
black raster border and verifies matching bars and seam repair for white, blue
and black backgrounds, preserved central pixels and restored GL state:

```sh
i686-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror tools/test_splash_gl.c src/splash.c -o build/test_splash_gl.exe -lopengl32 -lgdi32
build/test_splash_gl.exe
```

The dependency-free `tools/test_splash_policy.c` also checks nonuniform corners
are rejected; it runs alongside the HUD policy test in CI. Four-point sampling
is a heuristic for solid-background logos, not reconstruction of arbitrary art.

## Contribution evidence

For each proposed profile, include the executable fingerprint, address-map
provenance, verifier results, compiler/platform, render resolution and observed
behavior. Clearly separate static verification, automated tests, visual checks
and untested paths. Avoid claiming support for all languages based on one port.
