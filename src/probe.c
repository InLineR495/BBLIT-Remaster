// M1a — пассивные пробы, игра не меняется (только чтение глобалов и замеры времени).
//   P1     — темп: FrameTick, пейсер, построения/пропуски, Δ[0x4b24a4] в секунду;
//   G4-M   — ёмкость кадра: N_pre по формуле G4-S, [0x604130] после оригинала, пул, арена, примитивы;
//   T-G4-2 — N_pre == [0x604130] при N_pre ≤ 2000; T-G4-4 — где лежит курсор арены; T-G4-5 — время 0x4124b0;
//   P2     — строки GL, профиль, расширения, пиксельный формат (один раз, в контексте движка);
//   G4-E   — пик арены по концам записи эмиттеров tri/quad (в случае A курсор на входе C8 уже в BZE);
//   P3     — повторы штампа кэша матриц [0x7c6248] на соседних построениях;
//   P5     — рекордер GL (glrec.c): примитивы за отрисовку против [0x4e5e40], гистограмма функций.
// Раз в секунду — строка «P1 …» в лог; при загрузке уровня и на выходе — пики уровня «LVL …».
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "glrec.h"
#include "hook.h"
#include "mark.h"
#include "probe_cam.h"
#include "render.h"
#include "edges.h"
#include "pacer.h"
#include "log.h"
#include "remaster.h"

#define RD32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define RD16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define RD8(a)  (*(volatile uint8_t *)(uintptr_t)(a))

// Глобалы движка (DESIGN §1.10, §1.16, §2.2, §2.9)
#define G_LOGIC_CLOCK   0x4b24a4u   // +2 за тик по статике, «3/с» по старому замеру — проверяем
#define G_BUILDS        0x4b18fcu   // inc на каждом построении (0x405f04)
#define G_SKIPS         0x4b18f8u   // inc на тике без построения (0x405e69 ветка)
#define G_SKIP_FLAG     0x4b1d00u
#define G_PRIMS         0x4e5e40u
#define G_ARENA         0x4e5e44u   // malloc(1 000 000)
#define G_ARENA_CUR     0x4efb8cu
#define G_POOL_IDX      0x623654u   // пул вершин, ёмкость 35 000
#define G_SORT_N        0x604130u   // записей сортировки, ёмкость 2000
#define G_TEX_MAX       0x4b1c90u
#define G_BZE_BASE      0x4b2364u
#define G_BZE_SIZE      0x4b38a4u
#define G_BZE_LIMIT     0x4b372cu
#define G_PSXRAM        0x52fd00u
#define G_FOGDIST       0x9ca83cu
#define G_LOADING       0x4b3e48u
#define G_WORKM_STAMP   0x7c6248u   // GetTickCount на построение (0x40a1f2)
#define ARENA_BYTES     1000000u
#define SORT_CAP        2000u
#define POOL_CAP        35000u

struct peaks {
    uint32_t n_pre, n_post, pool, prims, arena, draw_us;
    uint32_t over_sort, mismatch, walk_err, cur_outside, draws;
    uint32_t emit_arena, emit_bze, emit_other, stamp_repeat;
    uint32_t rec_mismatch;
};

static LARGE_INTEGER qpf, sec_start;
static LARGE_INTEGER ts_ft0, ts_ft1, ts_c80, ts_c81;   // метки частей итерации (разбивка статтеров)
static struct peaks sec, lvl;
static uint32_t sec_ticks, sec_pacer, sec_gate;
static uint64_t sec_draw_us_sum;
static uint32_t prev_clock, prev_builds, prev_skips;
static uint32_t lvl_index, prev_stamp;
uint32_t bb_level_loads(void) { return lvl_index; }   // счётчик загрузок уровня (LVL #)
static int gl_logged;

uint32_t bb_last_pool, bb_last_arena, bb_last_nsort;   // M5b: ёмкость прошлого построения (страховка дальности)

static uint32_t level_id(void)
{
    uint32_t ram = RD32(G_PSXRAM);
    return ram ? RD32(ram + 0x10000) : 0xffffffffu;
}

// Узел OT: в арене или в BZE-области, выравнивание 4 (валидатор G4-S).
static int node_ok(uint32_t p)
{
    uint32_t a = RD32(G_ARENA), b = RD32(G_BZE_BASE), bl = RD32(G_BZE_SIZE);
    if (p & 3)
        return 0;
    return (a && p >= a && p < a + ARENA_BYTES) || (b && p >= b && p < b + bl);
}

