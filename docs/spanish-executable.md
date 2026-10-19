# Executable from a Spanish installation

> **Status:** supported by the default `ddraw.dll` as profile `ES10` (code
> layout B, shared with the European multi-language 1.0). The separately built
> profile described below was replaced by runtime edition detection; see
> [porting editions](porting-editions.md). The rest of this report is kept as
> the original contributor's evidence.
>
> **Not yet run:** the contributor's Windows test used the earlier separate build.
> The single-DLL build has not been run on this executable (the maintainer has no
> copy); layout B itself is checked against the European 1.0 executable.

This report concerns one executable supplied from a Spanish-language
installation of **Bugs Bunny: Perdido en el Tiempo**. It does not establish
that all Spanish releases use this executable, or that language alone causes
the incompatibility. No executable or game data is included in this report.

## Fingerprint

| Property | Observed value |
| --- | --- |
| File size | 772096 bytes |
| SHA-256 | `3eae4e369d79a02d0b3cafeb7c5f80f6bbba7cf4ffc02a56876aaa885d5bc050` |
| Image base | `0x00400000` |
| Entry point | `0x00452050` |
| `.text` VA / raw size | `0x00401000` / `0x5a400` |
| `.text` CRC32 | `4df536d5` (supported: `ecffb7c9`) |
| Qword at reference marker address `0x45ca00` | `3fe2d2d2d2d2d2d3` (expected: `3fe5555555555555`) |
| Windows file version resource | `1.0` (not evidence of the engine version) |

The original 1.0-beta log stops at `host: маркер версии 1.6 не найден`
and then `attach: не эталонный bugs.exe — только проброс DirectDraw`.
The mod therefore falls back to system DirectDraw without installing its hooks.

## Static verification

Running `python -I tools/verify_exe.py <path-to-bugs.exe>` against upstream
commit `7bd69dc` finds **38 mismatches**: the marker, the code CRC32,
all 29 hook-site byte sequences, and all seven import slots.

| Import | Expected slot | Observed slot |
| --- | --- | --- |
| `DDRAW.dll!DirectDrawCreate` | `0x9cc28c` | `0x9cc290` |
| `DDRAW.dll!DirectDrawEnumerateA` | `0x9cc290` | `0x9cc294` |
| `KERNEL32.dll!FreeLibrary` | `0x9cc2c8` | `0x9cc2cc` |
| `KERNEL32.dll!GetProcAddress` | `0x9cc2d8` | `0x9cc2dc` |
| `KERNEL32.dll!LoadLibraryA` | `0x9cc2dc` | `0x9cc2e0` |
| `USER32.dll!CreateWindowExA` | `0x9cc41c` | `0x9cc428` |
| `USER32.dll!SetWindowPos` | `0x9cc3fc` | `0x9cc3f8` |

These import offsets alone already show that one uniform address shift
would not suffice. A port needs verified hook addresses, expected bytes,
global data and function addresses, including embedded operands in generated
stubs. Changing only the accepted CRC or version marker would be unsafe.

## Experimental build and validation

The original contribution's `tools/build_spanish.py` generated separate sources
using an explicit code-address and IAT map, including generated-stub targets,
render-writer expectations and return-address classification. It updates the
two relative-call signatures independently. The generated DLL still requires
this profile's exact `.text` CRC and marker bytes; the original build's guards
remain unchanged. The eight marker bytes are treated as an existing fingerprint
check, not as proof of this executable's engine version.

Validated locally:

- All 29 hook sites, all seven import slots, code CRC, and marker checks pass
  with the generated profile. Its build helper rejects any other file hash.
- Windows startup reaches the original Spanish-language title/menu with the
  remaster installed. The log confirms a 1920x1080 framebuffer, 4x MSAA,
  interpolation, extended draw distance and installed controller/camera hooks.
- A user-reproduced save-menu misalignment was corrected and confirmed fixed
  by the user. Screen-edge anchoring is disabled for frontend/loading/language
  screens and pause menus. Standalone text groups no longer qualify as HUD
  counters solely because they happen to sit near an edge.
- The original English logo also appears without the mod in this installation;
  it is an existing game asset, not evidence of a language switch. It is kept.
- Startup-logo bars now sample the actual image background inside the thin
  raster border (the observed white is RGB 248/248/248). A real hidden OpenGL
  context test verifies matching side colours, preserved image pixels and GL
  state restoration for white, blue and black backgrounds.

The earlier `ReleaseDDThreadLock` loader dialog is explained by Windows'
32-bit `d3dim.dll` importing five symbols from `DDRAW.dll` that the original
proxy did not export. The proxy now forwards these exports to system DirectDraw.
The missing `LANGUAGE.BZE` startup prompt was avoided with the documented
OpenGL/windowed/boot arguments and a `BIN` working directory; no missing game
asset was fabricated or replaced.

Testing is limited: there is no claim of a complete playthrough, verification
of every level/save/controller path, Linux testing of this profile, or support
for other Spanish distributions. See [porting editions](porting-editions.md).
