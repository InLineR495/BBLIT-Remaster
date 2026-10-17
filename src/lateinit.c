// LateInit: главный поток, вне loader lock, до CRT игры и WinMain.
// Сюда попадаем из детура entry0 (0x451eb0); дальше исполнение идёт в 0x451eb5.
#include <windows.h>
#include <stdint.h>
#include "glrec.h"
#include "hook.h"
#include "log.h"
#include "remaster.h"

struct bb_config bb_cfg;
HINSTANCE bb_self;

// Украденные байты entry0: push ebp; mov ebp,esp; push -1 — исполняем после LateInit.
const uint32_t bb_entry_resume __attribute__((used)) = 0x451eb5;
__asm__(
    ".text\n"
    ".globl _bb_entry_stub\n"
    "_bb_entry_stub:\n"
    "    pushal\n"
    "    pushfl\n"
    "    call _bb_late_init\n"
    "    popfl\n"
    "    popal\n"
    "    pushl %ebp\n"
    "    movl %esp, %ebp\n"
    "    pushl $-1\n"
    "    jmp *_bb_entry_resume\n");

void bb_config_load(void)
{
    char ini[MAX_PATH], env[8];
    DWORD n = GetModuleFileNameA(bb_self, ini, MAX_PATH);
    while (n > 0 && ini[n - 1] != '\\' && ini[n - 1] != '/')
        n--;
    lstrcpyA(ini + n, "remaster.ini");
    bb_cfg.off = GetPrivateProfileIntA("remaster", "off", 0, ini);
    bb_cfg.log_gpa = GetPrivateProfileIntA("remaster", "log_gpa", 0, ini);
    bb_cfg.veh = GetPrivateProfileIntA("remaster", "veh", 1, ini);
    bb_cfg.probes = GetPrivateProfileIntA("remaster", "probes", 1, ini);
    bb_cfg.borderless = GetPrivateProfileIntA("remaster", "borderless", 1, ini);
    bb_cfg.pacer_sleep = GetPrivateProfileIntA("remaster", "pacer_sleep", 1, ini);
    bb_cfg.video_guard = GetPrivateProfileIntA("remaster", "video_guard", 1, ini);
    bb_cfg.render = GetPrivateProfileIntA("remaster", "render", 1, ini);
    bb_cfg.msaa = GetPrivateProfileIntA("remaster", "msaa", 4, ini);
    bb_cfg.vsync = GetPrivateProfileIntA("remaster", "vsync", -1, ini);
    bb_cfg.hor_plus = GetPrivateProfileIntA("remaster", "hor_plus", 1, ini);
    bb_cfg.hor_guard = GetPrivateProfileIntA("remaster", "hor_guard", 20, ini);
    bb_cfg.hor_logic = GetPrivateProfileIntA("remaster", "hor_logic", 1, ini);
    bb_cfg.no_quad_split = GetPrivateProfileIntA("remaster", "no_quad_split", 1, ini);
    bb_cfg.fix_shadow40 = GetPrivateProfileIntA("remaster", "fix_shadow40", 1, ini);
    bb_cfg.fog_sharp = GetPrivateProfileIntA("remaster", "fog_sharp", 25, ini);
    bb_cfg.interp = GetPrivateProfileIntA("remaster", "interp", 1, ini);
    bb_cfg.alpha_vb = GetPrivateProfileIntA("remaster", "alpha_vb", 1, ini);
    bb_cfg.fence = GetPrivateProfileIntA("remaster", "fence", 1, ini);
    bb_cfg.p23 = GetPrivateProfileIntA("remaster", "p23", 0, ini);
    bb_cfg.vb_guard = GetPrivateProfileIntA("remaster", "vb_guard", 110, ini);
    bb_cfg.hud_edges = GetPrivateProfileIntA("remaster", "hud_edges", 1, ini);
    bb_cfg.title_logo = GetPrivateProfileIntA("remaster", "title_logo", 85, ini);
    bb_cfg.decals = GetPrivateProfileIntA("remaster", "decals", 1, ini);
    bb_cfg.push_r = GetPrivateProfileIntA("remaster", "push_r", 50, ini);
    bb_cfg.log_sec = GetPrivateProfileIntA("remaster", "log_sec", 0, ini);
    bb_cfg.fps = GetPrivateProfileIntA("remaster", "fps", 0, ini);
    bb_cfg.gl_profile = GetPrivateProfileIntA("remaster", "gl_profile", -1, ini);
    bb_cfg.trans_mode = GetPrivateProfileIntA("remaster", "trans_mode", 1, ini);
    bb_cfg.shader = GetPrivateProfileIntA("remaster", "shader", 1, ini);
    bb_cfg.ps1_blend = GetPrivateProfileIntA("remaster", "ps1_blend", 1, ini);
    bb_cfg.ps1_gain = GetPrivateProfileIntA("remaster", "ps1_gain", 20, ini);
    bb_cfg.jump_k = GetPrivateProfileIntA("remaster", "jump_k", 10, ini);
    bb_cfg.repair = GetPrivateProfileIntA("remaster", "repair", 1, ini);
    bb_cfg.half_rate = GetPrivateProfileIntA("remaster", "half_rate", 1, ini);
    bb_cfg.reproj = GetPrivateProfileIntA("remaster", "reproj", 1, ini);
    bb_cfg.ghost = GetPrivateProfileIntA("remaster", "ghost", 1, ini);
    bb_cfg.quad_cull = GetPrivateProfileIntA("remaster", "quad_cull", 1, ini);
    bb_cfg.xinput = GetPrivateProfileIntA("remaster", "xinput", 1, ini);
    bb_cfg.analog = GetPrivateProfileIntA("remaster", "analog", 1, ini);
    bb_cfg.stick_dz = GetPrivateProfileIntA("remaster", "stick_dz", 12, ini);
    bb_cfg.stick_full = GetPrivateProfileIntA("remaster", "stick_full", 90, ini);
    bb_cfg.jump_buf = GetPrivateProfileIntA("remaster", "jump_buf", 4, ini);
    bb_cfg.pad_esc = GetPrivateProfileIntA("remaster", "pad_esc", 1, ini);
    bb_cfg.pad_map = GetPrivateProfileIntA("remaster", "pad_map", 1, ini);
    bb_cfg.cam_stick = GetPrivateProfileIntA("remaster", "cam_stick", 1, ini);
    bb_cfg.cam_speed = GetPrivateProfileIntA("remaster", "cam_speed", 52, ini);
    bb_cfg.cam_invert = GetPrivateProfileIntA("remaster", "cam_invert", 0, ini);
    bb_cfg.cam_swap = GetPrivateProfileIntA("remaster", "cam_swap", 1, ini);
    bb_cfg.sneak = GetPrivateProfileIntA("remaster", "sneak", 0, ini);
    bb_cfg.big_cores = GetPrivateProfileIntA("remaster", "big_cores", 1, ini);
    bb_cfg.hotkeys = GetPrivateProfileIntA("remaster", "hotkeys", 0, ini);
    bb_cfg.fog = GetPrivateProfileIntA("remaster", "fog", 1, ini);
    bb_cfg.draw_dist = GetPrivateProfileIntA("remaster", "draw_dist", 100, ini);
    bb_cfg.fog_start = GetPrivateProfileIntA("remaster", "fog_start", 75, ini);
    bb_cfg.actor_dist = GetPrivateProfileIntA("remaster", "actor_dist", 400, ini);
    bb_cfg.fade_in = GetPrivateProfileIntA("remaster", "fade_in", 1, ini);
    bb_cfg.haze = GetPrivateProfileIntA("remaster", "haze", 100, ini);
    bb_cfg.haze_depth = GetPrivateProfileIntA("remaster", "haze_depth", 0, ini);
    bb_cfg.haze_thick = GetPrivateProfileIntA("remaster", "haze_thick", 2500, ini);
    bb_cfg.haze_near = GetPrivateProfileIntA("remaster", "haze_near", 4000, ini);
    bb_cfg.haze_far = GetPrivateProfileIntA("remaster", "haze_far", 20000, ini);
    bb_cfg.haze_max = GetPrivateProfileIntA("remaster", "haze_max", 85, ini);
    bb_cfg.haze_a0 = GetPrivateProfileIntA("remaster", "haze_a0", 2, ini);
    bb_cfg.haze_a1 = GetPrivateProfileIntA("remaster", "haze_a1", 10, ini);
    bb_cfg.room_depth = GetPrivateProfileIntA("remaster", "room_depth", 3, ini);
    bb_cfg.v_guard = GetPrivateProfileIntA("remaster", "v_guard", 30, ini);
    bb_cfg.portal_guard = GetPrivateProfileIntA("remaster", "portal_guard", 100, ini);
    if (GetEnvironmentVariableA("REMASTER_OFF", env, sizeof env) && env[0] == '1')
        bb_cfg.off = 1;
}

