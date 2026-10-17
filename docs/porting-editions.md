# Porting other editions and languages

Language is not an executable layout. Releases in one language can use
different binaries, and different languages can share code. Do not select
addresses from a language name, `bugs.ini`, the Windows version resource,
file size, or one short signature. Identify the actual executable.

## Reproducible workflow

1. Record SHA-256, PE entry point, section layout and `.text` CRC using
   `python -I tools/verify_exe.py <bugs.exe>`. Keep game assets and the EXE
   local; a contribution needs fingerprints and address evidence, not game files.
2. Preserve the known-working build and all saves. Confirm the unmodified game
   starts with `/opengl /win /b0 /x800 /y600` and working directory `BIN`.
   Distinguish asset/path errors and Windows proxy-loader errors from rejected
   executable fingerprints. The engine's 800x600 internal viewport is separate
   from the remaster's monitor-sized output framebuffer.
3. Compare every `BB_SITES` location with the candidate's disassembly. Short
   prologues often match many functions: use callers, nearby instructions and
   global accesses to establish identity. Do not choose the first byte match.
4. Resolve all `BB_IATS` entries by DLL and symbol, then record actual slots.
   A uniform delta is insufficient even for the Spanish example.
5. Audit code addresses outside `addr.h`: startup resume, writer dispatch
   expectations, calls, return-address switches, generated-stub branch targets
   and video-menu callbacks. Verify global addresses and structure offsets too;
   the Spanish example retaining many globals is not a rule for other editions.
6. Recompute relative call operands from their verified site and target. Preserve
   complete instruction boundaries and calling conventions. Retain per-site
   byte checks and an exact accepted code fingerprint; never add a force flag.
7. Build a separate profile, run the verifier on its generated header, and test
   incrementally: startup, title, menus, pause, gameplay, transitions, save/load,
   controls, interpolation, draw distance and renderer fallback. Look for rejected
   writer slots, missing hooks, GL errors and crashes in the log. Validate on
   Windows and/or Proton explicitly, and state which was tested.

The Spanish helper demonstrates a conservative build-time mapping. It refuses
unknown file hashes and newly introduced unaudited code literals. It is not a
runtime signature scanner or a universal language detector. Profiles should
only share mappings after their code/data equivalence has been established.

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
build/test_ddraw_proxy.exe "C:/absolute/path/to/build/spanish/ddraw.dll"
```

This test passed on the local Windows system for the experimental DLL. It
does not exercise rendering, internal lock/unlock signatures or gameplay.

## Startup logo backgrounds

The startup logo loop runs before the first recorded renderer snapshot. Fill
its bars just before the engine's intercepted `wglSwapBuffers` call. Do not
infer the drawing path from the presence of DirectDraw or GDI imports.
The logo image can have a narrow black raster border: sampling at one pixel
from the viewport edge therefore returns black even on a white-background logo.
The implementation samples four points 5% inside the viewport and extends a
colour only if the samples agree within a small tolerance. A two-pixel overlap
at padded edges covers the residual black raster seam. The viewport, aspect
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
