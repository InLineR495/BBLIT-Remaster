// BBLIT Modern — прокси ddraw.dll. bugs.exe импортирует DDRAW.dll статически,
// поэтому под Proton (WINEDLLOVERRIDES=ddraw=n,b) эта DLL грузится до main().
// DllMain: выбор сборки хоста (база, CRC32 .text, маркер — BB_PROFILES) и детур entry0 -> LateInit.
// Если сборка неизвестна или REMASTER_OFF — только проброс DirectDraw в системную ddraw.
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

// Сборка хоста: база 0x400000, CRC32 .text и маркер по таблице BB_PROFILES (addr.h). Неизвестная сборка (другой
// выпуск или пропатченный exe) — 0: мод не ставится, только проброс DirectDraw.
static int host_detect(void)
{
    uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION(nt);
    uint64_t v = 0;
    SIZE_T got = 0;
    uint32_t crc = 0;
    int text_ok = 0;

    if ((uintptr_t)base != BB_IMAGE_BASE) {
        bb_log("host: база %p, не 0x400000", (void *)base);
        return 0;
    }
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
        crc = bb_crc32(base + s->VirtualAddress, s->SizeOfRawData);
        text_ok = BB_IMAGE_BASE + s->VirtualAddress == BB_TEXT_VA && s->SizeOfRawData == BB_TEXT_SIZE;
        bb_log("host: .text VA=%08lx raw_size=%08lx CRC32=%08x",
               (unsigned long)(BB_IMAGE_BASE + s->VirtualAddress), (unsigned long)s->SizeOfRawData, crc);
        break;
    }
    if (!ReadProcessMemory(GetCurrentProcess(), (void *)BB_MARKER_ADDR, &v, sizeof v, &got) || got != sizeof v)
        v = 0;
    if (!text_ok || bb_host_select(crc, v) < 0) {
        bb_log("host: неизвестная сборка: CRC32 .text %08x, маркер @%08x = %016llx", crc, BB_MARKER_ADDR,
               (unsigned long long)v);
        return 0;
    }
    bb_log("host: %s, CRC32 .text = %08x", bb_host_name(), crc);
    return 1;
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
        if (!host_detect())
            bb_log("attach: неподдерживаемый bugs.exe — только проброс DirectDraw");
        else if (bb_cfg.off)
            bb_log("attach: REMASTER_OFF — только проброс DirectDraw");
        else if ((bb_entry_resume = BB_CODE(0x451eb5u)) && bb_site_check(BB_SITE_ENTRY0) &&
                 bb_patch_jmp(bb_site_va(BB_SITE_ENTRY0), (void *)bb_entry_stub))
            bb_log("attach: entry0 %08x -> LateInit", bb_site_va(BB_SITE_ENTRY0));
    } else if (reason == DLL_PROCESS_DETACH) {
        if (bb_cfg.probes && !bb_cfg.off)
            bb_probe_exit();
        bb_log("detach");
        bb_log_close();
    }
    return TRUE;
}