// Секция [live] перечитывается раз в секунду: render=0/1 переключает свой рендер без перезапуска (A/B с SSH).
void bb_config_live(void)
{
    char ini[MAX_PATH];
    DWORD n = GetModuleFileNameA(bb_self, ini, MAX_PATH);
    while (n > 0 && ini[n - 1] != '\\' && ini[n - 1] != '/')
        n--;
    lstrcpyA(ini + n, "remaster.ini");
    int r = GetPrivateProfileIntA("live", "render", -1, ini);
    if (r >= 0 && r != bb_cfg.render) {
        bb_cfg.render = r;
        bb_log("live: свой рендер %s", r ? "ВКЛ" : "ВЫКЛ (оригинал)");
    }
    int fs = GetPrivateProfileIntA("live", "fog_sharp", -1, ini);
    if (fs >= 0 && fs != bb_cfg.fog_sharp) {
        bb_cfg.fog_sharp = fs;
        bb_log("live: fog_sharp=%d", fs);
    }
    int gp = GetPrivateProfileIntA("live", "gl_profile", -2, ini);
    if (gp >= -1 && gp != bb_cfg.gl_profile) {
        bb_cfg.gl_profile = gp;
        bb_log("live: gl_profile=%d (применится на Reinit)", gp);
    }
    int tm = GetPrivateProfileIntA("live", "trans_mode", -1, ini);
    if (tm >= 0 && tm != bb_cfg.trans_mode) {
        bb_cfg.trans_mode = tm;
        bb_log("live: trans_mode=%d", tm);
    }
    int sh = GetPrivateProfileIntA("live", "shader", -1, ini);
    if (sh >= 0 && sh != bb_cfg.shader) {
        bb_cfg.shader = sh;
        bb_log("live: shader=%d", sh);
    }
    int pb = GetPrivateProfileIntA("live", "ps1_blend", -1, ini);
    if (pb >= 0 && pb != bb_cfg.ps1_blend) {
        bb_cfg.ps1_blend = pb;
        bb_log("live: ps1_blend=%d", pb);
    }
    int pg = GetPrivateProfileIntA("live", "ps1_gain", -1, ini);
    if (pg >= 0 && pg != bb_cfg.ps1_gain) {
        bb_cfg.ps1_gain = pg;
        bb_log("live: ps1_gain=%d", pg);
    }
    int jk = GetPrivateProfileIntA("live", "jump_k", -1, ini);
    if (jk > 0 && jk != bb_cfg.jump_k) {
        bb_cfg.jump_k = jk;
        bb_log("live: jump_k=%d", jk);
    }
    int rp = GetPrivateProfileIntA("live", "repair", -1, ini);
    if (rp >= 0 && rp != bb_cfg.repair) {
        bb_cfg.repair = rp;
        bb_log("live: repair=%d", rp);
    }
    int hr = GetPrivateProfileIntA("live", "half_rate", -1, ini);
    if (hr >= 0 && hr != bb_cfg.half_rate) {
        bb_cfg.half_rate = hr;
        bb_log("live: half_rate=%d", hr);
    }
    int av = GetPrivateProfileIntA("live", "alpha_vb", -1, ini);
    if (av >= 0 && av != bb_cfg.alpha_vb) {
        bb_cfg.alpha_vb = av;
        bb_log("live: alpha_vb=%d", av);
    }
    int rj = GetPrivateProfileIntA("live", "reproj", -1, ini);
    if (rj >= 0 && rj != bb_cfg.reproj) {
        bb_cfg.reproj = rj;
        bb_log("live: reproj=%d", rj);
    }
    static const struct { const char *key; int *v; int lo, hi; } in_keys[] = {
        { "analog", &bb_cfg.analog, 0, 1 }, { "stick_dz", &bb_cfg.stick_dz, 0, 50 },
        { "stick_full", &bb_cfg.stick_full, 30, 100 }, { "jump_buf", &bb_cfg.jump_buf, 0, 10 },
        { "pad_esc", &bb_cfg.pad_esc, 0, 1 }, { "pad_map", &bb_cfg.pad_map, 0, 1 },
        { "cam_stick", &bb_cfg.cam_stick, 0, 1 }, { "cam_speed", &bb_cfg.cam_speed, 8, 160 },
        { "cam_invert", &bb_cfg.cam_invert, 0, 1 }, { "cam_swap", &bb_cfg.cam_swap, 0, 1 },
        { "sneak", &bb_cfg.sneak, 0, 90 },
        { "fence", &bb_cfg.fence, 0, 1 }, { "p23", &bb_cfg.p23, 0, 1 }, { "vb_guard", &bb_cfg.vb_guard, 0, 150 }, { "hud_edges", &bb_cfg.hud_edges, 0, 1 }, { "title_logo", &bb_cfg.title_logo, 0, 100 }, { "decals", &bb_cfg.decals, 0, 1 }, { "push_r", &bb_cfg.push_r, 0, 200 }, { "log_sec", &bb_cfg.log_sec, 0, 1 },
    };
    for (unsigned k = 0; k < sizeof in_keys / sizeof in_keys[0]; k++) {   // M6: ввод на лету
        int v = GetPrivateProfileIntA("live", in_keys[k].key, -1, ini);
        if (v >= in_keys[k].lo && v <= in_keys[k].hi && v != *in_keys[k].v) {
            *in_keys[k].v = v;
            bb_log("live: %s=%d", in_keys[k].key, v);
        }
    }
    int qcv = GetPrivateProfileIntA("live", "quad_cull", -1, ini);
    if (qcv >= 0 && qcv != bb_cfg.quad_cull) {
        bb_cfg.quad_cull = qcv;
        bb_log("live: quad_cull=%d", qcv);
    }
    int gh = GetPrivateProfileIntA("live", "ghost", -1, ini);
    if (gh >= 0 && gh != bb_cfg.ghost) {
        bb_cfg.ghost = gh;
        bb_log("live: ghost=%d", gh);
    }
    int fg = GetPrivateProfileIntA("live", "fog", -1, ini);
    if (fg >= 0 && fg != bb_cfg.fog) {
        bb_cfg.fog = fg;
        bb_log("live: fog=%d", fg);
    }
    int dd = GetPrivateProfileIntA("live", "draw_dist", -1, ini);
    if (dd >= 50 && dd <= 400 && dd != bb_cfg.draw_dist) {
        bb_cfg.draw_dist = dd;
        bb_log("live: draw_dist=%d", dd);
    }
    int fs0 = GetPrivateProfileIntA("live", "fog_start", -1, ini);
    if (fs0 >= 0 && fs0 < 100 && fs0 != bb_cfg.fog_start) {
        bb_cfg.fog_start = fs0;
        bb_log("live: fog_start=%d", fs0);
    }
    int rd = GetPrivateProfileIntA("live", "room_depth", -1, ini);
    if (rd >= 0 && rd <= 20 && rd != bb_cfg.room_depth) {
        bb_cfg.room_depth = rd;
        bb_log("live: room_depth=%d", rd);
    }
    int vg = GetPrivateProfileIntA("live", "v_guard", -1, ini);
    if (vg >= 0 && vg <= 100 && vg != bb_cfg.v_guard) {
        bb_cfg.v_guard = vg;
        bb_log("live: v_guard=%d", vg);
    }
    int pgd = GetPrivateProfileIntA("live", "portal_guard", -1, ini);
    if (pgd >= 0 && pgd <= 400 && pgd != bb_cfg.portal_guard) {
        bb_cfg.portal_guard = pgd;
        bb_log("live: portal_guard=%d", pgd);
    }
    int hg = GetPrivateProfileIntA("live", "hor_guard", -1, ini);
    if (hg >= 0 && hg <= 50 && hg != bb_cfg.hor_guard) {
        bb_cfg.hor_guard = hg;
        bb_log("live: hor_guard=%d", hg);
    }
    static const struct { const char *key; int *val; int lo, hi; } hz[] = {
        { "haze", &bb_cfg.haze, 0, 100 }, { "haze_depth", &bb_cfg.haze_depth, -20000, 20000 },
        { "haze_thick", &bb_cfg.haze_thick, 1, 50000 }, { "haze_near", &bb_cfg.haze_near, 0, 100000 },
        { "haze_far", &bb_cfg.haze_far, 1, 200000 }, { "haze_max", &bb_cfg.haze_max, 0, 100 },
        { "haze_a0", &bb_cfg.haze_a0, 0, 89 }, { "haze_a1", &bb_cfg.haze_a1, 1, 90 },
    };
    // дымка по уровням: [levelN] (N — как в /bN, id уровня [[0x52fd00]+0x10000]) → [live] → значение при запуске.
    // В L01A дымка до +800 над ногами прячет обрыв «шеи динозавра», в L03A та же чернит острова (тёмный горизонт).
    static int hz_def[8], hz_def_ok;
    if (!hz_def_ok) {
        for (unsigned i = 0; i < sizeof hz / sizeof hz[0]; i++)
            hz_def[i] = *hz[i].val;
        hz_def_ok = 1;
    }
    uint32_t ram = *(volatile uint32_t *)(uintptr_t)0x52fd00u;
    char lsec[24];
    wsprintfA(lsec, "level%lu", ram ? (unsigned long)*(volatile uint32_t *)(uintptr_t)(ram + 0x10000) : 0xfffffffful);
    for (unsigned i = 0; i < sizeof hz / sizeof hz[0]; i++) {
        int v = GetPrivateProfileIntA(lsec, hz[i].key, -99999, ini);
        if (v == -99999)
            v = GetPrivateProfileIntA("live", hz[i].key, -99999, ini);
        if (v == -99999)
            v = hz_def[i];
        if (v >= hz[i].lo && v <= hz[i].hi && v != *hz[i].val) {
            *hz[i].val = v;
            bb_log("live: %s=%d (%s)", hz[i].key, v, lsec);
        }
    }
    int fi = GetPrivateProfileIntA("live", "fade_in", -1, ini);
    if (fi >= 0 && fi != bb_cfg.fade_in) {
        bb_cfg.fade_in = fi;
        bb_log("live: fade_in=%d", fi);
    }
    int ad = GetPrivateProfileIntA("live", "actor_dist", -1, ini);
    if (ad >= 100 && ad <= 400 && ad != bb_cfg.actor_dist) {
        bb_cfg.actor_dist = ad;
        bb_log("live: actor_dist=%d", ad);
    }
    int ip = GetPrivateProfileIntA("live", "interp", -1, ini);
    if (ip >= 0 && ip != bb_cfg.interp) {
        bb_cfg.interp = ip;
        bb_log("live: интерполяция %s", ip ? "ВКЛ (частота экрана)" : "ВЫКЛ (30 кадров)");
    }
    int cap = GetPrivateProfileIntA("live", "cap", 0, ini);
    if (cap > 0) {                  // отладка: запись cap кадров (cap_lvl — начать на уровне, cap_div — уменьшение)
        extern int bb_cap_req, bb_cap_lvl, bb_cap_div;
        WritePrivateProfileStringA("live", "cap", "0", ini);
        bb_cap_lvl = GetPrivateProfileIntA("live", "cap_lvl", -1, ini);
        bb_cap_div = GetPrivateProfileIntA("live", "cap_div", 2, ini);
        bb_cap_req = cap;
        bb_log("live: cap=%d cap_lvl=%d cap_div=%d", cap, bb_cap_lvl, bb_cap_div);
    }
    int tr = GetPrivateProfileIntA("live", "trace", 0, ini);
    if (tr > 0) {                   // отладка: траектории объектов за tr кадров в trace.bin
        extern int bb_trace_req, bb_trace_full;
        WritePrivateProfileStringA("live", "trace", "0", ini);
        bb_trace_full = GetPrivateProfileIntA("live", "trace_full", 0, ini);
        bb_trace_req = tr;
        bb_log("live: trace=%d", tr);
    }
    if (GetPrivateProfileIntA("live", "ar_dump", 0, ini) == 1) {  // отладка: решения M5-AR за тик в ar_dump.txt
        extern int bb_ar_dump_req;
        WritePrivateProfileStringA("live", "ar_dump", "0", ini);
        bb_ar_dump_req = 1;
    }
    if (GetPrivateProfileIntA("live", "dump", 0, ini) == 1) {     // отладка: пара снимков в pair_dump.txt
        extern int bb_dump_req;
        WritePrivateProfileStringA("live", "dump", "0", ini);
        bb_dump_req = 1;
    }
    // отладка: reinit=1 — Reinit движка, как «OK» видео-меню ([0x45f2b0] = 0x13), ключ сбрасывается
    if (GetPrivateProfileIntA("live", "reinit", 0, ini) == 1) {
        WritePrivateProfileStringA("live", "reinit", "0", ini);
        *(volatile uint32_t *)(uintptr_t)0x52fb9c = *(volatile uint32_t *)(uintptr_t)0x45f2b0;
        *(volatile uint32_t *)(uintptr_t)0x45f2b0 = 0x13;
        bb_log("live: запрошен Reinit ([0x45f2b0]=0x13)");
    }
}