static uint32_t list_len(uint32_t head, uint32_t link, uint32_t *err)
{
    uint32_t n = 0, p = RD32(head);
    while (p) {
        if (!node_ok(p) || n >= 20000) {
            (*err)++;
            break;
        }
        n++;
        p = RD32(p + link);
    }
    return n;
}

// Формула G4-S: tri-списки (+0, +8) связаны через +0xc, quad-списки (+4, +0xc) — через +0x10.
static uint32_t count_sort_entries(uint32_t *err)
{
    uint32_t n = 0, tmax = RD32(G_TEX_MAX);
    if (tmax > 0x7cf)
        tmax = 0x7cf;
    for (uint32_t t = 1; t <= tmax; t++) {
        uint32_t ot = 0x4e7dc0u + 16 * t;
        if (RD16(0x621620u + 2 * t) == 1)
            n += list_len(ot + 8, 0xc, err) + list_len(ot + 0xc, 0x10, err);
        if (RD16(0x622600u + 2 * t) == 1 && RD8(0x52fd69u + 48 * t) != 0)
            n += list_len(ot + 0, 0xc, err) + list_len(ot + 4, 0x10, err);
    }
    return n;
}

static void peak(uint32_t *dst, uint32_t v)
{
    if (v > *dst)
        *dst = v;
}

static void merge(struct peaks *d, const struct peaks *s)
{
    peak(&d->n_pre, s->n_pre); peak(&d->n_post, s->n_post); peak(&d->pool, s->pool);
    peak(&d->prims, s->prims); peak(&d->arena, s->arena); peak(&d->draw_us, s->draw_us);
    d->over_sort += s->over_sort; d->mismatch += s->mismatch; d->walk_err += s->walk_err;
    d->cur_outside += s->cur_outside; d->draws += s->draws;
    peak(&d->emit_arena, s->emit_arena); peak(&d->emit_bze, s->emit_bze);
    d->emit_other += s->emit_other; d->stamp_repeat += s->stamp_repeat;
    d->rec_mismatch += s->rec_mismatch;
}

static void log_level_summary(const char *why)
{
    if (!lvl.draws)
        return;
    bb_rec_log_histogram(why);
    bb_log("LVL #%u id=%08x (%s): draws=%u | N_pre max=%u (%.0f%%) post max=%u >2000=%u mism=%u werr=%u | "
           "pool max=%u (%.0f%%) prims max=%u emit arena max=%u (%.0f%%) bze max=%u other=%u | stamp_rep=%u rec_mism=%u | draw max=%.2f мс",
           lvl_index, level_id(), why, lvl.draws, lvl.n_pre, 100.0 * lvl.n_pre / SORT_CAP, lvl.n_post,
           lvl.over_sort, lvl.mismatch, lvl.walk_err, lvl.pool, 100.0 * lvl.pool / POOL_CAP, lvl.prims,
           lvl.emit_arena, 100.0 * lvl.emit_arena / ARENA_BYTES, lvl.emit_bze, lvl.emit_other, lvl.stamp_repeat, lvl.rec_mismatch,
           lvl.draw_us / 1000.0);
}

// --- P2: GL в контексте движка ---

static int has_ext(const char *list, const char *name)
{
    size_t n = strlen(name);
    for (const char *p = list; p && (p = strstr(p, name)); p += n)
        if ((p == list || p[-1] == ' ') && (p[n] == ' ' || p[n] == 0))
            return 1;
    return 0;
}

