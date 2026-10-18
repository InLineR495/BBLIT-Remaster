// Безопасная запись в bugs.exe: каждый сайт сверяется с addr.h до записи.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hook.h"
#include "log.h"

struct site { const char *name; uint32_t va; const char *hex; };
struct iat  { const char *name; uint32_t va[2]; const char *dll; const char *fn; };
struct profile { const char *name; uint32_t crc; uint64_t marker; int layout; };

#define BB_SITE_ROW(name, va, bytes) { #name, va, bytes },
static const struct site sites[] = { BB_SITES(BB_SITE_ROW) };
#undef BB_SITE_ROW

#define BB_IAT_ROW(name, va_a, va_b, dll, fn) { #name, { va_a, va_b }, dll, fn },
static const struct iat iats[] = { BB_IATS(BB_IAT_ROW) };
#undef BB_IAT_ROW

#define BB_PROFILE_ROW(id, name, crc, marker, layout) { name, crc, marker, layout },
static const struct profile profiles[] = { BB_PROFILES(BB_PROFILE_ROW) };
#undef BB_PROFILE_ROW

#define BB_CODE_ROW(a, b) { a, b },
static const uint32_t codes[][2] = { BB_CODES(BB_CODE_ROW) };
#undef BB_CODE_ROW

static int host = -1, layout;   // выбранная сборка и её раскладка кода (0 — A, адреса как в исходниках)

int bb_host_select(uint32_t text_crc, uint64_t marker)
{
    for (int i = 0; i < BB_PROFILE_COUNT; i++)
        if (profiles[i].crc == text_crc && profiles[i].marker == marker) {
            host = i;
            layout = profiles[i].layout;
            return i;
        }
    return -1;
}

const char *bb_host_name(void)
{
    return host >= 0 ? profiles[host].name : "unknown bugs.exe";
}

uint32_t bb_code(uint32_t va)
{
    if (!layout)
        return va;
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++)
        if (codes[i][0] == va)
            return codes[i][1];
    bb_log("BB_CODE(%08x): адреса нет в BB_CODES для %s", va, bb_host_name());
    return 0;
}

uint32_t bb_crc32(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t crc = 0xffffffffu;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// "55 8b ec" -> {0x55,0x8b,0xec}; возвращает число байт.
size_t bb_parse_hex(const char *s, uint8_t *out, size_t cap)
{
    size_t n = 0;
    while (*s && n < cap) {
        int hi = hex_nibble(s[0]), lo = hi >= 0 ? hex_nibble(s[1]) : -1;
        if (hi < 0 || lo < 0) { s++; continue; }
        out[n++] = (uint8_t)(hi << 4 | lo);
        s += 2;
    }
    return n;
}

uint32_t bb_site_va(int site)
{
    return bb_code(sites[site].va);
}

// Ожидаемые байты сайта в текущем хосте: строка addr.h, у call rel32 (e8) цель переведена в раскладку хоста.
static size_t site_bytes(int site, uint8_t *out, size_t cap)
{
    size_t n = bb_parse_hex(sites[site].hex, out, cap);
    if (n == 5 && out[0] == 0xe8) {
        int32_t rel;
        memcpy(&rel, out + 1, 4);
        uint32_t to = bb_code(sites[site].va + 5 + (uint32_t)rel), va = bb_site_va(site);
        if (!to || !va)
            return 0;
        rel = (int32_t)(to - (va + 5));
        memcpy(out + 1, &rel, 4);
    }
    return n;
}

int bb_site_check(int site)
{
    uint8_t want[32];
    size_t n = site_bytes(site, want, sizeof want);
    uint32_t va = bb_site_va(site);
    if (!n || !va)
        return 0;
    const uint8_t *have = (const uint8_t *)(uintptr_t)va;
    if (memcmp(have, want, n) == 0)
        return 1;
    char got[3 * 32 + 1] = "", exp[3 * 32 + 1] = "";
    for (size_t i = 0; i < n; i++) {
        _snprintf(got + 3 * i, 4, "%02x ", have[i]);
        _snprintf(exp + 3 * i, 4, "%02x ", want[i]);
    }
    bb_log("site %s @%08x: ожидалось [%s], в памяти [%s] — патч не ставится", sites[site].name, va, exp, got);
    return 0;
}

int bb_patch(uint32_t va, const void *bytes, size_t len)
{
    DWORD old;
    void *p = (void *)(uintptr_t)va;
    if (!VirtualProtect(p, len, PAGE_EXECUTE_READWRITE, &old)) {
        bb_log("patch @%08x: VirtualProtect err=%lu", va, GetLastError());
        return 0;
    }
    memcpy(p, bytes, len);
    VirtualProtect(p, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, len);
    return 1;
}

int bb_patch_jmp(uint32_t from, const void *to)
{
    uint8_t jmp[5] = { 0xe9 };
    int32_t rel = (int32_t)((uintptr_t)to - (from + 5));
    memcpy(jmp + 1, &rel, 4);
    return bb_patch(from, jmp, sizeof jmp);
}

// Слот IAT ищется по таблице импорта exe и должен совпасть с адресом из addr.h.
void **bb_iat_slot(int iat)
{
    const struct iat *e = &iats[iat];
    uint8_t *base = (uint8_t *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress);

    for (; imp->Name; imp++) {
        if (lstrcmpiA((char *)(base + imp->Name), e->dll) != 0)
            continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        IMAGE_THUNK_DATA *slots = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (int k = 0; names[k].u1.AddressOfData; k++) {
            if (IMAGE_SNAP_BY_ORDINAL(names[k].u1.Ordinal))
                continue;
            IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME *)(base + names[k].u1.AddressOfData);
            if (lstrcmpA((char *)ibn->Name, e->fn) != 0)
                continue;
            if ((uintptr_t)&slots[k] != e->va[layout]) {
                bb_log("iat %s: слот %p, а в addr.h %08x — хук не ставится", e->name, (void *)&slots[k], e->va[layout]);
                return NULL;
            }
            return (void **)&slots[k];
        }
    }
    bb_log("iat %s: %s!%s не найден в импорте", e->name, e->dll, e->fn);
    return NULL;
}

