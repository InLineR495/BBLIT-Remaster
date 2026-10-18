// Рекордер GL-вызовов движка (P5) и захват снимка кадра (M3).
// GetProcAddress движка (I3) отдаёт вместо каждой функции opengl32 заглушку
// «inc dword [calls]; jmp dword [target]» — счётчик без знания арности (гистограмма по всем 360).
// target — реальная функция или наша stdcall-обёртка точной арности.
// Обёртки всегда ведут теневое состояние GL движка (enable-биты, смешивание, альфа-тест, глубина, ortho,
// текстура, текущие цвет и UV). Между bb_rec_begin/bb_rec_end (внутри обёртки C8) примитивы пишутся в снимок;
// при bb_gl_sink движковые glBegin/glEnd/glVertex/glColor/glTexCoord/glClear/glFlush/glFinish в драйвер не идут
// (состояние и текстуры идут как обычно — объекты движка и его контекст остаются такими, как в оригинале).
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "glrec.h"
#include "log.h"
#include "remaster.h"
#include "mark.h"
#include "render.h"
#include "window.h"
#include "splash.h"

#define POOL_BASE  0x734de0u
#define POOL_END   0x7bd960u     // 35 000 × 16

struct glfn {
    char name[40];
    volatile uint32_t calls;
    void *real;
    void *target;
    void **real_out;             // куда обёртке положить реальный указатель
};

static struct glfn fns[400];
static int nfns;
static uint8_t *stubs;

// 36 функций, которые движок вызывает (notes/infra.md §3.3); остальные — ловушки.
static const char *const used36[] = {
    "glAlphaFunc", "glBegin", "glBindTexture", "glBlendFunc", "glClear", "glClearColor", "glColor3f", "glColor4f",
    "glDeleteTextures", "glDepthFunc", "glDepthRange", "glDisable", "glEnable", "glEnd", "glFinish", "glFlush",
    "glGetError", "glGetString", "glHint", "glLoadIdentity", "glMatrixMode", "glOrtho", "glPolygonMode",
    "glShadeModel", "glTexCoord2f", "glTexImage2D", "glTexParameterf", "glVertex3f", "glVertex3fv", "glViewport",
    "wglChoosePixelFormat", "wglCreateContext", "wglDeleteContext", "wglMakeCurrent", "wglSetPixelFormat",
    "wglSwapBuffers",
};

// --- теневое состояние движка ---
static struct {
    unsigned caps;               // BB_CAP_*
    unsigned bsrc, bdst, afunc, dfunc, shade, tex, matrix_mode;
    float aref;
    double ortho[6];             // l r b t n f текущей проекции (identity = −1 1 −1 1 1 −1)
    uint32_t rgba;
    float u, v;
} sh = { 0, 1, 0, 0x207, 0x201, 0x1d01, 0, 0x1700, 0.0f, { -1, 1, -1, 1, 1, -1 }, 0xffffffffu, 0, 0 };

// --- запись ---
int bb_gl_sink;
static int recording, in_begin;
static unsigned cur_mode, block_verts, block_state, prim_fill;
static struct bb_rec_stats st;
static uint32_t prim_total, bind_logged, upload_logged;
// три буфера: запись, последний и предыдущий опубликованные (снимок ~3 МБ)
static struct bb_snapshot snaps[3];
static struct bb_snapshot *rec_snap = &snaps[0], *latest, *prev;
#define snap (*rec_snap)
int bb_swap_suppress;