static void log_gl_info(void)
{
    HMODULE gl = GetModuleHandleA("opengl32.dll");
    const char *(WINAPI *glGetString)(unsigned) = (void *)GetProcAddress(gl, "glGetString");
    void (WINAPI *glGetIntegerv)(unsigned, int *) = (void *)GetProcAddress(gl, "glGetIntegerv");
    HDC (WINAPI *wglGetCurrentDC)(void) = (void *)GetProcAddress(gl, "wglGetCurrentDC");
    PROC (WINAPI *wglGetProcAddress)(const char *) = (void *)GetProcAddress(gl, "wglGetProcAddress");
    const char *(WINAPI *wglGetExtensionsStringARB)(HDC);
    PIXELFORMATDESCRIPTOR pfd;
    int profile = -1, flags = -1, maj = -1, min = -1, aniso = -1;
    const char *ext, *wext = "";
    HDC dc;

    if (!glGetString || !glGetIntegerv || !wglGetCurrentDC || !wglGetProcAddress) {
        bb_log("P2: opengl32 не найден");
        return;
    }
    dc = wglGetCurrentDC();
    bb_log("P2: GL_VENDOR=%s", glGetString(0x1f00));
    bb_log("P2: GL_RENDERER=%s", glGetString(0x1f01));
    bb_log("P2: GL_VERSION=%s", glGetString(0x1f02));
    glGetIntegerv(0x821b, &maj);   // GL_MAJOR_VERSION
    glGetIntegerv(0x821c, &min);   // GL_MINOR_VERSION
    glGetIntegerv(0x9126, &profile); // GL_CONTEXT_PROFILE_MASK
    glGetIntegerv(0x821e, &flags); // GL_CONTEXT_FLAGS
    glGetIntegerv(0x84ff, &aniso); // GL_MAX_TEXTURE_MAX_ANISOTROPY
    ext = glGetString(0x1f03);
    wglGetExtensionsStringARB = (void *)wglGetProcAddress("wglGetExtensionsStringARB");
    if (wglGetExtensionsStringARB)
        wext = wglGetExtensionsStringARB(dc);
    bb_log("P2: version %d.%d profile_mask=%#x flags=%#x max_aniso=%d ext_len=%u", maj, min, profile, flags, aniso,
           ext ? (unsigned)strlen(ext) : 0u);
    bb_log("P2: GL ext: KHR_debug=%d ARB_framebuffer_object=%d EXT_texture_filter_anisotropic=%d ARB_texture_filter_anisotropic=%d",
           has_ext(ext, "GL_KHR_debug"), has_ext(ext, "GL_ARB_framebuffer_object"),
           has_ext(ext, "GL_EXT_texture_filter_anisotropic"), has_ext(ext, "GL_ARB_texture_filter_anisotropic"));
    bb_log("P2: WGL ext: ARB_create_context=%d ARB_create_context_profile=%d EXT_swap_control=%d EXT_swap_control_tear=%d ARB_pixel_format=%d ARB_multisample=%d",
           has_ext(wext, "WGL_ARB_create_context"), has_ext(wext, "WGL_ARB_create_context_profile"),
           has_ext(wext, "WGL_EXT_swap_control"), has_ext(wext, "WGL_EXT_swap_control_tear"),
           has_ext(wext, "WGL_ARB_pixel_format"), has_ext(wext, "WGL_ARB_multisample"));
    int pf = GetPixelFormat(dc);
    if (pf > 0 && DescribePixelFormat(dc, pf, sizeof pfd, &pfd))
        bb_log("P2: pixel format #%d: color=%u (r%u g%u b%u a%u) depth=%u stencil=%u flags=%#lx",
               pf, pfd.cColorBits, pfd.cRedBits, pfd.cGreenBits, pfd.cBlueBits, pfd.cAlphaBits,
               pfd.cDepthBits, pfd.cStencilBits, pfd.dwFlags);
    bb_log("P2: engine renderer=[0x4ac094]=%u blend=[0x4ac028/2c/30]=%#x/%#x/%#x W×H=%u×%u fogDist=%u",
           RD32(0x4ac094), RD32(0x4ac028), RD32(0x4ac02c), RD32(0x4ac030), RD32(0x467748), RD32(0x46774c),
           RD32(G_FOGDIST));
}

// --- G4-E1/E2: обёртки эмиттеров, замер конца записи (оригинал без изменений) ---