int bb_iat_hook(int iat, void *fn, void **orig)
{
    void **slot = bb_iat_slot(iat);
    if (!slot)
        return 0;
    *orig = *slot;
    if (!bb_patch((uint32_t)(uintptr_t)slot, &fn, sizeof fn))
        return 0;
    bb_log("iat %s: %p -> %p", iats[iat].name, *orig, fn);
    return 1;
}

// --- Пре-детуры: jmp на сгенерированную заглушку, которая сохраняет все регистры,
// вызывает pre(esp_на_входе), исполняет украденные инструкции и возвращается на site+len.
// Украденные байты сайта (addr.h) должны быть целыми позиционно-независимыми инструкциями.

static uint8_t *stub_mem;
static size_t stub_used;

static uint8_t *stub_alloc(size_t n)
{
    if (!stub_mem)
        stub_mem = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!stub_mem || stub_used + n > 4096)
        return NULL;
    uint8_t *p = stub_mem + stub_used;
    stub_used += (n + 15) & ~(size_t)15;
    return p;
}

static uint8_t *put_rel32(uint8_t *p, uint8_t op, uintptr_t target)
{
    int32_t rel = (int32_t)(target - ((uintptr_t)p + 5));
    *p++ = op;
    memcpy(p, &rel, 4);
    return p + 4;
}

int bb_detour_pre(int site, void (*pre)(uint32_t *esp))
{
    uint8_t stolen[32], patch[32];
    size_t n = site_bytes(site, stolen, sizeof stolen);
    uint32_t va = bb_site_va(site);
    uint8_t *s, *p;

    if (n < 5 || !bb_site_check(site))
        return 0;
    if (!(s = stub_alloc(32 + n))) {
        bb_log("detour %s: нет места под заглушку", sites[site].name);
        return 0;
    }
    p = s;
    *p++ = 0x60;                                            // pushal
    *p++ = 0x9c;                                            // pushfl
    memcpy(p, "\x8d\x44\x24\x24", 4); p += 4;               // lea eax,[esp+0x24] — esp на входе
    *p++ = 0x50;                                            // push eax
    p = put_rel32(p, 0xe8, (uintptr_t)pre);                 // call pre
    memcpy(p, "\x83\xc4\x04\x9d\x61", 5); p += 5;           // add esp,4; popfl; popal
    memcpy(p, stolen, n); p += n;                           // украденные инструкции
    p = put_rel32(p, 0xe9, va + n);                         // jmp site+n
    FlushInstructionCache(GetCurrentProcess(), s, (size_t)(p - s));

    int32_t rel = (int32_t)((uintptr_t)s - (va + 5));
    patch[0] = 0xe9;
    memcpy(patch + 1, &rel, 4);
    memset(patch + 5, 0x90, n - 5);
    if (!bb_patch(va, patch, n))
        return 0;
    bb_log("detour %s @%08x -> stub %p (pre %p, %u байт)", sites[site].name, va, (void *)s, (void *)pre, (unsigned)n);
    return 1;
}

// --- Обёртки «вокруг» для слотов-указателей без знания арности ---
// В слот ставится заглушка: pre(k, esp) подменяет адрес возврата на post-заглушку (настоящий — в теневой стек),
// затем jmp на оригинал; после возврата post(k, esp) кладёт настоящий адрес возврата и делает ret.
// eax/edx (результат), флаги и стек вызывающего не меняются; cdecl — стек чистит вызывающий.

