// Windows/i686 smoke test, run with an absolute path to a built ddraw.dll.
// This is deliberately not bugs.exe: the mod must remain in passthrough mode.
#include <windows.h>
#include <stdio.h>

int wmain(int argc, wchar_t **argv)
{
    const char *names[] = {
        "AcquireDDThreadLock", "ReleaseDDThreadLock", "DDInternalLock",
        "DDInternalUnlock", "D3DParseUnknownCommand"
    };
    if (argc != 2)
        return 2;
    HMODULE proxy = LoadLibraryW(argv[1]);
    if (!proxy) {
        fprintf(stderr, "Load proxy failed: %lu\n", GetLastError());
        return 1;
    }
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!GetProcAddress(proxy, names[i])) {
            fprintf(stderr, "Missing export: %s\n", names[i]);
            return 1;
        }
    // Resolves the legacy imports that produced the user's loader dialog.
    HMODULE d3d = LoadLibraryExW(L"d3dim.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!d3d) {
        fprintf(stderr, "Load system d3dim failed: %lu\n", GetLastError());
        return 1;
    }
    void (WINAPI *acquire)(void) = (void *)GetProcAddress(proxy, names[0]);
    void (WINAPI *release)(void) = (void *)GetProcAddress(proxy, names[1]);
    // Exercise both lazy resolution and cached forwarding, with balanced locks.
    for (unsigned i = 0; i < 2; i++) {
        acquire();
        release();
    }
    FreeLibrary(d3d);
    FreeLibrary(proxy);
    puts("DirectDraw passthrough and d3dim imports: passed");
    return 0;
}