// --- I3: GetProcAddress / LoadLibraryA / FreeLibrary (M0: только лог) ---

static FARPROC (WINAPI *real_GetProcAddress)(HMODULE, LPCSTR);
static HMODULE (WINAPI *real_LoadLibraryA)(LPCSTR);
static BOOL (WINAPI *real_FreeLibrary)(HMODULE);
static LONG gpa_count;
static HMODULE gl_module;   // opengl32, загруженный движком

static FARPROC WINAPI hook_GetProcAddress(HMODULE mod, LPCSTR name)
{
    FARPROC p = real_GetProcAddress(mod, name);
    DWORD err = GetLastError();
    LONG n = InterlockedIncrement(&gpa_count);
    if ((bb_cfg.probes || bb_cfg.borderless || bb_cfg.render) && p && mod && mod == gl_module && HIWORD((DWORD_PTR)name))
        p = (FARPROC)bb_gl_intercept(name, (void *)p);
    if (bb_cfg.log_gpa) {
        void *ret = __builtin_return_address(0);
        if (HIWORD((DWORD_PTR)name))
            bb_log("GPA #%ld %p %s -> %p (ret %p)", n, (void *)mod, name, (void *)p, ret);
        else
            bb_log("GPA #%ld %p #%u -> %p (ret %p)", n, (void *)mod, (unsigned)LOWORD((DWORD_PTR)name), (void *)p, ret);
    }
    SetLastError(err);
    return p;
}

