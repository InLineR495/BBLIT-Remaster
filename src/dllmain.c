// BBLIT Modern — прокси ddraw.dll. bugs.exe импортирует DDRAW.dll статически,
// поэтому под Proton (WINEDLLOVERRIDES=ddraw=n,b) эта DLL грузится до main().
// DllMain: проверка хоста (база, версия, CRC32 .text) и детур entry0 -> LateInit.
// Если хост не эталонный 1.6 или REMASTER_OFF — только проброс DirectDraw в системную ddraw.
#include <windows.h>
#include <stdint.h>
#include "hook.h"
#include "log.h"
#include "remaster.h"

typedef HRESULT (WINAPI *DirectDrawCreate_t)(GUID *, void **, IUnknown *);
typedef HRESULT (WINAPI *DirectDrawEnumerateA_t)(void *, void *);

static HMODULE real_ddraw;
static DirectDrawCreate_t real_DirectDrawCreate;
static DirectDrawEnumerateA_t real_DirectDrawEnumerateA;

static void load_real_ddraw(void)
{
    char path[MAX_PATH];
    if (real_ddraw)
        return;
    GetSystemDirectoryA(path, MAX_PATH);
    lstrcatA(path, "\\ddraw.dll");
    real_ddraw = LoadLibraryA(path);
    if (!real_ddraw) {
        bb_log("ddraw: LoadLibrary(%s) failed, err=%lu", path, GetLastError());
        return;
    }
    real_DirectDrawCreate = (DirectDrawCreate_t)GetProcAddress(real_ddraw, "DirectDrawCreate");
    real_DirectDrawEnumerateA = (DirectDrawEnumerateA_t)GetProcAddress(real_ddraw, "DirectDrawEnumerateA");
    bb_log("ddraw: real %s loaded at %p", path, (void *)real_ddraw);
}

HRESULT WINAPI proxy_DirectDrawCreate(GUID *guid, void **dd, IUnknown *outer)
{
    load_real_ddraw();
    bb_log("DirectDrawCreate(guid=%p)", (void *)guid);
    return real_DirectDrawCreate ? real_DirectDrawCreate(guid, dd, outer) : E_FAIL;
}

HRESULT WINAPI proxy_DirectDrawEnumerateA(void *cb, void *ctx)
{
    load_real_ddraw();
    bb_log("DirectDrawEnumerateA");
    return real_DirectDrawEnumerateA ? real_DirectDrawEnumerateA(cb, ctx) : E_FAIL;
}

// Эталон 1.6: база 0x400000, маркер версии и CRC32 .text (маркер не отличает hpRT, CRC отличает).
static int host_is_reference(void)
{
    uint64_t v = 0;
    SIZE_T got = 0;
    uint32_t crc;

    if ((uintptr_t)GetModuleHandleA(NULL) != BB_IMAGE_BASE) {
        bb_log("host: база %p, не 0x400000", (void *)GetModuleHandleA(NULL));
        return 0;
    }
    if (!ReadProcessMemory(GetCurrentProcess(), (void *)BB_VERSION_ADDR, &v, sizeof v, &got) || got != sizeof v
        || v != BB_VERSION_1_6) {
        bb_log("host: маркер версии 1.6 не найден");
        return 0;
    }
    crc = bb_crc32((const void *)BB_TEXT_VA, BB_TEXT_SIZE);
    bb_log("host: bugs.exe 1.6, CRC32 .text = %08x (эталон %08x)", crc, BB_TEXT_CRC32);
    return crc == BB_TEXT_CRC32;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        bb_self = inst;
        bb_log_open(inst);
        bb_config_load();
        bb_log("attach: BBLIT Remaster %s, pid=%lu dll=%p exe=%s", BB_VERSION, GetCurrentProcessId(), (void *)inst, GetCommandLineA());
        if (!host_is_reference())
            bb_log("attach: не эталонный bugs.exe — только проброс DirectDraw");
        else if (bb_cfg.off)
            bb_log("attach: REMASTER_OFF — только проброс DirectDraw");
        else if (bb_site_check(BB_SITE_ENTRY0) && bb_patch_jmp(bb_site_va(BB_SITE_ENTRY0), (void *)bb_entry_stub))
            bb_log("attach: entry0 %08x -> LateInit", bb_site_va(BB_SITE_ENTRY0));
    } else if (reason == DLL_PROCESS_DETACH) {
        if (bb_cfg.probes && !bb_cfg.off)
            bb_probe_exit();
        bb_log("detach");
        bb_log_close();
    }
    return TRUE;
}