typedef uint32_t (*emit_fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

static void emit_account(uint32_t ptr, uint32_t bytes)
{
    uint32_t a = RD32(G_ARENA), b = RD32(G_BZE_BASE), bl = RD32(G_BZE_SIZE), end = ptr + bytes;
    if (a && ptr >= a && ptr <= a + ARENA_BYTES) {
        peak(&sec.emit_arena, end - a);
        peak(&lvl.emit_arena, end - a);
    } else if (b && ptr >= b && ptr <= b + bl) {
        peak(&sec.emit_bze, end - b);
        peak(&lvl.emit_bze, end - b);
    } else {
        sec.emit_other++;
        lvl.emit_other++;
    }
}

static uint32_t e1_tri(uint32_t p, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    uint32_t r = ((emit_fn)BB_FN_EMIT_TRI)(p, a1, a2, a3, a4, a5);
    emit_account(p, r);
    return r;
}

static uint32_t e2_quad(uint32_t p, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    uint32_t r = ((emit_fn)BB_FN_EMIT_QUAD)(p, a1, a2, a3, a4, a5);
    emit_account(p, r);
    return r;
}

// --- C8: обёртка слота отрисовки, вызывает оригинал без изменений ---

static void c8_draw(void)
{
    struct peaks s = { 0 };
    LARGE_INTEGER t0, t1;
    int gate = RD32(G_SKIP_FLAG) == 0 && (int32_t)RD32(G_PRIMS) > 0;
    uint32_t arena = RD32(G_ARENA), cur = RD32(G_ARENA_CUR);

    if (!gl_logged) {
        gl_logged = 1;
        log_gl_info();
        bb_p9_log_pad();
    }
    if (gate) {
        s.n_pre = count_sort_entries(&s.walk_err);
        sec_gate++;
    }
    uint32_t stamp = RD32(G_WORKM_STAMP);
    s.stamp_repeat = stamp == prev_stamp;
    prev_stamp = stamp;
    s.pool = RD32(G_POOL_IDX);
    s.prims = RD32(G_PRIMS);
    if (arena && cur >= arena && cur <= arena + ARENA_BYTES)
        s.arena = cur - arena;
    else
        s.cur_outside = 1;

    uint32_t rec0 = bb_rec_prim_count();
    int ours = gate && bb_render_ready();   // M3: геометрию движка в драйвер не пускаем, рисуем снимок сами
    QueryPerformanceCounter(&t0);
    ts_c80 = t0;
    bb_rec_begin();
    bb_gl_sink = ours;
    ((void (*)(void))BB_FN_DRAW)();
    bb_gl_sink = 0;
    bb_rec_end();
    if (ours) {
        bb_rec_publish(RD32(G_BUILDS), lvl_index, bb_hor_fcull());
        if (bb_cfg.interp && bb_cfg.pacer_sleep)
            bb_swap_suppress_arm();      // M4: кадры выводит цикл в пейсере
        else
            bb_render_frame(bb_snap_latest());
    }
    QueryPerformanceCounter(&t1);
    ts_c81 = t1;
    uint32_t rec = bb_rec_prim_count() - rec0;
    if (gate && rec != s.prims) {
        s.rec_mismatch = 1;
        if (sec.rec_mismatch + lvl.rec_mismatch < 10)
            bb_log("P5: записано примитивов %u, [0x4e5e40]=%u, N_pre=%u", rec, s.prims, s.n_pre);
    }

    s.draw_us = (uint32_t)((t1.QuadPart - t0.QuadPart) * 1000000 / qpf.QuadPart);
    sec_draw_us_sum += s.draw_us;
    s.draws = 1;
    if (gate) {
        s.n_post = RD32(G_SORT_N);
        s.over_sort = s.n_pre > SORT_CAP;
        s.mismatch = s.n_pre <= SORT_CAP && s.n_pre != s.n_post;
        if (s.mismatch && sec.mismatch + lvl.mismatch < 20)
            bb_log("T-G4-2: N_pre=%u, [0x604130]=%u", s.n_pre, s.n_post);
    }
    merge(&sec, &s);
    merge(&lvl, &s);
    bb_last_pool = s.pool;
    bb_last_arena = s.arena;
    bb_last_nsort = s.n_pre;
}

// --- Пре-хуки ---

// --- разбивка итерации главного цикла (статтеры): лог итераций > 25 мс; метки — ниже и в c8_draw ---
static uint32_t stall_logged;

static double us_between(LARGE_INTEGER a, LARGE_INTEGER b)
{
    return (double)(b.QuadPart - a.QuadPart) * 1e6 / (double)qpf.QuadPart;
}

// --- сторож: если логика тика идёт > 15 мс, приостановить главный поток и снять стек (адреса в .text exe и модуль EIP).
// Пока главный поток стоит, ничего не логируем и не берём блокировок — только копируем, потом Resume и лог.
static HANDLE main_thread;
static volatile LONG in_ft, ft_samples;

static void module_of(uint32_t a, char *out, int n)
{
    HMODULE hm;
    lstrcpynA(out, "?", n);
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(uintptr_t)a, &hm)) {
        char path[MAX_PATH];
        DWORD k = GetModuleFileNameA(hm, path, sizeof path);
        while (k > 0 && path[k - 1] != '\\' && path[k - 1] != '/')
            k--;
        lstrcpynA(out, path + k, n);
    }
}