#define AROUND_MAX 16
static uint8_t *around_mem;
static uint32_t around_orig[AROUND_MAX];
static void (*around_pre_cb[AROUND_MAX])(int, uint32_t *);
static void (*around_post_cb[AROUND_MAX])(int, uint32_t *);
static uint32_t shadow_ret[256];
static int shadow_top;

static void around_enter(int k, uint32_t *esp)
{
    if (around_pre_cb[k])
        around_pre_cb[k](k, esp);
    if (shadow_top >= (int)(sizeof shadow_ret / sizeof shadow_ret[0]))
        return;                                                 // переполнение: без post, вызов идёт как есть
    shadow_ret[shadow_top++] = esp[0];
    esp[0] = (uint32_t)(uintptr_t)(around_mem + 64 * k + 32);   // вернуться в post-заглушку k
}

static void around_leave(int k, uint32_t *esp)
{
    // esp[0] — заглушка под адрес возврата, esp[1..] — аргументы вызова
    esp[0] = shadow_ret[--shadow_top];
    if (around_post_cb[k])
        around_post_cb[k](k, esp);
}

static uint8_t *gen_call_cb(uint8_t *p, int k, void (*cb)(int, uint32_t *))
{
    *p++ = 0x60;                                            // pushal
    *p++ = 0x9c;                                            // pushfl
    memcpy(p, "\x8d\x44\x24\x24", 4); p += 4;               // lea eax,[esp+0x24]
    *p++ = 0x50;                                            // push eax
    *p++ = 0x6a; *p++ = (uint8_t)k;                         // push k
    p = put_rel32(p, 0xe8, (uintptr_t)cb);                  // call cb
    memcpy(p, "\x83\xc4\x08\x9d\x61", 5); p += 5;           // add esp,8; popfl; popal
    return p;
}

int bb_around_slot(int k, uint32_t slot, uint32_t expect, void (*pre)(int, uint32_t *), void (*post)(int, uint32_t *))
{
    uint8_t *p;
    if (k < 0 || k >= AROUND_MAX)
        return 0;
    if (!around_mem && !(around_mem = VirtualAlloc(NULL, 64 * AROUND_MAX, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)))
        return 0;
    if (*(volatile uint32_t *)(uintptr_t)slot != expect)
        return 0;
    around_orig[k] = expect;
    around_pre_cb[k] = pre;
    around_post_cb[k] = post;
    p = around_mem + 64 * k;                                // вход: [0..32)
    p = gen_call_cb(p, k, around_enter);
    *p++ = 0xff; *p++ = 0x25;                               // jmp dword [around_orig + 4k]
    *(uint32_t *)p = (uint32_t)(uintptr_t)&around_orig[k];
    p = around_mem + 64 * k + 32;                           // post: [32..64)
    *p++ = 0x50;                                            // push eax — место под адрес возврата
    p = gen_call_cb(p, k, around_leave);
    *p++ = 0xc3;                                            // ret
    FlushInstructionCache(GetCurrentProcess(), around_mem + 64 * k, 64);
    *(volatile uint32_t *)(uintptr_t)slot = (uint32_t)(uintptr_t)(around_mem + 64 * k);
    return 1;
}

// То же для кода: детур сайта из BB_SITES (украденные байты исполняются после pre, затем jmp site+n).
int bb_around_site(int k, int site, void (*pre)(int, uint32_t *), void (*post)(int, uint32_t *))
{
    uint8_t stolen[16], patch[16], *p;
    size_t n = site_bytes(site, stolen, sizeof stolen);
    uint32_t va = bb_site_va(site);
    int32_t rel;

    if (k < 0 || k >= AROUND_MAX || n < 5 || n > 8 || !bb_site_check(site))
        return 0;
    if (!around_mem && !(around_mem = VirtualAlloc(NULL, 64 * AROUND_MAX, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)))
        return 0;
    around_pre_cb[k] = pre;
    around_post_cb[k] = post;
    p = around_mem + 64 * k;                                // вход: [0..32)
    p = gen_call_cb(p, k, around_enter);
    memcpy(p, stolen, n); p += n;
    p = put_rel32(p, 0xe9, va + n);
    p = around_mem + 64 * k + 32;                           // post: [32..64)
    *p++ = 0x50;
    p = gen_call_cb(p, k, around_leave);
    *p++ = 0xc3;
    FlushInstructionCache(GetCurrentProcess(), around_mem + 64 * k, 64);

    rel = (int32_t)((uintptr_t)(around_mem + 64 * k) - (va + 5));
    patch[0] = 0xe9;
    memcpy(patch + 1, &rel, 4);
    memset(patch + 5, 0x90, n - 5);
    if (!bb_patch(va, patch, n))
        return 0;
    bb_log("around %s @%08x -> stub %p", sites[site].name, va, (void *)(around_mem + 64 * k));
    return 1;
}