static void (WINAPI *r_glBegin)(unsigned);
static void (WINAPI *r_glEnd)(void);
static void (WINAPI *r_glVertex3fv)(const float *);
static void (WINAPI *r_glVertex3f)(float, float, float);
static void (WINAPI *r_glColor3f)(float, float, float);
static void (WINAPI *r_glColor4f)(float, float, float, float);
static void (WINAPI *r_glTexCoord2f)(float, float);
static void (WINAPI *r_glClear)(unsigned);
static void (WINAPI *r_glFlush)(void);
static void (WINAPI *r_glFinish)(void);
static void (WINAPI *r_glEnable)(unsigned);
static void (WINAPI *r_glDisable)(unsigned);
static void (WINAPI *r_glBlendFunc)(unsigned, unsigned);
static void (WINAPI *r_glAlphaFunc)(unsigned, float);
static void (WINAPI *r_glDepthFunc)(unsigned);
static void (WINAPI *r_glShadeModel)(unsigned);
static void (WINAPI *r_glMatrixMode)(unsigned);
static void (WINAPI *r_glLoadIdentity)(void);
static void (WINAPI *r_glOrtho)(double, double, double, double, double, double);
static void (WINAPI *r_glBindTexture)(unsigned, unsigned);
static void (WINAPI *r_glTexImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
static void (WINAPI *r_glDeleteTextures)(int, const unsigned *);
static BOOL (WINAPI *r_wglSwapBuffers)(HDC);
static BOOL (WINAPI *r_wglDeleteContext)(HGLRC);
static void (WINAPI *r_glViewport)(int, int, int, int);
static const char *(WINAPI *r_glGetString)(unsigned);

// Профиль драйвера движка (0x422050) выбирается по glGetString(VENDOR/RENDERER): подмена строки — эксперимент
// с режимами смешивания (bb_cfg.gl_profile: −1 — как есть, иначе индекс в таблице ниже). Применяется на Reinit.
static const char *const profile_names[] = { "3Dfx", "RENDITION", "NEC", "", "NVIDIA", "ATI", "Direct3D", "Real3D" };

static const char *WINAPI w_glGetString(unsigned name)
{
    int p = bb_cfg.gl_profile;
    if ((name == 0x1f00 || name == 0x1f01) && p >= 0 && p < 8 && profile_names[p][0]) {
        bb_log("glGetString(%#x) для движка -> \"%s\" (профиль %d)", name, profile_names[p], p);
        return profile_names[p];
    }
    return r_glGetString(name);
}
LARGE_INTEGER bb_swap_t0, bb_swap_t1;

static unsigned cap_bit(unsigned cap)
{
    switch (cap) {
    case 0x0de1: return BB_CAP_TEXTURE;
    case 0x0be2: return BB_CAP_BLEND;
    case 0x0bc0: return BB_CAP_ALPHA;
    case 0x0b71: return BB_CAP_DEPTH;
    case 0x0b44: return BB_CAP_CULL;
    case 0x0b60: return BB_CAP_FOG;
    default: return 0;
    }
}

// Состояние на момент glBegin; подряд идущие одинаковые не дублируются.
static unsigned capture_state(void)
{
    struct bb_state s;
    memset(&s, 0, sizeof s);
    s.caps = sh.caps;
    s.tex = sh.tex;
    s.bsrc = sh.bsrc;
    s.bdst = sh.bdst;
    s.afunc = sh.afunc;
    s.aref = sh.aref;
    s.dfunc = sh.dfunc;
    s.shade = sh.shade;
    memcpy(s.ortho, sh.ortho, sizeof s.ortho);
    if (snap.nstates && !memcmp(&snap.states[snap.nstates - 1], &s, sizeof s))
        return snap.nstates - 1;
    if (snap.nstates >= BB_SNAP_MAX_STATES) {
        snap.overflow = 1;
        return snap.nstates - 1;
    }
    snap.states[snap.nstates] = s;
    return snap.nstates++;
}

static void put_vertex(float x, float y, float z, uint8_t tag, uint32_t obj, uint16_t local, uint8_t fl)
{
    unsigned per = cur_mode == 4 ? 3 : cur_mode == 7 ? 4 : 0;
    if (!per)
        return;
    if (snap.nverts >= BB_SNAP_MAX_VERTS || (prim_fill == 0 && snap.nprims >= BB_SNAP_MAX_PRIMS)) {
        snap.overflow = 1;
        return;
    }
    if (prim_fill == 0) {
        struct bb_prim *p = &snap.prims[snap.nprims];
        p->first = snap.nverts;
        p->state = (uint16_t)block_state;
        p->nverts = (uint8_t)per;
    }
    struct bb_vert *v = &snap.verts[snap.nverts++];
    v->x = x; v->y = y; v->z = z;
    v->u = sh.u; v->v = sh.v;
    v->rgba = sh.rgba;
    v->tag = tag;
    v->obj = obj;
    v->local = local;
    v->fl = fl;
    if (++prim_fill == per) {
        prim_fill = 0;
        snap.nprims++;
    }
}

static void close_block(void)
{
    if (cur_mode == 4) {          // GL_TRIANGLES
        st.tris += block_verts / 3;
        prim_total += block_verts / 3;
        st.leftover += block_verts % 3;
    } else if (cur_mode == 7) {   // GL_QUADS
        st.quads += block_verts / 4;
        prim_total += block_verts / 4;
        st.leftover += block_verts % 4;
    } else {
        st.other_mode_verts += block_verts;
    }
    if (prim_fill) {              // недописанный примитив — отбросить
        snap.nverts -= prim_fill;
        prim_fill = 0;
    }
}

static void WINAPI w_glBegin(unsigned mode)
{
    if (recording) {
        if (in_begin) {
            st.ignored_begin++;   // glBegin внутри Begin/End — GL_INVALID_OPERATION, вызов игнорируется
        } else {
            in_begin = 1;
            cur_mode = mode;
            block_verts = 0;
            prim_fill = 0;
            block_state = capture_state();
            st.blocks++;
        }
    }
    if (!bb_gl_sink)
        r_glBegin(mode);
}

static void WINAPI w_glEnd(void)
{
    if (recording) {
        if (!in_begin)
            st.ignored_end++;
        else {
            close_block();
            in_begin = 0;
        }
    }
    if (!bb_gl_sink)
        r_glEnd();
}

static void WINAPI w_glVertex3fv(const float *p)
{
    if (recording && in_begin) {
        block_verts++;
        if ((uintptr_t)p >= POOL_BASE && (uintptr_t)p < POOL_END && (((uintptr_t)p - POOL_BASE) & 15) == 0) {
            uint32_t idx = ((uintptr_t)p - POOL_BASE) >> 4;
            uint8_t tag = bb_pool_tag[idx];
            st.pool_verts++;
            st.tag_refs[tag]++;
            put_vertex(p[0], p[1], p[2], tag, bb_pool_obj[idx], bb_pool_local[idx], bb_pool_fl[idx]);
        } else {
            st.other_verts++;
            put_vertex(p[0], p[1], p[2], BB_TAG_IMM, 0, 0, 0);
        }
    }
    if (!bb_gl_sink)
        r_glVertex3fv(p);
}

static void WINAPI w_glVertex3f(float x, float y, float z)
{
    if (recording && in_begin) {
        block_verts++;
        st.imm_verts++;
        put_vertex(x, y, z, BB_TAG_IMM, 0, 0, 0);
    }
    if (!bb_gl_sink)
        r_glVertex3f(x, y, z);
}

static uint32_t pack_rgba(float r, float g, float b, float a)
{
    float c[4] = { r, g, b, a };
    uint32_t out = 0;
    for (int i = 0; i < 4; i++) {
        float f = c[i] < 0 ? 0 : c[i] > 1 ? 1 : c[i];
        out |= (uint32_t)(f * 255.0f + 0.5f) << (8 * i);
    }
    return out;
}

static void WINAPI w_glColor3f(float r, float g, float b)
{
    sh.rgba = pack_rgba(r, g, b, 1.0f);
    if (!bb_gl_sink)
        r_glColor3f(r, g, b);
}

static void WINAPI w_glColor4f(float r, float g, float b, float a)
{
    sh.rgba = pack_rgba(r, g, b, a);
    if (!bb_gl_sink)
        r_glColor4f(r, g, b, a);
}

static void WINAPI w_glTexCoord2f(float u, float v)
{
    sh.u = u;
    sh.v = v;
    if (!bb_gl_sink)
        r_glTexCoord2f(u, v);
}

static void WINAPI w_glClear(unsigned mask)
{
    if (!bb_gl_sink)
        r_glClear(mask);
}

static void WINAPI w_glFlush(void)
{
    if (!bb_gl_sink)
        r_glFlush();
}

static void WINAPI w_glFinish(void)
{
    if (!bb_gl_sink)
        r_glFinish();
}

static void WINAPI w_glEnable(unsigned cap)
{
    sh.caps |= cap_bit(cap);
    r_glEnable(cap);
}

static void WINAPI w_glDisable(unsigned cap)
{
    sh.caps &= ~cap_bit(cap);
    r_glDisable(cap);
}

static void WINAPI w_glBlendFunc(unsigned s, unsigned d)
{
    sh.bsrc = s;
    sh.bdst = d;
    r_glBlendFunc(s, d);
}

static void WINAPI w_glAlphaFunc(unsigned f, float ref)
{
    sh.afunc = f;
    sh.aref = ref;
    r_glAlphaFunc(f, ref);
}

static void WINAPI w_glDepthFunc(unsigned f)
{
    sh.dfunc = f;
    r_glDepthFunc(f);
}

static void WINAPI w_glShadeModel(unsigned m)
{
    sh.shade = m;
    r_glShadeModel(m);
}

static void WINAPI w_glMatrixMode(unsigned m)
{
    sh.matrix_mode = m;
    r_glMatrixMode(m);
}

static void WINAPI w_glLoadIdentity(void)
{
    if (sh.matrix_mode == 0x1701) {   // GL_PROJECTION
        double id[6] = { -1, 1, -1, 1, 1, -1 };
        memcpy(sh.ortho, id, sizeof id);
    }
    r_glLoadIdentity();
}

static void WINAPI w_glOrtho(double l, double r, double b, double t, double n, double f)
{
    if (sh.matrix_mode == 0x1701) {   // движок зовёт glOrtho только после LoadIdentity проекции
        sh.ortho[0] = l; sh.ortho[1] = r; sh.ortho[2] = b;
        sh.ortho[3] = t; sh.ortho[4] = n; sh.ortho[5] = f;
    }
    r_glOrtho(l, r, b, t, n, f);
}

static void WINAPI w_glViewport(int x, int y, int w, int h)
{
    bb_window_viewport(&x, &y, &w, &h);   // C12
    r_glViewport(x, y, w, h);
}

static void WINAPI w_glBindTexture(unsigned target, unsigned id)
{
    sh.tex = id;
    if ((id < 1 || id > 0x7cf) && (st.canary_bind++, bind_logged++ < 5))  // канарейка G1d
        bb_log("G1d: glBindTexture(%#x, %u) вне [1, 0x7cf]", target, id);
    r_glBindTexture(target, id);
}

uint32_t bb_tex_seq[0x800];          // имя текстуры → порядковый номер её последней загрузки (уровень 0)
static uint32_t tex_seq_n;

static void WINAPI w_glTexImage2D(unsigned target, int level, int ifmt, int w, int h, int border, unsigned fmt,
                                  unsigned type, const void *px)
{
    st.tex_uploads++;
    if (level == 0 && sh.tex < 0x800)
        bb_tex_seq[sh.tex] = ++tex_seq_n;
    if ((sh.tex < 1 || sh.tex > 0x7cb) && (st.canary_upload++, upload_logged++ < 5))  // канарейка G1e
        bb_log("G1e: glTexImage2D в имя %u вне [1, 0x7cb] (%dx%d)", sh.tex, w, h);
    r_glTexImage2D(target, level, ifmt, w, h, border, fmt, type, px);
}

static void WINAPI w_glDeleteTextures(int n, const unsigned *ids)
{
    st.tex_deletes += n > 0 ? (unsigned)n : 0;
    r_glDeleteTextures(n, ids);
}

// M4: C8 опубликовал снимок → своп движка пропустить; пейсер подтверждает, что выведет сам
static int swap_pending;
void bb_swap_suppress_arm(void)
{
    bb_swap_suppress = 1;
    swap_pending = 1;
}

int bb_swap_suppress_pending(void)
{
    return swap_pending;
}

void bb_swap_suppress_done(void)
{
    swap_pending = 0;
}

BOOL bb_gl_real_swap(HDC dc)
{
    return r_wglSwapBuffers ? r_wglSwapBuffers(dc) : FALSE;
}

static BOOL WINAPI w_wglSwapBuffers(HDC dc)
{
    LARGE_INTEGER t0, t1, f;
    if (bb_swap_suppress) {          // M4: кадр этого тика выводит наш цикл в пейсере
        bb_swap_suppress = 0;
        st.swaps_skipped++;
        return TRUE;
    }
    if (bb_cfg.splash_fill && bb_cfg.borderless && bb_cfg.render && !latest)
        bb_splash_fill();
    QueryPerformanceCounter(&t0);
    BOOL ok = r_wglSwapBuffers(dc);
    QueryPerformanceCounter(&t1);
    bb_swap_t0 = t0;
    bb_swap_t1 = t1;
    QueryPerformanceFrequency(&f);
    uint32_t us = (uint32_t)((t1.QuadPart - t0.QuadPart) * 1000000 / f.QuadPart);   // P11: блокировка свопа
    st.swaps++;
    st.swap_us_sum += us;
    if (us > st.swap_us_max)
        st.swap_us_max = us;
    if (us > 20000)
        st.swap_slow++;
    return ok;
}

// C15: контекст движка удаляется (Reinit) — сначала удалить наш, он в одной share-группе.
static BOOL WINAPI w_wglDeleteContext(HGLRC ctx)
{
    bb_render_engine_context_deleted(ctx);
    return r_wglDeleteContext(ctx);
}

static const struct { const char *name; void *wrapper; void **real; } wrappers[] = {
    { "glBegin", (void *)w_glBegin, (void **)&r_glBegin },
    { "glEnd", (void *)w_glEnd, (void **)&r_glEnd },
    { "glVertex3fv", (void *)w_glVertex3fv, (void **)&r_glVertex3fv },
    { "glVertex3f", (void *)w_glVertex3f, (void **)&r_glVertex3f },
    { "glColor3f", (void *)w_glColor3f, (void **)&r_glColor3f },
    { "glColor4f", (void *)w_glColor4f, (void **)&r_glColor4f },
    { "glTexCoord2f", (void *)w_glTexCoord2f, (void **)&r_glTexCoord2f },
    { "glClear", (void *)w_glClear, (void **)&r_glClear },
    { "glFlush", (void *)w_glFlush, (void **)&r_glFlush },
    { "glFinish", (void *)w_glFinish, (void **)&r_glFinish },
    { "glEnable", (void *)w_glEnable, (void **)&r_glEnable },
    { "glDisable", (void *)w_glDisable, (void **)&r_glDisable },
    { "glBlendFunc", (void *)w_glBlendFunc, (void **)&r_glBlendFunc },
    { "glAlphaFunc", (void *)w_glAlphaFunc, (void **)&r_glAlphaFunc },
    { "glDepthFunc", (void *)w_glDepthFunc, (void **)&r_glDepthFunc },
    { "glShadeModel", (void *)w_glShadeModel, (void **)&r_glShadeModel },
    { "glMatrixMode", (void *)w_glMatrixMode, (void **)&r_glMatrixMode },
    { "glLoadIdentity", (void *)w_glLoadIdentity, (void **)&r_glLoadIdentity },
    { "glOrtho", (void *)w_glOrtho, (void **)&r_glOrtho },
    { "glBindTexture", (void *)w_glBindTexture, (void **)&r_glBindTexture },
    { "glTexImage2D", (void *)w_glTexImage2D, (void **)&r_glTexImage2D },
    { "glDeleteTextures", (void *)w_glDeleteTextures, (void **)&r_glDeleteTextures },
    { "wglSwapBuffers", (void *)w_wglSwapBuffers, (void **)&r_wglSwapBuffers },
    { "wglDeleteContext", (void *)w_wglDeleteContext, (void **)&r_wglDeleteContext },
    { "glViewport", (void *)w_glViewport, (void **)&r_glViewport },
    { "glGetString", (void *)w_glGetString, (void **)&r_glGetString },
};

void *bb_gl_intercept(const char *name, void *real)
{
    struct glfn *e = NULL;
    uint8_t *s;

    if (!real || lstrlenA(name) >= (int)sizeof fns[0].name)
        return real;
    for (int i = 0; i < nfns; i++)
        if (lstrcmpA(fns[i].name, name) == 0)
            e = &fns[i];
    if (!e) {
        if (nfns == (int)(sizeof fns / sizeof fns[0]))
            return real;
        if (!stubs && !(stubs = VirtualAlloc(NULL, 400 * 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)))
            return real;
        e = &fns[nfns++];
        lstrcpyA(e->name, name);
        for (size_t k = 0; k < sizeof wrappers / sizeof wrappers[0]; k++)
            if (lstrcmpA(wrappers[k].name, name) == 0) {
                e->target = wrappers[k].wrapper;
                e->real_out = wrappers[k].real;
            }
    }
    e->real = real;
    if (e->real_out)
        *e->real_out = real;
    else
        e->target = real;
    s = stubs + 16 * (e - fns);
    s[0] = 0xff; s[1] = 0x05;                                   // inc dword [&e->calls]
    *(uint32_t *)(s + 2) = (uint32_t)(uintptr_t)&e->calls;
    s[6] = 0xff; s[7] = 0x25;                                   // jmp dword [&e->target]
    *(uint32_t *)(s + 8) = (uint32_t)(uintptr_t)&e->target;
    FlushInstructionCache(GetCurrentProcess(), s, 12);
    return s;
}

void bb_rec_begin(void)
{
    recording = 1;
    in_begin = 0;
    prim_fill = 0;
    snap.nverts = snap.nprims = snap.nstates = 0;
    snap.overflow = 0;
}

void bb_rec_end(void)
{
    if (in_begin) {               // Begin без End в конце отрисовки — посчитать и закрыть
        close_block();
        st.unclosed++;
    }
    recording = 0;
    in_begin = 0;
}

const struct bb_snapshot *bb_rec_snapshot(void)
{
    return rec_snap;
}

void bb_rec_publish(uint32_t build_id, uint32_t level_gen, double fcull)
{
    LARGE_INTEGER now, f;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&f);
    rec_snap->t = (double)now.QuadPart / (double)f.QuadPart;
    rec_snap->build_id = build_id;
    rec_snap->level_gen = level_gen;
    rec_snap->fcull = fcull;
    for (int i = 0; i < 9; i++)
        rec_snap->cam[i] = *(volatile int16_t *)(uintptr_t)(0x4efae0u + 2 * i) / 4096.0;
    for (int i = 0; i < 3; i++)     // [0x4efb20] — −eye
        rec_snap->eye[i] = -(double)*(volatile int32_t *)(uintptr_t)(0x4efb20u + 4 * i);
    bb_fog_params(&rec_snap->fog_d, &rec_snap->fog_m);
    uint32_t pl = *(volatile uint32_t *)(uintptr_t)0x4b324cu;
    for (int i = 0; i < 3; i++)
        rec_snap->ply[i] = pl > 0x10000 ? (double)*(volatile int32_t *)(uintptr_t)(pl + 0xe4 + 4 * i) : 0.0;
    prev = latest;
    latest = rec_snap;
    for (int i = 0; i < 3; i++)        // следующий буфер записи — тот, что не latest и не prev
        if (&snaps[i] != latest && &snaps[i] != prev) {
            rec_snap = &snaps[i];
            break;
        }
}