// Похоже ли v на адрес возврата: перед ним call rel32 (E8), call [abs] (FF 15), call reg (FF D0+r) или call [reg+d8].
static int looks_like_ret(uint32_t v)
{
    uint8_t b[8];
    SIZE_T got = 0;
    if (v < 0x10000 || !ReadProcessMemory(GetCurrentProcess(), (void *)(uintptr_t)(v - 7), b, 7, &got) || got != 7)
        return 0;
    return b[2] == 0xe8 || (b[1] == 0xff && b[2] == 0x15) || (b[5] == 0xff && (b[6] & 0xf8) == 0xd0) ||
           (b[4] == 0xff && (b[5] & 0xf8) == 0x50) || (b[1] == 0xff && (b[2] & 0xf8) == 0x90);
}

static DWORD WINAPI watchdog(LPVOID arg)
{
    (void)arg;
    for (;;) {
        Sleep(4);
        if (!in_ft || ft_samples >= 4)
            continue;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double ms = (double)(now.QuadPart - ts_ft0.QuadPart) * 1000.0 / (double)qpf.QuadPart;
        if (ms < 15)
            continue;
        CONTEXT c;
        uint32_t stack[512];
        SIZE_T got = 0;
        memset(&c, 0, sizeof c);
        c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (SuspendThread(main_thread) == (DWORD)-1)
            continue;
        BOOL ok = GetThreadContext(main_thread, &c);
        if (ok)
            ReadProcessMemory(GetCurrentProcess(), (void *)(uintptr_t)c.Esp, stack, sizeof stack, &got);
        ResumeThread(main_thread);
        if (!ok)
            continue;
        ft_samples++;
        char mod[64], line[700], mods[300], m[64];
        int len = 0, mlen = 0;
        module_of(c.Eip, mod, sizeof mod);
        mods[0] = 0;
        for (unsigned i = 0; i < got / 4 && len < (int)sizeof line - 40; i++) {
            uint32_t v = stack[i];
            if (!looks_like_ret(v))
                continue;
            if (v >= BB_TEXT_VA && v < BB_TEXT_VA + BB_TEXT_SIZE) {   // адрес возврата в .text exe
                len += wsprintfA(line + len, "%x ", v);
            } else if (mlen < (int)sizeof mods - 70) {  // через какие DLL шёл вызов (по порядку, без повторов подряд)
                module_of(v, m, sizeof m);
                if (!(mlen && strstr(mods, m)))
                    mlen += wsprintfA(mods + mlen, "%s ", m);
            }
        }
        line[len] = 0;
        bb_log("WATCH логика %.1f мс: eip=%08lx (%s) | DLL: %s| exe: %s", ms, c.Eip, mod, mlen ? mods : "- ", len ? line : "-");
    }
    return 0;
}

static void on_frametick_post(int k, uint32_t *esp)
{
    (void)k; (void)esp;
    QueryPerformanceCounter(&ts_ft1);
    in_ft = 0;
}