static HMODULE WINAPI hook_LoadLibraryA(LPCSTR name)
{
    HMODULE h = real_LoadLibraryA(name);
    DWORD err = GetLastError();
    if (h && name && lstrcmpiA(name, "opengl32.dll") == 0)
        gl_module = h;
    bb_log("LoadLibraryA(%s) -> %p (ret %p)", name ? name : "(null)", (void *)h, __builtin_return_address(0));
    SetLastError(err);
    return h;
}

static BOOL WINAPI hook_FreeLibrary(HMODULE h)
{
    BOOL ok = real_FreeLibrary(h);
    DWORD err = GetLastError();
    bb_log("FreeLibrary(%p) -> %d (ret %p)", (void *)h, ok, __builtin_return_address(0));
    SetLastError(err);
    return ok;
}

// --- VEH: только наблюдение, обработку исключений не меняет ---

static LONG veh_count;

static LONG CALLBACK veh_logger(EXCEPTION_POINTERS *ep)
{
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    CONTEXT *c = ep->ContextRecord;
    char mod[MAX_PATH] = "?";
    HMODULE hm;

    switch (er->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        break;
    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (InterlockedIncrement(&veh_count) > 32)
        return EXCEPTION_CONTINUE_SEARCH;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)er->ExceptionAddress, &hm))
        GetModuleFileNameA(hm, mod, sizeof mod);
    bb_log("VEH #%ld code=%08lx at %p (%s) info=[%lx %lx] tid=%lu",
           veh_count, er->ExceptionCode, er->ExceptionAddress, mod,
           er->NumberParameters > 0 ? (unsigned long)er->ExceptionInformation[0] : 0ul,
           er->NumberParameters > 1 ? (unsigned long)er->ExceptionInformation[1] : 0ul,
           GetCurrentThreadId());
    bb_log("VEH     eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx",
           c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    return EXCEPTION_CONTINUE_SEARCH;
}