const struct bb_snapshot *bb_snap_latest(void)
{
    return latest;
}

const struct bb_snapshot *bb_snap_prev(void)
{
    return prev;
}

uint32_t bb_rec_prim_count(void)
{
    return prim_total;
}

void bb_rec_take(struct bb_rec_stats *out)
{
    *out = st;
    memset(&st, 0, sizeof st);
}

// Гистограмма: вызванные функции; «ЛОВУШКА» — вызвана функция вне списка 36.
void bb_rec_log_histogram(const char *why)
{
    char line[900];
    int len = 0, traps = 0;
    for (int i = 0; i < nfns; i++) {
        int known = 0;
        if (!fns[i].calls)
            continue;
        for (size_t k = 0; k < sizeof used36 / sizeof used36[0]; k++)
            known |= lstrcmpA(used36[k], fns[i].name) == 0;
        if (!known) {
            traps++;
            bb_log("P5 ЛОВУШКА: %s вызвана %lu раз", fns[i].name, (unsigned long)fns[i].calls);
        }
        if (len < (int)sizeof line - 60)
            len += _snprintf(line + len, sizeof line - len, "%s=%lu ", fns[i].name, (unsigned long)fns[i].calls);
    }
    bb_log("P5 гистограмма (%s): функций в таблице %d, ловушек %d: %s", why, nfns, traps, len ? line : "-");
}