static void on_frametick(int k, uint32_t *esp)
{
    LARGE_INTEGER now;
    (void)esp; (void)k;
    static SHORT prev_f9;
    SHORT f9 = bb_cfg.hotkeys ? GetAsyncKeyState(VK_F9) : 0;
    if ((f9 & 0x8000) && !(prev_f9 & 0x8000)) {
        bb_cfg.render = !bb_cfg.render;
        bb_log("F9: свой рендер %s", bb_cfg.render ? "ВКЛ" : "ВЫКЛ (оригинал)");
    }
    prev_f9 = f9;
    QueryPerformanceCounter(&ts_ft0);
    ft_samples = 0;
    in_ft = 1;
    sec_ticks++;
    if (RD32(BB_SLOT_DRAW) == BB_FN_DRAW) {   // Reinit переписывает слот — ставим заново
        *(volatile uint32_t *)(uintptr_t)BB_SLOT_DRAW = (uint32_t)(uintptr_t)c8_draw;
        bb_log("C8: слот [%08x] -> обёртка (уровень %08x)", BB_SLOT_DRAW, level_id());
    }
    bb_mark_install_slots();
    if (RD32(BB_SLOT_EMIT_TRI) == BB_FN_EMIT_TRI)
        *(volatile uint32_t *)(uintptr_t)BB_SLOT_EMIT_TRI = (uint32_t)(uintptr_t)e1_tri;
    if (RD32(BB_SLOT_EMIT_QUAD) == BB_FN_EMIT_QUAD)
        *(volatile uint32_t *)(uintptr_t)BB_SLOT_EMIT_QUAD = (uint32_t)(uintptr_t)e2_quad;
    QueryPerformanceCounter(&now);
    if (now.QuadPart - sec_start.QuadPart < qpf.QuadPart)
        return;
    double dt = (double)(now.QuadPart - sec_start.QuadPart) / (double)qpf.QuadPart;
    bb_log_mute = !bb_cfg.log_sec;           // тихий режим: счётчики сбрасываются, строки P* не пишутся
    uint32_t clock = RD32(G_LOGIC_CLOCK), builds = RD32(G_BUILDS), skips = RD32(G_SKIPS);
    struct bb_rec_stats r;
    bb_rec_take(&r);
    FILETIME ct, et, kt, ut;
    static uint64_t prev_cpu;
    uint64_t cpu = 0;
    if (GetProcessTimes(GetCurrentProcess(), &ct, &et, &kt, &ut))
        cpu = ((uint64_t)kt.dwHighDateTime << 32 | kt.dwLowDateTime) + ((uint64_t)ut.dwHighDateTime << 32 | ut.dwLowDateTime);
    double cpu_pct = prev_cpu ? (double)(cpu - prev_cpu) / 1e5 / dt : 0.0;   // 100 нс -> % одного ядра
    prev_cpu = cpu;
    uint32_t pw, ps, pspin, m4f, m4t;
    bb_pacer_take(&pw, &ps, &pspin);
    bb_pacer_take_m4(&m4f, &m4t);
    bb_log("P1 %.2fs lvl=%08x load=%u ticks=%u pacer=%u builds=+%u skips=+%u clock=+%d cpu=%.0f%% wait=%u sleep=%uus spin=%uus m4=%u/%u | draws=%u gate=%u "
           "N_pre=%u post=%u >2000=%u mism=%u | pool=%u prims=%u emit=%u/%u/%u stamp_rep=%u | draw avg=%.2f max=%.2f мс | "
           "rec blk=%u tri=%u quad=%u left=%u pool_v=%u other_v=%u imm_v=%u ign_beg=%u ign_end=%u mism=%u swaps=%u swap avg=%uus max=%uus slow=%u tex=%u/%u",
           dt, level_id(), RD8(G_LOADING), sec_ticks, sec_pacer, builds - prev_builds, skips - prev_skips,
           (int)(clock - prev_clock), cpu_pct, pw, ps, pspin, m4f, m4t, sec.draws, sec_gate, sec.n_pre, sec.n_post, sec.over_sort, sec.mismatch,
           sec.pool, sec.prims, sec.emit_arena, sec.emit_bze, sec.emit_other, sec.stamp_repeat,
           sec.draws ? sec_draw_us_sum / 1000.0 / sec.draws : 0.0, sec.draw_us / 1000.0,
           r.blocks, r.tris, r.quads, r.leftover + r.other_mode_verts, r.pool_verts, r.other_verts, r.imm_verts,
           r.ignored_begin, r.ignored_end, sec.rec_mismatch, r.swaps, r.swaps ? r.swap_us_sum / r.swaps : 0u, r.swap_us_max,
           r.swap_slow, r.tex_uploads, r.tex_deletes);
    {
        char tags[400];
        int len = 0;
        for (int t = 0; t < BB_TAG_COUNT; t++)
            if (r.tag_refs[t] || bb_mark_written[t])
                len += wsprintfA(tags + len, "%s=%lu/%lu ", bb_tag_name(t), (unsigned long)r.tag_refs[t],
                                 (unsigned long)bb_mark_written[t]);
        tags[len] = 0;
        bb_log("P6 ссылки/записано: %s| anomaly=%lu", tags, (unsigned long)bb_mark_anomaly);
        memset(bb_mark_written, 0, sizeof bb_mark_written);
    }
    {
        static int pool_peak;
        int cap, used = bb_part_pool_used(&cap);
        pool_peak = used > pool_peak ? used : pool_peak;
        bb_log("P13 пул частей: занято %d из %d (пик %d)", used, cap, pool_peak);
    }
    {
        extern uint32_t bb_p14_total[16], bb_p14_nokey[16], bb_p14_nomatch[16], bb_p14_jump[16];
        char line[400];
        int len = 0;
        for (int t = 0; t < 16; t++)
            if (bb_p14_total[t])
                len += wsprintfA(line + len, "%s=%lu/%lu/%lu/%lu ", t == 15 ? "imm" : bb_tag_name(t),
                                 (unsigned long)bb_p14_total[t], (unsigned long)bb_p14_nokey[t],
                                 (unsigned long)bb_p14_nomatch[t], (unsigned long)bb_p14_jump[t]);
        line[len] = 0;
        if (len)
            bb_log("P14 интерп (всего/без ключа/без пары/скачок): %s", line);
        memset(bb_p14_total, 0, sizeof bb_p14_total); memset(bb_p14_nokey, 0, sizeof bb_p14_nokey);
        memset(bb_p14_nomatch, 0, sizeof bb_p14_nomatch); memset(bb_p14_jump, 0, sizeof bb_p14_jump);
    }
    bb_cam_log_second();
    bb_fog_log_second();
    bb_input_log_second();
    bb_log_mute = 0;
    bb_config_live();
    prev_clock = clock;
    prev_builds = builds;
    prev_skips = skips;
    memset(&sec, 0, sizeof sec);
    sec_ticks = sec_pacer = sec_gate = 0;
    sec_draw_us_sum = 0;
    sec_start = now;
}

