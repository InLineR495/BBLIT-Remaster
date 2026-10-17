// BBLIT Modern — прокси ddraw.dll. bugs.exe импортирует DDRAW.dll статически,
// поэтому под Proton (WINEDLLOVERRIDES=ddraw=n,b) эта DLL грузится до main().
// DllMain: проверка хоста (база, версия, CRC32 .text) и детур entry0 -> LateInit.
// Если хост не эталонный 1.6 или REMASTER_OFF — только проброс DirectDraw в системную ddraw.
#include <windows.h>
#include <stdint.h>
#include <string.h>
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

// Windows d3dim.dll imports these functions from the module named ddraw.dll.
// Forward to the real implementation even when an unsupported host disables
// the remaster. A tail jump preserves the undocumented entry points' ABI.
FARPROC bb_ddraw_export(const char *name)
{
    load_real_ddraw();
    FARPROC fn = real_ddraw ? GetProcAddress(real_ddraw, name) : NULL;
    if (!fn) {
        bb_log("ddraw: required system export %s is unavailable", name);
        ExitProcess(ERROR_PROC_NOT_FOUND);
    }
    return fn;
}

// Эталон 1.6: база 0x400000, маркер версии и CRC32 .text (маркер не отличает hpRT, CRC отличает).
static int host_is_reference(void)
{
    uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION(nt);
    uint64_t v = 0;
    SIZE_T got = 0;
    uint32_t crc;

    if ((uintptr_t)GetModuleHandleA(NULL) != BB_IMAGE_BASE) {
        bb_log("host: база %p, не 0x400000", (void *)GetModuleHandleA(NULL));
        return 0;
    }
    // Other editions may have a different layout. Report their actual code
    // fingerprint even when the reference version marker does not match.
    // All existing acceptance checks below remain required.
    bb_log("host: PE timestamp=%08lx image_size=%08lx entry=%08lx",
           (unsigned long)nt->FileHeader.TimeDateStamp,
           (unsigned long)nt->OptionalHeader.SizeOfImage,
           (unsigned long)(BB_IMAGE_BASE + nt->OptionalHeader.AddressOfEntryPoint));
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        IMAGE_SECTION_HEADER *s = &sections[i];
        if (memcmp(s->Name, ".text\0\0\0", IMAGE_SIZEOF_SHORT_NAME) != 0)
            continue;
        if (s->VirtualAddress > nt->OptionalHeader.SizeOfImage ||
            s->SizeOfRawData > nt->OptionalHeader.SizeOfImage - s->VirtualAddress)
            break;
        bb_log("host: actual .text VA=%08lx raw_size=%08lx CRC32=%08x",
               (unsigned long)(BB_IMAGE_BASE + s->VirtualAddress),
               (unsigned long)s->SizeOfRawData,
               bb_crc32(base + s->VirtualAddress, s->SizeOfRawData));
        break;
    }
    if (!ReadProcessMemory(GetCurrentProcess(), (void *)BB_VERSION_ADDR, &v, sizeof v, &got) || got != sizeof v
        || v != BB_VERSION_1_6) {
        bb_log("host: reference marker @%08x: read=%lu bytes, value=%016llx (expected %016llx)",
               BB_VERSION_ADDR, (unsigned long)got,
               (unsigned long long)v, (unsigned long long)BB_VERSION_1_6);
        bb_log("host: маркер версии 1.6 не найден");
        return 0;
    }
    crc = bb_crc32((const void *)BB_TEXT_VA, BB_TEXT_SIZE);
    bb_log("host: %s, CRC32 .text = %08x (эталон %08x)", BB_HOST_NAME, crc, BB_TEXT_CRC32);
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