// Главный поток — на самых быстрых ядрах. На Legion Go 2 (Z2 Extreme: 3 Zen5 до 5,1 ГГц + 5 Zen5c до 3,3 ГГц)
// scx_lavd держал его на Zen5c (1,3–2,1 ГГц на 10 Вт): после тика логика + пара + отрисовка не влезали в vblank,
// 13 пропусков/с в тяжёлом месте L01A; на Zen5 — 2,6 ГГц и 2 пропуска/с при тех же 10 Вт (замер 2026-10-17).
// Ядра — по cpuinfo_max_freq Linux (Z: → /); бит маски Wine = номер CPU Linux.
static void pin_big_cores(void)
{
    DWORD fq[32], top = 0;
    DWORD_PTR mask = 0;
    int n = 0;
    for (int i = 0; i < 32; i++) {
        char path[96], buf[24] = { 0 };
        DWORD got = 0;
        fq[i] = 0;
        wsprintfA(path, "Z:\\sys\\devices\\system\\cpu\\cpu%d\\cpufreq\\cpuinfo_max_freq", i);
        HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (f == INVALID_HANDLE_VALUE)
            continue;
        if (ReadFile(f, buf, sizeof buf - 1, &got, NULL))
            for (char *p = buf; *p >= '0' && *p <= '9'; p++)
                fq[i] = fq[i] * 10 + (DWORD)(*p - '0');
        CloseHandle(f);
        if (fq[i] > top)
            top = fq[i];
        n += fq[i] > 0;
    }
    for (int i = 0; i < 32; i++)
        if (fq[i] && fq[i] >= top / 100 * 97)
            mask |= (DWORD_PTR)1 << i;
    int nb = 0;
    for (DWORD_PTR m = mask; m; m &= m - 1)
        nb++;
    if (!top || nb == n) {
        bb_log("big_cores: ядра одинаковые или частоты не прочитаны (CPU %d, макс %lu кГц) — не трогаем", n, (unsigned long)top);
        return;
    }
    DWORD_PTR old = SetThreadAffinityMask(GetCurrentThread(), mask);
    bb_log("big_cores: главный поток на %d из %d CPU (маска %#lx, %lu МГц), было %#lx", nb, n, (unsigned long)mask,
           (unsigned long)(top / 1000), (unsigned long)old);
}