static void on_pacer(void)
{
    LARGE_INTEGER now;
    sec_pacer++;
    QueryPerformanceCounter(&now);
    if (!bb_pacer_exit_ts.QuadPart)
        return;
    double total = us_between(bb_pacer_exit_ts, now);
    if (total > 25000 && stall_logged++ < 200) {
        // метки вне итерации (часть не вызывалась) печатаются как −1
        LARGE_INTEGER s = bb_pacer_exit_ts;
        double pre = ts_ft0.QuadPart > s.QuadPart ? us_between(s, ts_ft0) : -1;
        double ft = ts_ft1.QuadPart > ts_ft0.QuadPart && ts_ft0.QuadPart > s.QuadPart ? us_between(ts_ft0, ts_ft1) : -1;
        double build = ts_c80.QuadPart > ts_ft1.QuadPart && ts_ft1.QuadPart > s.QuadPart ? us_between(ts_ft1, ts_c80) : -1;
        double draw = ts_c81.QuadPart > ts_c80.QuadPart && ts_c80.QuadPart > s.QuadPart ? us_between(ts_c80, ts_c81) : -1;
        double swap = bb_swap_t1.QuadPart > bb_swap_t0.QuadPart && bb_swap_t0.QuadPart > s.QuadPart ? us_between(bb_swap_t0, bb_swap_t1) : -1;
        double tail = bb_swap_t1.QuadPart > s.QuadPart ? us_between(bb_swap_t1, now) : -1;
        bb_log("STALL итерация %.1f мс: до логики %.1f | логика %.1f | построение %.1f | отрисовка %.1f | своп %.1f | после свопа %.1f (мс) lvl=%08x",
               total / 1000, pre / 1000, ft / 1000, build / 1000, draw / 1000, swap / 1000, tail / 1000, level_id());
    }
}

static void on_levelload(uint32_t *esp)
{
    log_level_summary("смена уровня");
    memset(&lvl, 0, sizeof lvl);
    lvl_index++;
    bb_cam_level_reset();
    bb_log("LVL #%u: загрузка (ret %08x) id=%08x | arena=[0x4e5e44]=%08x psxram=%08x bze=%08x+%u limit=[0x4b372c]=%08x fogDist=%u",
           lvl_index, esp[0], level_id(), RD32(G_ARENA), RD32(G_PSXRAM), RD32(G_BZE_BASE), RD32(G_BZE_SIZE),
           RD32(G_BZE_LIMIT), RD32(G_FOGDIST));
}

void bb_probe_exit(void)
{
    log_level_summary("выход");
}

void bb_probe_init(void)
{
    QueryPerformanceFrequency(&qpf);
    QueryPerformanceCounter(&sec_start);
    prev_clock = RD32(G_LOGIC_CLOCK);
    if (!bb_site_check(BB_SITE_WMPAINT_C8))
        return;
    bb_around_site(10, BB_SITE_FRAMETICK, on_frametick, on_frametick_post);
    if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &main_thread,
                        THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, 0))
        CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
    bb_pacer_observer = on_pacer;
    bb_detour_pre(BB_SITE_LEVELLOAD, on_levelload);
    bb_mark_init();
    bb_fog_init();
    bb_cam_init();
}