void bb_late_init(void)
{
    const char *(CDECL *wine_get_version)(void) =
        (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version");

    bb_log("LateInit: tid=%lu wine=%s dll=%p log_gpa=%d veh=%d probes=%d borderless=%d pacer_sleep=%d video_guard=%d "
           "render=%d msaa=%d vsync=%d", GetCurrentThreadId(), wine_get_version ? wine_get_version() : "-",
           (void *)bb_self, bb_cfg.log_gpa, bb_cfg.veh, bb_cfg.probes, bb_cfg.borderless, bb_cfg.pacer_sleep,
           bb_cfg.video_guard, bb_cfg.render, bb_cfg.msaa, bb_cfg.vsync);
    bb_log("LateInit: cmdline=%s", GetCommandLineA());
    if (bb_cfg.veh && !AddVectoredExceptionHandler(1, veh_logger))
        bb_log("LateInit: AddVectoredExceptionHandler failed");

    bb_iat_hook(BB_IAT_GETPROCADDRESS, (void *)hook_GetProcAddress, (void **)&real_GetProcAddress);
    bb_iat_hook(BB_IAT_LOADLIBRARYA, (void *)hook_LoadLibraryA, (void **)&real_LoadLibraryA);
    bb_iat_hook(BB_IAT_FREELIBRARY, (void *)hook_FreeLibrary, (void **)&real_FreeLibrary);
    if (bb_cfg.big_cores)
        pin_big_cores();
    // Windows: таймер 1 мс — иначе сон пейсера (NtDelayExecution) идёт шагами по 15,6 мс и срывает кадры; под Wine без эффекта
    UINT (WINAPI *time_begin_period)(UINT) = (void *)GetProcAddress(LoadLibraryA("winmm.dll"), "timeBeginPeriod");
    if (time_begin_period)
        time_begin_period(1);
    bb_pacer_init();
    bb_window_init();
    bb_menu_guard_init();
    bb_edges_init();
    bb_input_init();
    if (bb_cfg.probes)
        bb_probe_init();
    bb_log("LateInit: done");
}
