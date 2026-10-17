// M3a — свой рендер снимка кадра (DESIGN §1.4, §1.7, упрощение для 30 Гц).
// Контекст: свой, OpenGL 4.6 compat, в share-группе с контекстом движка (тот же HDC окна), создаётся лениво
// при первой отрисовке; при удалении контекста движка (Reinit, C15) удаляется и наш.
// Правило имён (§1.4.1): glGenTextures/glCreateTextures не вызываем; резерв имён 1..0x7cf как GL_TEXTURE_2D;
// вложения FBO — renderbuffer-ы (MSAA RGBA8 + DEPTH32F) → resolve в RBO RGBA8. Вывод в окно — в КОНТЕКСТЕ ДВИЖКА:
// под Wine у каждого контекста своя поверхность окна, и blit из нашего контекста движок не показывает (проверено
// 2026-10-15: на экране оставалась заставка). RBO общий для share-группы, поэтому в контексте движка заводим свой
// FBO-объект с этим RBO, blit в окно, привязку возвращаем в 0. Своп делает движок.
// Мир: из пула движка (экранные X, Y и Z вида, float) восстанавливаются однородные координаты отсечения
//   x_c = x_ndc·d, y_c = y_ndc·d, w_c = −Z, где d = Z ≥ 0 ? Z+1 : −Z (делитель 0x40fa90),
// — отсюда перспективно-корректные UV и настоящее отсечение у камеры. Экран (HUD, текст, оверлеи) — ortho движка.
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "glrec.h"
#include "log.h"
#include "mark.h"
#include "remaster.h"
#include "render.h"
#include "window.h"
#include "edges.h"
#include "pacer.h"
#include <math.h>
#include <stddef.h>

#define GL_TEXTURE_2D 0x0de1
#define GL_BLEND 0x0be2
#define GL_ALPHA_TEST 0x0bc0
#define GL_DEPTH_TEST 0x0b71
#define GL_CULL_FACE 0x0b44
#define GL_LIGHTING 0x0b50
#define GL_FOG 0x0b60
#define GL_DITHER 0x0bd0
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_TRIANGLES 4
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_DEPTH_BUFFER_BIT 0x0100
#define GL_FRAMEBUFFER 0x8d40
#define GL_READ_FRAMEBUFFER 0x8ca8
#define GL_DRAW_FRAMEBUFFER 0x8ca9
#define GL_RENDERBUFFER 0x8d41
#define GL_COLOR_ATTACHMENT0 0x8ce0
#define GL_DEPTH_ATTACHMENT 0x8d00
#define GL_FRAMEBUFFER_COMPLETE 0x8cd5
#define GL_RGBA8 0x8058
#define GL_DEPTH_COMPONENT32F 0x8cac
#define GL_NEAREST 0x2600
#define GL_MULTISAMPLE 0x809d
#define GL_CONTEXT_PROFILE_MASK 0x9126
#define GL_MAX_SAMPLES 0x8d57
#define GL_VERSION 0x1f02

#define Z_NEAR 8.0
#define FOG_NEAREST 8000.0      // ближе туман движка не начинается (минимум по уровням ~11 700 при fogDist 2)
#define Z_FAR 65536.0

static struct {
    BOOL (WINAPI *MakeCurrent)(HDC, HGLRC);
    HGLRC (WINAPI *GetCurrentContext)(void);
    HDC (WINAPI *GetCurrentDC)(void);
    BOOL (WINAPI *DeleteContext)(HGLRC);
    PROC (WINAPI *GetProcAddress)(LPCSTR);
    void (WINAPI *Begin)(unsigned);
    void (WINAPI *End)(void);
    void (WINAPI *Vertex4f)(float, float, float, float);
    void (WINAPI *Vertex3f)(float, float, float);
    void (WINAPI *Color4ub)(uint8_t, uint8_t, uint8_t, uint8_t);
    void (WINAPI *TexCoord2f)(float, float);
    void (WINAPI *Enable)(unsigned);
    void (WINAPI *Disable)(unsigned);
    void (WINAPI *BindTexture)(unsigned, unsigned);
    void (WINAPI *BlendFunc)(unsigned, unsigned);
    void (WINAPI *AlphaFunc)(unsigned, float);
    void (WINAPI *DepthFunc)(unsigned);
    void (WINAPI *DepthMask)(unsigned char);
    void (WINAPI *PolygonOffset)(float, float);
    void (WINAPI *ShadeModel)(unsigned);
    void (WINAPI *MatrixMode)(unsigned);
    void (WINAPI *LoadIdentity)(void);
    void (WINAPI *Ortho)(double, double, double, double, double, double);
    void (WINAPI *Viewport)(int, int, int, int);
    void (WINAPI *Clear)(unsigned);
    void (WINAPI *ClearColor)(float, float, float, float);
    void (WINAPI *ClearDepth)(double);
    void (WINAPI *GetIntegerv)(unsigned, int *);
    void (WINAPI *ReadPixels)(int, int, int, int, unsigned, unsigned, void *);
    void (WINAPI *PixelStorei)(unsigned, int);
    void (WINAPI *TexImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
    void (WINAPI *TexParameteri)(unsigned, unsigned, int);
    void (WINAPI *DrawArrays)(unsigned, int, int);
    void (WINAPI *GetTexImage)(unsigned, int, unsigned, unsigned, void *);
    const char *(WINAPI *GetString)(unsigned);
    unsigned (WINAPI *GetError)(void);
    // расширения (через wglGetProcAddress в нашем контексте)
    HGLRC (WINAPI *CreateContextAttribsARB)(HDC, HGLRC, const int *);
    void (WINAPI *GenFramebuffers)(int, unsigned *);
    void (WINAPI *DeleteFramebuffers)(int, const unsigned *);
    void (WINAPI *BindFramebuffer)(unsigned, unsigned);
    void (WINAPI *FramebufferRenderbuffer)(unsigned, unsigned, unsigned, unsigned);
    unsigned (WINAPI *CheckFramebufferStatus)(unsigned);
    void (WINAPI *GenRenderbuffers)(int, unsigned *);
    void (WINAPI *DeleteRenderbuffers)(int, const unsigned *);
    void (WINAPI *BindRenderbuffer)(unsigned, unsigned);
    void (WINAPI *RenderbufferStorageMultisample)(unsigned, int, unsigned, int, int);
    void (WINAPI *BlitFramebuffer)(int, int, int, int, int, int, int, int, unsigned, unsigned);
    BOOL (WINAPI *SwapIntervalEXT)(int);
    void *(WINAPI *FenceSync)(unsigned, unsigned);
    void (WINAPI *WaitSync)(void *, unsigned, uint64_t);
    void (WINAPI *DeleteSync)(void *);
    void (WINAPI *Flush)(void);
} gl;

static HGLRC ours, ours_share;
static unsigned fbo, rb_color, rb_depth, fbo_resolve, rb_resolve;
static unsigned eng_fbo, eng_fbo_rb;   // FBO-объект в контексте движка и RBO, подключённый к нему
static HGLRC eng_fbo_ctx;
static void (WINAPI *gl_Finish)(void);
static int fb_w, fb_h, fb_samples, failed;
static uint32_t frames, last_logged_prims;

static int load_core(void)
{
    HMODULE m = GetModuleHandleA("opengl32.dll");
    if (!m)
        return 0;
#define L(f, n) (*(FARPROC *)&gl.f = GetProcAddress(m, n))
    L(MakeCurrent, "wglMakeCurrent"); L(GetCurrentContext, "wglGetCurrentContext"); L(GetCurrentDC, "wglGetCurrentDC");
    L(DeleteContext, "wglDeleteContext"); L(GetProcAddress, "wglGetProcAddress");
    L(Begin, "glBegin"); L(End, "glEnd"); L(Vertex4f, "glVertex4f"); L(Vertex3f, "glVertex3f");
    L(Color4ub, "glColor4ub"); L(TexCoord2f, "glTexCoord2f"); L(Enable, "glEnable"); L(Disable, "glDisable");
    L(BindTexture, "glBindTexture"); L(BlendFunc, "glBlendFunc"); L(AlphaFunc, "glAlphaFunc");
    L(DepthFunc, "glDepthFunc"); L(DepthMask, "glDepthMask"); L(PolygonOffset, "glPolygonOffset"); L(ShadeModel, "glShadeModel");
    L(MatrixMode, "glMatrixMode"); L(LoadIdentity, "glLoadIdentity"); L(Ortho, "glOrtho"); L(Viewport, "glViewport");
    L(Clear, "glClear"); L(ClearColor, "glClearColor"); L(ClearDepth, "glClearDepth"); L(GetIntegerv, "glGetIntegerv");
    L(GetString, "glGetString"); L(GetError, "glGetError"); L(ReadPixels, "glReadPixels"); L(PixelStorei, "glPixelStorei");
    L(TexImage2D, "glTexImage2D"); L(TexParameteri, "glTexParameteri"); L(DrawArrays, "glDrawArrays");
    L(GetTexImage, "glGetTexImage");
    *(FARPROC *)&gl_Finish = GetProcAddress(m, "glFinish");
    L(Flush, "glFlush");
#undef L
    return gl.MakeCurrent && gl.GetProcAddress && gl.Begin && gl.Vertex4f;
}

static int load_ext(void)
{
#define X(f, n) (*(PROC *)&gl.f = gl.GetProcAddress(n))
    X(GenFramebuffers, "glGenFramebuffers"); X(DeleteFramebuffers, "glDeleteFramebuffers");
    X(BindFramebuffer, "glBindFramebuffer"); X(FramebufferRenderbuffer, "glFramebufferRenderbuffer");
    X(CheckFramebufferStatus, "glCheckFramebufferStatus"); X(GenRenderbuffers, "glGenRenderbuffers");
    X(DeleteRenderbuffers, "glDeleteRenderbuffers"); X(BindRenderbuffer, "glBindRenderbuffer");
    X(RenderbufferStorageMultisample, "glRenderbufferStorageMultisample"); X(BlitFramebuffer, "glBlitFramebuffer");
    X(SwapIntervalEXT, "wglSwapIntervalEXT");
    X(FenceSync, "glFenceSync"); X(WaitSync, "glWaitSync"); X(DeleteSync, "glDeleteSync");
#undef X
    return gl.GenFramebuffers && gl.BindFramebuffer && gl.GenRenderbuffers && gl.RenderbufferStorageMultisample &&
           gl.BlitFramebuffer && gl.FramebufferRenderbuffer && gl.CheckFramebufferStatus;
}

static void gpu_forget(void);
static void cap_forget(void);
static unsigned sky_fbo;
static int sky_w, sky_h;

static void destroy_ours(const char *why)
{
    if (!ours)
        return;
    gl.DeleteContext(ours);   // FBO/RBO уходят вместе с контекстом (наш не бывает текущим вне кадра)
    bb_log("M3: наш контекст %p удалён (%s)", (void *)ours, why);
    ours = ours_share = NULL;
    fbo = rb_color = rb_depth = fbo_resolve = rb_resolve = 0;
    fb_w = fb_h = 0;
    sky_fbo = 0;
    sky_w = sky_h = 0;
    gpu_forget();
    cap_forget();
}

void bb_render_engine_context_deleted(HGLRC ctx)
{
    if (ctx == eng_fbo_ctx) {       // FBO-объект движкового контекста уходит вместе с ним
        eng_fbo = eng_fbo_rb = 0;
        eng_fbo_ctx = NULL;
    }
    if (ours && ctx == ours_share)
        destroy_ours("удалён контекст движка");
}

static int create_ours(HDC dc, HGLRC eng)
{
    static const int attribs[] = {
        0x2091, 4,      // WGL_CONTEXT_MAJOR_VERSION_ARB
        0x2092, 6,      // WGL_CONTEXT_MINOR_VERSION_ARB
        0x9126, 2,      // WGL_CONTEXT_PROFILE_MASK_ARB = COMPATIBILITY
        0
    };
    int profile = 0;

    if (!gl.MakeCurrent && !load_core()) {
        bb_log("M3: opengl32 не найден");
        return 0;
    }
    *(PROC *)&gl.CreateContextAttribsARB = gl.GetProcAddress("wglCreateContextAttribsARB");   // eng текущий
    if (!gl.CreateContextAttribsARB) {
        bb_log("M3: нет wglCreateContextAttribsARB");
        return 0;
    }
    ours = gl.CreateContextAttribsARB(dc, eng, attribs);
    if (!ours) {
        bb_log("M3: wglCreateContextAttribsARB(share=%p) не создал контекст, err=%lu", (void *)eng, GetLastError());
        return 0;
    }
    ours_share = eng;
    gl.MakeCurrent(dc, ours);
    if (!load_ext()) {
        bb_log("M3: нет функций FBO в нашем контексте");
        gl.MakeCurrent(dc, eng);
        destroy_ours("нет FBO");
        return 0;
    }
    gl.GetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
    gl.GetIntegerv(GL_MAX_SAMPLES, &fb_samples);
    if (fb_samples > bb_cfg.msaa)
        fb_samples = bb_cfg.msaa;
    // резерв имён текстур 1..0x7cf (§1.4.1 п. 7): чужие Gen в share-группе не займут имена движка
    for (unsigned n = 1; n <= 0x7cf; n++)
        gl.BindTexture(GL_TEXTURE_2D, n);
    gl.BindTexture(GL_TEXTURE_2D, 0);
    gl.Disable(GL_LIGHTING); gl.Disable(GL_CULL_FACE); gl.Disable(GL_FOG); gl.Disable(GL_DITHER);
    if (gl.SwapIntervalEXT && bb_cfg.vsync >= 0)   // по умолчанию не трогаем: своп делает движок
        gl.SwapIntervalEXT(bb_cfg.vsync);
    bb_log("M3: наш контекст %p (share %p), %s, profile_mask=%#x, MSAA %d×, err=%#x", (void *)ours, (void *)eng,
           gl.GetString(GL_VERSION), profile, fb_samples, gl.GetError());
    gl.MakeCurrent(dc, eng);
    return 1;
}

static int ensure_fbo(int w, int h)
{
    if (fbo && w == fb_w && h == fb_h)
        return 1;
    if (fbo) {
        gl.DeleteFramebuffers(1, &fbo);
        gl.DeleteFramebuffers(1, &fbo_resolve);
        gl.DeleteRenderbuffers(1, &rb_color);
        gl.DeleteRenderbuffers(1, &rb_depth);
        gl.DeleteRenderbuffers(1, &rb_resolve);
        fbo = rb_color = rb_depth = fbo_resolve = rb_resolve = 0;
    }
    gl.GenRenderbuffers(1, &rb_color);
    gl.BindRenderbuffer(GL_RENDERBUFFER, rb_color);
    gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, fb_samples, GL_RGBA8, w, h);
    gl.GenRenderbuffers(1, &rb_depth);
    gl.BindRenderbuffer(GL_RENDERBUFFER, rb_depth);
    gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, fb_samples, GL_DEPTH_COMPONENT32F, w, h);
    gl.GenFramebuffers(1, &fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb_color);
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb_depth);
    unsigned status = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
    // разрешение MSAA — в одиночный RGBA8 (форматы при MSAA-blit должны совпадать, а у окна RGB8), потом в окно
    gl.GenRenderbuffers(1, &rb_resolve);
    gl.BindRenderbuffer(GL_RENDERBUFFER, rb_resolve);
    gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, 0, GL_RGBA8, w, h);
    gl.GenFramebuffers(1, &fbo_resolve);
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo_resolve);
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb_resolve);
    unsigned status2 = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
    bb_log("M3: FBO %ux%u MSAA %d× -> %#x, resolve -> %#x", w, h, fb_samples, status, status2);
    if (status != GL_FRAMEBUFFER_COMPLETE || status2 != GL_FRAMEBUFFER_COMPLETE)
        return 0;
    fb_w = w;
    fb_h = h;
    return 1;
}

// мир: всё из пула, кроме экранных писателей (HUD3D, текст, оверлеи, полоски) и билбордов у самой камеры (HUD-спрайты)
static int is_world(const struct bb_vert *v)
{
    switch (v->tag) {
    case BB_TAG_HUD3D: case BB_TAG_TEXT: case BB_TAG_OVL: case BB_TAG_BARS: case BB_TAG_HUDBAR: case BB_TAG_IMM:
        return 0;
    case BB_TAG_BB:
        return v->z < -Z_NEAR;
    default:
        return 1;
    }
}

// --- Hor+ (DESIGN §1.8 п. 1–2): на время построения sx камеры движка ([0x4efb54], Q12) сжимается на
// F_cull = (4/3)/aspect/(1+guard) — движок не отсекает геометрию за краями 4:3; матрица 0x4efae0 построения уже
// сжата. Билборды (матрица 0x52fce0) и 3D-HUD (читает sx после восстановления) не сжаты. В рендере мир разжимается
// на весь экран при неизменном vFOV.
#define G_SX 0x4efb54u
static double hor_fcull = 1.0;          // F_cull последнего построения (1 — Hor+ не действует)
static int16_t saved_sx;
static int sx_compressed;

void bb_hor_build_begin(uint32_t ret)
{
    RECT rc;
    sx_compressed = 0;
    switch (ret) {                       // только 5 построений; 0x40dea7 (реинит) и 0x4478ce (инит) — без сжатия
    case 0x405ea9: case 0x406851: case 0x4069c0: case 0x406be1: case 0x406d4c:
        break;
    default:
        return;
    }
    hor_fcull = 1.0;
    if (!bb_cfg.render || !bb_cfg.hor_plus || failed || !bb_game_hwnd || !GetClientRect(bb_game_hwnd, &rc) ||
        rc.bottom <= 0) {
        bb_edges_apply(0, 1.0);
        return;
    }
    double f = (4.0 / 3.0) / ((double)rc.right / rc.bottom);
    if (f >= 1.0) {
        bb_edges_apply(0, 1.0);
        return;
    }
    double fc = f / (1.0 + bb_cfg.hor_guard / 100.0);
    saved_sx = *(volatile int16_t *)(uintptr_t)G_SX;
    *(volatile int16_t *)(uintptr_t)G_SX = (int16_t)lround(saved_sx * fc);
    sx_compressed = 1;
    hor_fcull = fc;
    bb_edges_apply(1, fc);   // H1/H1-G/R2′ действуют с этого построения (ActorManager — в следующем тике)
}

void bb_hor_build_end(void)
{
    if (sx_compressed) {
        *(volatile int16_t *)(uintptr_t)G_SX = saved_sx;
        sx_compressed = 0;
    }
}

static double world_xscale, bb_xscale;  // множители x_ndc для сжатого мира и для билбордов

static const struct bb_state *cur;

static void apply_state(const struct bb_state *s, int world)
{
    if (s->caps & BB_CAP_TEXTURE && s->tex) {
        gl.Enable(GL_TEXTURE_2D);
        gl.BindTexture(GL_TEXTURE_2D, s->tex);
    } else {
        gl.Disable(GL_TEXTURE_2D);
    }
    if (s->caps & BB_CAP_BLEND) {
        gl.Enable(GL_BLEND);
        gl.BlendFunc(s->bsrc, s->bdst);
    } else {
        gl.Disable(GL_BLEND);
    }
    if (s->caps & BB_CAP_ALPHA) {
        gl.Enable(GL_ALPHA_TEST);
        gl.AlphaFunc(s->afunc, s->aref);
    } else {
        gl.Disable(GL_ALPHA_TEST);
    }
    if (s->caps & BB_CAP_DEPTH) {
        gl.Enable(GL_DEPTH_TEST);
        gl.DepthFunc(s->dfunc);
    } else {
        gl.Disable(GL_DEPTH_TEST);
    }
    gl.ShadeModel(s->shade);
    if (!world) {
        gl.MatrixMode(GL_PROJECTION);
        gl.LoadIdentity();
        gl.Ortho(s->ortho[0], s->ortho[1], s->ortho[2], s->ortho[3], s->ortho[4], s->ortho[5]);
        gl.MatrixMode(GL_MODELVIEW);
    }
    cur = s;
}

static void emit_world(const struct bb_vert *v, const struct bb_state *s, const float *pos)
{
    const double *o = s->ortho;
    double xn = (2.0 * pos[0] - (o[1] + o[0])) / (o[1] - o[0]) * (v->tag == BB_TAG_BB ? bb_xscale : world_xscale);
    double yn = (2.0 * pos[1] - (o[3] + o[2])) / (o[3] - o[2]);
    double z = pos[2], d = z >= 0 ? z + 1.0 : -z, w = -z;
    double zc = w * (Z_FAR + Z_NEAR) / (Z_FAR - Z_NEAR) - 2.0 * Z_FAR * Z_NEAR / (Z_FAR - Z_NEAR);
    unsigned a = v->rgba >> 24;
    // «резкий» туман: только у дальних вершин — туман движка начинается не ближе ~11 700 (fogDist 2, notes/m5-
    // draw-distance.md); ближе α < 1 — собственная прозрачность эффектов (вихрь LS01), её не трогаем
    if (a < 255 && v->tag != BB_TAG_BB && bb_cfg.fog_sharp > 10 && pos[2] < -FOG_NEAREST) {
        a = a * (unsigned)bb_cfg.fog_sharp / 10;
        if (a > 255)
            a = 255;
    }
    gl.Color4ub(v->rgba & 0xff, (v->rgba >> 8) & 0xff, (v->rgba >> 16) & 0xff, (uint8_t)a);
    gl.TexCoord2f(v->u, v->v);
    gl.Vertex4f((float)(xn * d), (float)(yn * d), (float)zc, (float)w);
}

static void emit_screen(const struct bb_vert *v)
{
    gl.Color4ub(v->rgba & 0xff, (v->rgba >> 8) & 0xff, (v->rgba >> 16) & 0xff, v->rgba >> 24);
    gl.TexCoord2f(v->u, v->v);
    gl.Vertex3f(v->x, v->y, v->z);
}

// --- M4: интерполяция между снимками по ключам вершин (объект, номер) ---
// Позиция интерполируется в пространстве вида: P = (X·d, Y·d, Z), d — делитель 0x40fa90; обратно X = Px/d'.
// Камера и объекты так интерполируются одинаково; скачки (смена ракурса, телепорт, близко к плоскости камеры)
// узнаются по величине сдвига и берутся из S_b без сглаживания.
#define HBITS 18
static int32_t htab[1 << HBITS];
static int32_t match_idx[BB_SNAP_MAX_VERTS];
static float ipos[BB_SNAP_MAX_VERTS][3];
static const struct bb_snapshot *m_a, *m_b;
static uint32_t m_a_build, m_b_build, m_matched, m_total;

static uint32_t hkey(uint32_t obj, uint16_t local)
{
    return (obj * 2654435761u ^ (uint32_t)local * 40503u) >> (32 - HBITS);
}

static void build_match(const struct bb_snapshot *a, const struct bb_snapshot *b)
{
    const uint32_t mask = (1u << HBITS) - 1;
    memset(htab, 0xff, sizeof htab);
    for (uint32_t i = 0; i < a->nverts; i++) {
        const struct bb_vert *v = &a->verts[i];
        if (!v->obj)
            continue;
        uint32_t h = hkey(v->obj, v->local);
        while (htab[h] >= 0 && !(a->verts[htab[h]].obj == v->obj && a->verts[htab[h]].local == v->local))
            h = (h + 1) & mask;
        if (htab[h] < 0)
            htab[h] = (int32_t)i;
    }
    m_matched = m_total = 0;
    for (uint32_t i = 0; i < b->nverts; i++) {
        const struct bb_vert *v = &b->verts[i];
        match_idx[i] = -1;
        if (!v->obj)
            continue;
        m_total++;
        for (uint32_t h = hkey(v->obj, v->local); htab[h] >= 0; h = (h + 1) & mask)
            if (a->verts[htab[h]].obj == v->obj && a->verts[htab[h]].local == v->local) {
                match_idx[i] = htab[h];
                m_matched++;
                break;
            }
    }
}

// P14: почему вершины мира не интерполировались (по метке источника): нет ключа / нет пары / скачок
uint32_t bb_p14_total[16], bb_p14_nokey[16], bb_p14_nomatch[16], bb_p14_jump[16];

static double divisor(double z)
{
    return z >= 0 ? z + 1.0 : -z;
}

// объекты, у которых хоть одна вершина без пары или со скачком, — целиком из S_b (иначе треугольник
// растягивается между старым и новым положением на кадр: «мерцающие большие треугольники»)
#define BAD_BITS 12
static uint32_t bad_obj[1 << BAD_BITS];
static uint16_t bad_nv[1 << BAD_BITS], bad_nm[1 << BAD_BITS], bad_nj[1 << BAD_BITS], bad_tex[1 << BAD_BITS];
static int8_t vjump[BB_SNAP_MAX_VERTS];

static uint32_t bad_add(uint32_t obj)
{
    uint32_t h = (obj * 2654435761u) >> (32 - BAD_BITS);
    while (bad_obj[h] && bad_obj[h] != obj)
        h = (h + 1) & ((1u << BAD_BITS) - 1);
    bad_obj[h] = obj;
    return h;
}

static int bad_slot(uint32_t obj)
{
    for (uint32_t h = (obj * 2654435761u) >> (32 - BAD_BITS); bad_obj[h]; h = (h + 1) & ((1u << BAD_BITS) - 1))
        if (bad_obj[h] == obj)
            return (int)h;
    return -1;
}

static uint8_t bad_ok[1 << BAD_BITS];  // объект починен: вершинам без пары положение в A восстановлено (syn)

static int bad_has(uint32_t obj)
{
    int h = bad_slot(obj);
    return h >= 0 && !bad_ok[h];
}

// P16: «плохие» объекты — сколько пар за ~1 с объект рисовался без сглаживания и почему (вершин / без пары / скачок)
#define P16_MAX 64
static struct { uint32_t obj, pairs, nv, nm, nj; uint16_t tex; } p16[P16_MAX];
static uint32_t p16_n, p16_pairs, p16_lost, p16_fixed, p16_fixed_aff, p16_knn_fallback;
static char pair_note[600];             // плохие объекты текущей пары (для записи кадров)

static void p16_collect(const struct bb_snapshot *b)
{
    int len = 0;
    for (uint32_t i = 0; i < b->nprims; i++) {
        const struct bb_prim *p = &b->prims[i];
        for (unsigned k = 0; k < p->nverts; k++) {
            const struct bb_vert *v = &b->verts[p->first + k];
            int h = v->obj ? bad_slot(v->obj) : -1;
            if (h < 0)
                continue;
            bad_nv[h]++;
            if (!bad_tex[h])
                bad_tex[h] = (uint16_t)b->states[p->state].tex;
        }
    }
    p16_pairs++;
    pair_note[0] = 0;
    for (uint32_t h = 0; h < (1u << BAD_BITS); h++) {
        if (!bad_obj[h])
            continue;
        if (bad_ok[h]) {
            p16_fixed++;
            p16_fixed_aff += bad_ok[h] == 1;
            continue;
        }
        if (len < (int)sizeof pair_note - 64)
            len += wsprintfA(pair_note + len, " %08lx/%u/%u/%u/%u", (unsigned long)bad_obj[h], bad_tex[h], bad_nv[h],
                             bad_nm[h], bad_nj[h]);
        uint32_t k = 0;
        while (k < p16_n && p16[k].obj != bad_obj[h])
            k++;
        if (k == p16_n) {
            if (p16_n == P16_MAX) {
                p16_lost++;
                continue;
            }
            memset(&p16[p16_n++], 0, sizeof p16[0]);
            p16[k].obj = bad_obj[h];
            p16[k].tex = bad_tex[h];
        }
        p16[k].pairs++;
        p16[k].nv += bad_nv[h];
        p16[k].nm += bad_nm[h];
        p16[k].nj += bad_nj[h];
    }
}

static void p16_log(void)
{
    char line[900];
    int len = 0;
    for (int r = 0; r < 10 && p16_n; r++) {         // 10 худших по числу пар
        uint32_t best = 0;
        for (uint32_t k = 1; k < p16_n; k++)
            if (p16[k].pairs > p16[best].pairs)
                best = k;
        len += wsprintfA(line + len, " %08lx tex%u:%lu пар (в=%lu бп=%lu ск=%lu)", (unsigned long)p16[best].obj,
                         p16[best].tex, (unsigned long)p16[best].pairs, (unsigned long)(p16[best].nv / p16[best].pairs),
                         (unsigned long)p16[best].nm, (unsigned long)p16[best].nj);
        p16[best] = p16[--p16_n];
    }
    line[len] = 0;
    if (len || p16_lost || p16_fixed)
        bb_log("P16 плохие объекты (пар %lu, мимо таблицы %lu, починено %lu, из них аффинно %lu, вершин с отказом аффинного %lu):%s",
               (unsigned long)p16_pairs, (unsigned long)p16_lost, (unsigned long)p16_fixed, (unsigned long)p16_fixed_aff,
               (unsigned long)p16_knn_fallback, line);
    p16_n = p16_pairs = p16_lost = p16_fixed = p16_fixed_aff = p16_knn_fallback = 0;
}

// Вершины без пары (грань только что вошла в кадр или развернулась к камере) не выключают сглаживание объекта:
// положение в A восстанавливается по его вершинам с парой. В пространстве камеры B → A — аффинное отображение
// (у жёсткого объекта точно; у плоского пола вне плоскости вершин нет — псевдообратная); при большой невязке
// (скелетный актёр) — смещение 4 ближайших вершин с парой. Раньше объект целиком шёл из B: пол LS01 на повороте
// терял сглаживание 1–2 пары в секунду (звёзды «дёргаются»), а на прилёте слои пола делали это вразнобой —
// незакреплённый слой съезжал с соседнего и пол «мигал слоями» (запись кадров 2026-10-16, кадры 427 и 436).
#define REP_MAX 256
static int16_t bad_ci[1 << BAD_BITS];
static uint32_t rep_off[REP_MAX + 1], rep_list[BB_SNAP_MAX_VERTS];
static float syn[BB_SNAP_MAX_VERTS][3];
static uint8_t vsyn[BB_SNAP_MAX_VERTS];

static void to_view(const float *p, double ih, double *o)
{
    double d = divisor(p[2]) * ih;
    o[0] = p[0] * d;
    o[1] = p[1] * d;
    o[2] = p[2];
}

// симметричная 3×3 → диагональ (собственные числа) и векторы-столбцы v (Якоби)
static void eig3(double a[3][3], double v[3][3])
{
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            v[i][j] = i == j;
    for (int sweep = 0; sweep < 32; sweep++) {
        double off = fabs(a[0][1]) + fabs(a[0][2]) + fabs(a[1][2]);
        if (off <= 1e-15 * (fabs(a[0][0]) + fabs(a[1][1]) + fabs(a[2][2])))
            break;
        for (int p = 0; p < 2; p++)
            for (int q = p + 1; q < 3; q++) {
                if (a[p][q] == 0)
                    continue;
                double th = (a[q][q] - a[p][p]) / (2 * a[p][q]);
                double t = (th >= 0 ? 1 : -1) / (fabs(th) + sqrt(th * th + 1)), c = 1 / sqrt(t * t + 1), sn = t * c;
                for (int k = 0; k < 3; k++) {
                    double kp = a[k][p], kq = a[k][q];
                    a[k][p] = c * kp - sn * kq;
                    a[k][q] = sn * kp + c * kq;
                }
                for (int k = 0; k < 3; k++) {
                    double pk = a[p][k], qk = a[q][k];
                    a[p][k] = c * pk - sn * qk;
                    a[q][k] = sn * pk + c * qk;
                }
                for (int k = 0; k < 3; k++) {
                    double kp = v[k][p], kq = v[k][q];
                    v[k][p] = c * kp - sn * kq;
                    v[k][q] = sn * kp + c * kq;
                }
            }
    }
}

static void repair_objects(const struct bb_snapshot *a, const struct bb_snapshot *b, double ih, double k)
{
    uint32_t ncand = 0;
    memset(bad_ci, 0xff, sizeof bad_ci);
    memset(bad_ok, 0, sizeof bad_ok);
    for (uint32_t h = 0; h < (1u << BAD_BITS) && ncand < REP_MAX; h++)
        if (bad_obj[h] && bad_nm[h] && !bad_nj[h])
            bad_ci[h] = (int16_t)ncand++;
    if (!ncand)
        return;
    memset(rep_off, 0, sizeof rep_off);
    for (uint32_t i = 0; i < b->nverts; i++) {          // вершины с парой по объектам-кандидатам
        int h = match_idx[i] >= 0 ? bad_slot(b->verts[i].obj) : -1;
        if (h >= 0 && bad_ci[h] >= 0)
            rep_off[bad_ci[h] + 1]++;
    }
    for (uint32_t c = 0; c < ncand; c++)
        rep_off[c + 1] += rep_off[c];
    static uint32_t fill[REP_MAX];
    memcpy(fill, rep_off, ncand * sizeof fill[0]);
    for (uint32_t i = 0; i < b->nverts; i++) {
        int h = match_idx[i] >= 0 ? bad_slot(b->verts[i].obj) : -1;
        if (h >= 0 && bad_ci[h] >= 0)
            rep_list[fill[bad_ci[h]]++] = i;
    }
    static double L[REP_MAX][3][3], mua[REP_MAX][3], mub[REP_MAX][3], dmax[REP_MAX];
    static int8_t mode[REP_MAX];                     // 0 — не чинится, 1 — аффинно, 2 — ближайшие
    for (uint32_t c = 0; c < ncand; c++) {
        uint32_t n = rep_off[c + 1] - rep_off[c];
        mode[c] = 0;
        if (n < 4)
            continue;
        double sb[3] = { 0 }, sa[3] = { 0 }, C[3][3] = { { 0 } }, D[3][3] = { { 0 } }, V[3][3], pb[3], pa[3];
        for (uint32_t t = rep_off[c]; t < rep_off[c + 1]; t++) {
            to_view(&b->verts[rep_list[t]].x, ih, pb);
            to_view(&a->verts[match_idx[rep_list[t]]].x, ih, pa);
            for (int r = 0; r < 3; r++) {
                sb[r] += pb[r];
                sa[r] += pa[r];
            }
        }
        for (int r = 0; r < 3; r++) {
            mub[c][r] = sb[r] / n;
            mua[c][r] = sa[r] / n;
        }
        for (uint32_t t = rep_off[c]; t < rep_off[c + 1]; t++) {
            to_view(&b->verts[rep_list[t]].x, ih, pb);
            to_view(&a->verts[match_idx[rep_list[t]]].x, ih, pa);
            for (int r = 0; r < 3; r++)
                for (int q = 0; q < 3; q++) {
                    C[r][q] += (pb[r] - mub[c][r]) * (pb[q] - mub[c][q]);
                    D[r][q] += (pb[r] - mub[c][r]) * (pa[q] - mua[c][q]);
                }
        }
        double spread2 = (C[0][0] + C[1][1] + C[2][2]) / n;
        eig3(C, V);
        double lmax = fmax(C[0][0], fmax(C[1][1], C[2][2])), inv[3];
        int rank = 0;
        for (int r = 0; r < 3; r++) {
            // разброс по направлению < 1 % главного — вырождено (ухо Багза — узкая полоса: обращение шума
            // уносило восстановленные вершины в сторону, «уши в воздухе», 2026-10-16)
            inv[r] = C[r][r] > 1e-4 * lmax && C[r][r] > 0 ? 1.0 / C[r][r] : 0.0;
            rank += inv[r] > 0;
        }
        double Cp[3][3];                                 // C⁺ = V·diag(inv)·Vᵀ, L = Dᵀ·C⁺
        for (int r = 0; r < 3; r++)
            for (int q = 0; q < 3; q++)
                Cp[r][q] = V[r][0] * inv[0] * V[q][0] + V[r][1] * inv[1] * V[q][1] + V[r][2] * inv[2] * V[q][2];
        for (int r = 0; r < 3; r++)
            for (int q = 0; q < 3; q++)
                L[c][r][q] = D[0][r] * Cp[0][q] + D[1][r] * Cp[1][q] + D[2][r] * Cp[2][q];
        double res = 0;
        dmax[c] = 0;
        for (uint32_t t = rep_off[c]; t < rep_off[c + 1]; t++) {
            to_view(&b->verts[rep_list[t]].x, ih, pb);
            to_view(&a->verts[match_idx[rep_list[t]]].x, ih, pa);
            double dd = sqrt((pa[0] - pb[0]) * (pa[0] - pb[0]) + (pa[1] - pb[1]) * (pa[1] - pb[1]) + (pa[2] - pb[2]) * (pa[2] - pb[2]));
            dmax[c] = fmax(dmax[c], dd);
            for (int r = 0; r < 3; r++) {
                double e = mua[c][r] - pa[r];
                for (int q = 0; q < 3; q++)
                    e += L[c][r][q] * (pb[q] - mub[c][q]);
                res += e * e;
            }
        }
        mode[c] = rank >= 2 && sqrt(res / n) <= 1.0 + 0.002 * sqrt(spread2) ? 1 : 2;
    }
    for (uint32_t i = 0; i < b->nverts; i++) {          // вершины без пары у кандидатов
        const struct bb_vert *v = &b->verts[i];
        if (match_idx[i] >= 0 || !v->obj)
            continue;
        int h = bad_slot(v->obj);
        if (h < 0 || bad_ci[h] < 0)
            continue;
        uint32_t c = (uint32_t)bad_ci[h];
        double pb[3], pa[3];
        to_view(&v->x, ih, pb);
        int use_knn = mode[c] == 2;
        if (mode[c] == 1) {
            for (int r = 0; r < 3; r++)
                pa[r] = mua[c][r] + L[c][r][0] * (pb[0] - mub[c][0]) + L[c][r][1] * (pb[1] - mub[c][1]) +
                        L[c][r][2] * (pb[2] - mub[c][2]);
            // сдвиг восстановленной вершины не больше двух максимальных сдвигов вершин с парой — иначе экстраполяция
            // ушла вразнос (вершина далеко за пределами вершин с парой), берём соседей
            double ds = sqrt((pa[0] - pb[0]) * (pa[0] - pb[0]) + (pa[1] - pb[1]) * (pa[1] - pb[1]) + (pa[2] - pb[2]) * (pa[2] - pb[2]));
            if (ds > 2.0 * dmax[c] + 1.0) {
                use_knn = 1;
                p16_knn_fallback++;
            }
        } else if (mode[c] != 2) {
            continue;
        }
        if (use_knn) {
            int32_t nj[4] = { -1, -1, -1, -1 };          // 4 ближайших разных вершины пула с парой
            double nd[4] = { 1e300, 1e300, 1e300, 1e300 }, wsum = 0, q[3];
            uint32_t nt[4] = { 0 };
            for (uint32_t t = rep_off[c]; t < rep_off[c + 1]; t++) {
                int32_t j = match_idx[rep_list[t]];
                if (j == nj[0] || j == nj[1] || j == nj[2] || j == nj[3])
                    continue;
                to_view(&b->verts[rep_list[t]].x, ih, q);
                double dd = (q[0] - pb[0]) * (q[0] - pb[0]) + (q[1] - pb[1]) * (q[1] - pb[1]) + (q[2] - pb[2]) * (q[2] - pb[2]);
                for (int s4 = 0; s4 < 4; s4++)
                    if (dd < nd[s4]) {
                        for (int m = 3; m > s4; m--) {
                            nd[m] = nd[m - 1];
                            nj[m] = nj[m - 1];
                            nt[m] = nt[m - 1];
                        }
                        nd[s4] = dd;
                        nj[s4] = j;
                        nt[s4] = rep_list[t];
                        break;
                    }
            }
            pa[0] = pa[1] = pa[2] = 0;
            for (int s4 = 0; s4 < 4 && nj[s4] >= 0; s4++) {
                double w = 1.0 / (nd[s4] + 1.0), qa[3];
                to_view(&b->verts[nt[s4]].x, ih, q);
                to_view(&a->verts[nj[s4]].x, ih, qa);
                for (int r = 0; r < 3; r++)
                    pa[r] += w * (qa[r] - q[r]);
                wsum += w;
            }
            for (int r = 0; r < 3; r++)
                pa[r] = pb[r] + pa[r] / wsum;
        }
        // восстановленный сдвиг — под тот же тест скачка; у плоскости камеры (d → 0) не восстанавливаем
        double ra = sqrt(pa[0] * pa[0] + pa[1] * pa[1] + pa[2] * pa[2]), rb = sqrt(pb[0] * pb[0] + pb[1] * pb[1] + pb[2] * pb[2]);
        double dx = pb[0] - pa[0], dy = pb[1] - pa[1], dz = pb[2] - pa[2], da = divisor(pa[2]) * ih;
        if (sqrt(dx * dx + dy * dy + dz * dz) > (0.5 * fmax(ra, rb) + 500.0) * k || da < 1e-6) {
            mode[c] = 0;
            continue;
        }
        syn[i][0] = (float)(pa[0] / da);
        syn[i][1] = (float)(pa[1] / da);
        syn[i][2] = (float)pa[2];
        vsyn[i] = 1;
    }
    for (uint32_t h = 0; h < (1u << BAD_BITS); h++)    // починен — если ни одна вершина не отказала
        if (bad_ci[h] >= 0 && mode[bad_ci[h]])
            bad_ok[h] = (uint8_t)mode[bad_ci[h]];
}

// Объекты с анимацией через тик (15 Гц): поза сингулярности LS01 меняется в одном тике и стоит в следующем — сглаживание
// по парам давало «едет тик — стоит тик» (запись кадров 2026-10-16: разница кадров 2,5 / 0,1 через пару). Движение
// объекта меряется в МИРЕ (w = C⁻¹·v + eye, камера снимка), иначе при движущейся камере (прилёт) покой не узнать.
// Если последние 6 пар объекта чередуются (движение/покой), шаг позы растягивается на две пары, а камера — как у всех:
// в паре с движением B заменяется на «середину шага при камере B» C_B·((w_A + w_B)/2 − e_B), в паре покоя A —
// на «середину шага при камере A» C_A·((w_X1 + w_A)/2 − e_A), X1 — поза в начале шага (прошлая пара). Половины
// сходятся в середине; при неподвижной камере это ровно t = α/2 и t = (1+α)/2.
#define HR_BITS 12
static struct hr_obj { uint32_t obj, build, seen; uint8_t hist; } hr_tab[1 << HR_BITS];
static uint32_t hr_used, hr_prev_build;
static int8_t vhalf[BB_SNAP_MAX_VERTS];          // 0 — обычная, 1 — первая половина шага (B заменён), 2 — вторая (A заменён)
static float hr_pos[BB_SNAP_MAX_VERTS][3];       // заменённая позиция (экранные X, Y и Z вида)
static float hr_w1[2][BB_SNAP_MAX_VERTS][3];     // [hr_cur] — мировая поза A вершин первой половины (по индексу B)
static uint8_t hr_w1ok[2][BB_SNAP_MAX_VERTS];
static int hr_cur;
static uint32_t p17_pairs, p17_objs, p17_n, p17_obj[6], p18_n, p18_wstill, p18_vstill, fd_sec;

static struct hr_obj *hr_find(uint32_t obj, int add)
{
    uint32_t h = (obj * 2654435761u) >> (32 - HR_BITS);
    while (hr_tab[h].obj && hr_tab[h].obj != obj)
        h = (h + 1) & ((1u << HR_BITS) - 1);
    if (!hr_tab[h].obj) {
        if (!add)
            return NULL;
        hr_tab[h].obj = obj;
        hr_tab[h].build = hr_tab[h].seen = 0;
        hr_tab[h].hist = 0;
        hr_used++;
    }
    return &hr_tab[h];
}

static void hr_rebuild(uint32_t keep_from)          // таблица заполнилась: оставить объекты двух последних тиков
{
    static struct hr_obj old[1 << HR_BITS];
    memcpy(old, hr_tab, sizeof old);
    memset(hr_tab, 0, sizeof hr_tab);
    hr_used = 0;
    for (uint32_t i = 0; i < (1u << HR_BITS); i++)
        if (old[i].obj && old[i].build >= keep_from)
            *hr_find(old[i].obj, 1) = old[i];
}

static const float *pair_a(const struct bb_snapshot *a, const struct bb_snapshot *b, uint32_t i);

struct hr_cam { double c[9], ci[9], e[3]; };

static int hr_cam_of(const struct bb_snapshot *s, struct hr_cam *k)
{
    const double *m = s->cam;
    double d = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (fabs(d) < 1e-9) {               // камеры нет — мир = вид (покой узнаётся только при неподвижной камере)
        memset(k, 0, sizeof *k);
        k->c[0] = k->c[4] = k->c[8] = k->ci[0] = k->ci[4] = k->ci[8] = 1;
        return 0;
    }
    memcpy(k->c, m, sizeof k->c);
    memcpy(k->e, s->eye, sizeof k->e);
    k->ci[0] = (m[4] * m[8] - m[5] * m[7]) / d; k->ci[1] = (m[2] * m[7] - m[1] * m[8]) / d; k->ci[2] = (m[1] * m[5] - m[2] * m[4]) / d;
    k->ci[3] = (m[5] * m[6] - m[3] * m[8]) / d; k->ci[4] = (m[0] * m[8] - m[2] * m[6]) / d; k->ci[5] = (m[2] * m[3] - m[0] * m[5]) / d;
    k->ci[6] = (m[3] * m[7] - m[4] * m[6]) / d; k->ci[7] = (m[1] * m[6] - m[0] * m[7]) / d; k->ci[8] = (m[0] * m[4] - m[1] * m[3]) / d;
    return 1;
}

static void hr_world(const struct hr_cam *k, const double *v, double *w)    // w = C⁻¹·v + e
{
    for (int r = 0; r < 3; r++)
        w[r] = k->ci[3 * r] * v[0] + k->ci[3 * r + 1] * v[1] + k->ci[3 * r + 2] * v[2] + k->e[r];
}

static void hr_view(const struct hr_cam *k, const double *w, double *v)     // v = C·(w − e)
{
    double d[3] = { w[0] - k->e[0], w[1] - k->e[1], w[2] - k->e[2] };
    for (int r = 0; r < 3; r++)
        v[r] = k->c[3 * r] * d[0] + k->c[3 * r + 1] * d[1] + k->c[3 * r + 2] * d[2];
}

static int hr_screen(const double *v, double ih, float *o)                  // вид → экранные X, Y и Z вида
{
    double d = divisor(v[2]) * ih;
    if (d < 1e-6)
        return 0;
    o[0] = (float)(v[0] / d);
    o[1] = (float)(v[1] / d);
    o[2] = (float)v[2];
    return 1;
}

static void half_rate(const struct bb_snapshot *a, const struct bb_snapshot *b, double ih)
{
    struct hr_cam ka, kb;
    int prev = hr_cur, w1_valid = hr_prev_build == a->build_id;
    hr_cam_of(a, &ka);
    hr_cam_of(b, &kb);
    hr_cur ^= 1;
    hr_prev_build = b->build_id;
    memset(hr_w1ok[hr_cur], 0, b->nverts);
    if (hr_used > (1u << HR_BITS) / 2)
        hr_rebuild(a->build_id);
    for (uint32_t i = 0; i < b->nverts; i++) {          // история движения объектов в мире: бит 0 — эта пара
        const struct bb_vert *v = &b->verts[i];
        int32_t j = match_idx[i];
        if (!v->obj || j < 0)
            continue;
        struct hr_obj *o = hr_find(v->obj, 1);
        if (o->build != b->build_id) {
            o->hist = o->build == a->build_id ? (uint8_t)(o->hist << 1) : 0;   // объект пропал на тик — заново
            o->build = b->build_id;
        }
        double pa[3], pb[3], wa[3], wb[3];
        to_view(&a->verts[j].x, ih, pa);
        to_view(&v->x, ih, pb);
        hr_world(&ka, pa, wa);
        hr_world(&kb, pb, wb);
        // шум: Q12 матрицы камеры и целые сдвиги движка — ~1 ед. на 8000; движение позы сингулярности — десятки
        double tol = 1.5 + 2e-4 * sqrt(pb[0] * pb[0] + pb[1] * pb[1] + pb[2] * pb[2]);
        double dw = fabs(wb[0] - wa[0]) + fabs(wb[1] - wa[1]) + fabs(wb[2] - wa[2]);
        double dv = fabs(pb[0] - pa[0]) + fabs(pb[1] - pa[1]) + fabs(pb[2] - pa[2]);
        p18_n++;
        p18_wstill += dw <= tol;
        p18_vstill += dv <= tol;
        if (dw > tol)
            o->hist |= 1;
    }
    p17_pairs++;
    for (uint32_t i = 0; i < b->nverts; i++) {
        const struct bb_vert *v = &b->verts[i];
        vhalf[i] = 0;
        const float *u = v->obj ? pair_a(a, b, i) : NULL;
        struct hr_obj *o = u ? hr_find(v->obj, 0) : NULL;
        if (!o || o->build != b->build_id)
            continue;
        unsigned h = o->hist & 63u;
        double pa[3], pb[3], wa[3], q[3];
        to_view(u, ih, pa);
        to_view(&v->x, ih, pb);
        if (h == 21u) {                             // 010101: движение после покоя — первая половина
            hr_world(&ka, pa, wa);
            hr_view(&kb, wa, q);                    // поза A при камере B
            for (int r = 0; r < 3; r++)
                q[r] = 0.5 * (pb[r] + q[r]);
            if (!hr_screen(q, ih, hr_pos[i]))
                continue;
            for (int r = 0; r < 3; r++)
                hr_w1[hr_cur][i][r] = (float)wa[r];
            hr_w1ok[hr_cur][i] = 1;
            vhalf[i] = 1;
        } else if (h == 42u && w1_valid && match_idx[i] >= 0 && hr_w1ok[prev][match_idx[i]]) {
            const float *w1 = hr_w1[prev][match_idx[i]];
            double w[3] = { w1[0], w1[1], w1[2] };
            hr_view(&ka, w, q);                     // поза X1 при камере A
            for (int r = 0; r < 3; r++)
                q[r] = 0.5 * (pa[r] + q[r]);
            if (!hr_screen(q, ih, hr_pos[i]))
                continue;
            vhalf[i] = 2;                           // 101010: покой после движения — вторая половина
        } else {
            continue;
        }
        if (o->seen != b->build_id) {
            o->seen = b->build_id;
            p17_objs++;
            uint32_t k = 0;
            while (k < p17_n && p17_obj[k] != v->obj)
                k++;
            if (k == p17_n && p17_n < 6)
                p17_obj[p17_n++] = v->obj;
        }
    }
}

// M4e: вершина без положения в A (нет пары, объект «плохой») стояла весь тик в позе B, т. е. при повороте камеры —
// на тик впереди всех («дёргается»; новые у края кадра выскакивали сдвинутыми). Её A — поза B при камере A, как у
// неподвижной в мире: C_A·(C_B⁻¹·v_B + e_B − e_A). Своё движение объекта по-прежнему шагом тика, камера — плавно.
// Смена ракурса (поворот > 30° или скачок глаза > 1500 за тик) — без перепроекции.
#define REP_ZMIN 16.0
static float rp[BB_SNAP_MAX_VERTS][3];
static uint8_t vrep[BB_SNAP_MAX_VERTS];
static struct hr_cam rka, rkb;          // камеры пары (и для граней A, M4e ghost)
static int cam_cut = 1;
static uint32_t p24_rep, p24_cut, p24_pairs, p24_syn;
static float bad_res[1 << BAD_BITS];    // наибольшее отклонение пар объекта от «неподвижен в мире», в долях допуска

static void reproject(const struct bb_snapshot *a, const struct bb_snapshot *b, double ih)
{
    memset(vrep, 0, b->nverts);
    int ok = hr_cam_of(a, &rka) & hr_cam_of(b, &rkb);
    double m[9], de = 0;
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            m[3 * r + c] = rkb.c[3 * r] * rka.ci[c] + rkb.c[3 * r + 1] * rka.ci[3 + c] + rkb.c[3 * r + 2] * rka.ci[6 + c];
    for (int r = 0; r < 3; r++)
        de += (rkb.e[r] - rka.e[r]) * (rkb.e[r] - rka.e[r]);
    cam_cut = !ok || (m[0] + m[4] + m[8] - 1.0) * 0.5 < 0.866 || de > 1500.0 * 1500.0;
    p24_pairs++;
    p24_cut += cam_cut;
    if (cam_cut || !bb_cfg.reproj)
        return;
    // Починенный (M4c) объект, чьи пары неподвижны в мире, — его вершины без пары тоже по перепроекции: аффинная
    // подгонка по почти плоскому набору пар ошибалась поперёк плоскости (скала LS01, local 18/19 объекта 034e7edc:
    // 0,08 NDC ≈ 90 пкс — «отдельная полоска и щель с небом на изгибе» при повороте, trace_full 2026-10-16).
    memset(bad_res, 0, sizeof bad_res);
    for (uint32_t i = 0; i < b->nverts; i++) {
        const struct bb_vert *v = &b->verts[i];
        int32_t j = match_idx[i];
        if (!v->obj || j < 0 || vjump[i] || !is_world(v))
            continue;
        int h = bad_slot(v->obj);
        if (h < 0 || !bad_ok[h])
            continue;
        double p[3], w[3], q[3], pa[3];
        to_view(&v->x, ih, p);
        hr_world(&rkb, p, w);
        hr_view(&rka, w, q);
        to_view(&a->verts[j].x, ih, pa);
        double e = fabs(q[0] - pa[0]) + fabs(q[1] - pa[1]) + fabs(q[2] - pa[2]);
        double tol = 3.0 + 4e-4 * (fabs(pa[0]) + fabs(pa[1]) + fabs(pa[2]));
        if (e / tol > bad_res[h])
            bad_res[h] = (float)(e / tol);
    }
    for (uint32_t i = 0; i < b->nverts; i++) {
        const struct bb_vert *v = &b->verts[i];
        if (!is_world(v) || v->z > -REP_ZMIN)
            continue;
        int h = v->obj ? bad_slot(v->obj) : -1;
        if (pair_a(a, b, i)) {
            if (!vsyn[i] || h < 0 || bad_res[h] > 1.0f)
                continue;                   // пара, или починенная вершина движущегося объекта — как было
            p24_syn++;
        } else if (h >= 0 && bad_nj[h]) {   // скачок (телепорт) — объект целиком из B, как раньше
            continue;
        }
        double p[3], w[3], q[3];
        to_view(&v->x, ih, p);
        hr_world(&rkb, p, w);
        hr_view(&rka, w, q);
        if (!hr_screen(q, ih, rp[i]))
            continue;
        vrep[i] = 1;
        p24_rep++;
    }
}

// позиции A и B для интерполяции с учётом шага через тик; A = NULL — вершина не сглаживается
static const float *pair_a_half(const struct bb_snapshot *a, const struct bb_snapshot *b, uint32_t i)
{
    if (a && vrep[i])                   // без пары или починенная у неподвижного — перепроекция
        return rp[i];
    const float *u = pair_a(a, b, i);
    return u && vhalf[i] == 2 ? hr_pos[i] : u;
}

static const float *pair_b_half(const struct bb_snapshot *b, uint32_t i)
{
    return vhalf[i] == 1 ? hr_pos[i] : &b->verts[i].x;
}

static void p17_log(void)
{
    char line[200];
    int len = 0;
    for (uint32_t k = 0; k < p17_n; k++)
        len += wsprintfA(line + len, " %08lx", (unsigned long)p17_obj[k]);
    line[len] = 0;
    if (p17_objs || p18_n)
        bb_log("P17 через тик: объекто-пар %lu из %lu пар:%s | P18 вершин %lu, неподвижны в мире %lu%%, в виде %lu%% | "
               "проявлений за с %lu", (unsigned long)p17_objs, (unsigned long)p17_pairs, line, (unsigned long)p18_n,
               (unsigned long)(p18_n ? 100ull * p18_wstill / p18_n : 0), (unsigned long)(p18_n ? 100ull * p18_vstill / p18_n : 0),
               (unsigned long)fd_sec);
    fd_sec = 0;
    p17_pairs = p17_objs = p17_n = p18_n = p18_wstill = p18_vstill = 0;
}

// Пара снимков сменилась (30 раз в секунду): сопоставление, скачки и «плохие» объекты — от α не зависят.
// P14 считается здесь же, по разу на пару.
static uint32_t pair_gen;

static void prepare_pair(const struct bb_snapshot *a, const struct bb_snapshot *b)
{
    static int m_jump_k;
    if (a == m_a && b == m_b && (!a || a->build_id == m_a_build) && b->build_id == m_b_build &&
        m_jump_k == bb_cfg.jump_k)
        return;
    m_a = a; m_b = b; m_a_build = a ? a->build_id : 0; m_b_build = b->build_id; m_jump_k = bb_cfg.jump_k;
    pair_gen++;
    if (!a) {
        m_matched = m_total = 0;
        return;
    }
    // Скачок — в пространстве камеры: X·d = H·vx (0x40fa90, H = [0x4efb30] = 400), сдвиг больше половины расстояния
    // до камеры + 500. Экранный порог (35 % экрана) ловил вершины у плоскости камеры (d → 0, X огромен): пол LS01 и
    // большие объекты вокруг камеры каждую пару теряли сглаживание и крутились в 30 кадров.
    double k = bb_cfg.jump_k / 10.0;    // множитель порога (live jump_k, ×10)
    int32_t hi = *(volatile int32_t *)(uintptr_t)0x4efb30u;
    double ih = 1.0 / (hi > 0 ? hi : 400);
    build_match(a, b);
    memset(bad_obj, 0, sizeof bad_obj);
    memset(bad_nv, 0, sizeof bad_nv); memset(bad_nm, 0, sizeof bad_nm);
    memset(bad_nj, 0, sizeof bad_nj); memset(bad_tex, 0, sizeof bad_tex);
    memset(bad_ok, 0, sizeof bad_ok);
    memset(vsyn, 0, b->nverts);
    for (uint32_t i = 0; i < b->nverts; i++) {         // скачки и вершины без пары → объект «плохой»
        const struct bb_vert *v = &b->verts[i];
        int32_t j = match_idx[i];
        vjump[i] = 0;
        if (is_world(v)) {
            unsigned t = v->tag & 15;
            bb_p14_total[t]++;
            if (!v->obj)
                bb_p14_nokey[t]++;
            else if (j < 0)
                bb_p14_nomatch[t]++;
        }
        if (!v->obj)
            continue;
        if (j < 0) {
            bad_nm[bad_add(v->obj)]++;
            continue;
        }
        const struct bb_vert *u = &a->verts[j];
        double da = divisor(u->z) * ih, db = divisor(v->z) * ih;
        double ax = u->x * da, ay = u->y * da, bx = v->x * db, by = v->y * db;
        double ra = sqrt(ax * ax + ay * ay + (double)u->z * u->z), rb = sqrt(bx * bx + by * by + (double)v->z * v->z);
        double dx = bx - ax, dy = by - ay, dz = (double)v->z - u->z;
        if (sqrt(dx * dx + dy * dy + dz * dz) > (0.5 * fmax(ra, rb) + 500.0) * k) {
            vjump[i] = 1;
            bb_p14_jump[v->tag & 15]++;
            bad_nj[bad_add(v->obj)]++;
        }
    }
    if (bb_cfg.repair)
        repair_objects(a, b, ih, k);
    if (bb_cfg.half_rate)
        half_rate(a, b, ih);
    else
        memset(vhalf, 0, b->nverts);
    reproject(a, b, ih);
    p16_collect(b);
}

// положение вершины b в тике A (экранные X, Y и Z вида), если она сглаживается: пара без скачка или восстановленное
// у починенного объекта; иначе NULL — из B как есть
static const float *pair_a(const struct bb_snapshot *a, const struct bb_snapshot *b, uint32_t i)
{
    if (!a || !b->verts[i].obj || bad_has(b->verts[i].obj))
        return NULL;
    int32_t j = match_idx[i];
    if (j >= 0)
        return vjump[i] ? NULL : &a->verts[j].x;
    return vsyn[i] ? syn[i] : NULL;
}

static void interp_positions(const struct bb_snapshot *a, const struct bb_snapshot *b, double alpha)
{
    prepare_pair(a, b);
    int on = a && alpha != 1.0;
    for (uint32_t i = 0; i < b->nverts; i++) {         // интерполяция «хороших» объектов
        const struct bb_vert *v = &b->verts[i];
        const float *u = on ? pair_a_half(a, b, i) : NULL;
        if (u) {
            const float *w = pair_b_half(b, i);
            double za = u[2], zb = w[2], da = divisor(za), db = divisor(zb);
            double px = u[0] * da + (w[0] * db - u[0] * da) * alpha;
            double py = u[1] * da + (w[1] * db - u[1] * da) * alpha;
            double z = za + (zb - za) * alpha, d = divisor(z);
            ipos[i][0] = (float)(px / d);
            ipos[i][1] = (float)(py / d);
            ipos[i][2] = (float)z;
        } else {
            ipos[i][0] = v->x;
            ipos[i][1] = v->y;
            ipos[i][2] = v->z;
        }
    }
}

// Полноэкранный оверлей (фейд, затемнение паузы, киношные полосы) перекрывает всю ширину 4:3 — тянем на окно (R4).
static int is_wide_overlay(const struct bb_snapshot *s, const struct bb_prim *p)
{
    const struct bb_vert *v = &s->verts[p->first];
    if (v->tag != BB_TAG_OVL && v->tag != BB_TAG_BARS)
        return 0;
    const double *o = s->states[p->state].ortho;
    double mn = v[0].x, mx = v[0].x, eps = (o[1] - o[0]) * 0.02;
    for (unsigned k = 1; k < p->nverts; k++) {
        mn = v[k].x < mn ? v[k].x : mn;
        mx = v[k].x > mx ? v[k].x : mx;
    }
    return mn <= o[0] + eps && mx >= o[1] - eps;
}

// Варианты текстур под режимы полупрозрачности PS1 (ABR 0..3): [0x52fd74 + 48·id + 4·abr] (эмиттер 0x41493a).
// Движок конвертирует копию текстуры под режим (0x420d90, BBLIT-Findings 306/N68) и рисует её одним SRC_ALPHA.
static int8_t variant_mode[0x800];      // id текстуры → режим ABR (−1 — не вариант)
static uint32_t variant_scan_build;

static void scan_variants(uint32_t build_id)
{
    if (build_id == variant_scan_build)
        return;
    variant_scan_build = build_id;
    memset(variant_mode, -1, sizeof variant_mode);
    uint32_t max = *(volatile uint32_t *)(uintptr_t)0x4b1c90u;
    if (max > 0x7cb)
        max = 0x7cb;
    for (uint32_t id = 1; id <= max; id++)
        for (int abr = 0; abr < 4; abr++) {
            uint32_t v = *(volatile uint32_t *)(uintptr_t)(0x52fd74u + 48 * id + 4 * abr);
            if (v > 0 && v < sizeof variant_mode)
                variant_mode[v] = (int8_t)abr;
        }
}

// Полупрозрачный примитив (смешивание + текстура-вариант ABR или вершина с α < 255): глубину не пишет и чуть
// смещён к камере — как на PS1, где буфера глубины нет.
// У движка глубина линейна по экрану и между совпадающими плоскостями «выигрывает» одна; у нас перспективная
// глубина почти равна у слоёв в одной плоскости (вихрь LS01 над полом) → победитель — шум округления, мерцание.
static int is_translucent(const struct bb_snapshot *s, const struct bb_prim *p);
static int is_translucent(const struct bb_snapshot *s, const struct bb_prim *p)
{
    if (!bb_cfg.trans_mode || !(s->states[p->state].caps & BB_CAP_BLEND))
        return 0;
    if (bb_cfg.trans_mode == 2)          // эксперимент: все смешиваемые примитивы
        return 1;
    const struct bb_state *st = &s->states[p->state];
    if (st->caps & BB_CAP_TEXTURE && st->tex < sizeof variant_mode && variant_mode[st->tex] >= 0)
        return 1;
    for (unsigned k = 0; k < p->nverts; k++)
        if ((s->verts[p->first + k].rgba >> 24) < 255)
            return 1;
    return 0;
}

// Квады — всегда два треугольника (r0,r1,r2)+(r0,r2,r3): диагональ PS1 и разреза движка (R1).
static void draw_pass(const struct bb_snapshot *s, int world, const int vp43[4], const int vpwide[4])
{
    int open = 0, wide_now = -1, trans_now = -1;
    cur = NULL;
    gl.MatrixMode(GL_PROJECTION);
    gl.LoadIdentity();
    gl.MatrixMode(GL_MODELVIEW);
    gl.LoadIdentity();
    for (uint32_t i = 0; i < s->nprims; i++) {
        const struct bb_prim *p = &s->prims[i];
        const struct bb_vert *v = &s->verts[p->first];
        if (is_world(v) != world)
            continue;
        const struct bb_state *st = &s->states[p->state];
        int wide = !world && is_wide_overlay(s, p);
        int trans = world && is_translucent(s, p);
        if (st != cur || wide != wide_now || trans != trans_now) {
            if (open)
                gl.End();
            if (trans != trans_now) {
                gl.DepthMask(trans ? 0 : 1);
                if (trans) {
                    gl.Enable(GL_POLYGON_OFFSET_FILL);
                    gl.PolygonOffset(-1.0f, -4.0f);
                } else {
                    gl.Disable(GL_POLYGON_OFFSET_FILL);
                }
                trans_now = trans;
            }
            if (!world && wide != wide_now) {
                const int *vp = wide ? vpwide : vp43;
                gl.Viewport(vp[0], vp[1], vp[2], vp[3]);
                wide_now = wide;
            }
            if (st != cur)
                apply_state(st, world);
            gl.Begin(GL_TRIANGLES);
            open = 1;
        }
        static const uint8_t tri[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
        for (unsigned t = 0; t < (p->nverts == 4 ? 2u : 1u); t++)
            for (unsigned k = 0; k < 3; k++) {
                if (world)
                    emit_world(&v[tri[t][k]], st, ipos[p->first + tri[t][k]]);
                else
                    emit_screen(&v[tri[t][k]]);
            }
    }
    if (open)
        gl.End();
    gl.DepthMask(1);
    gl.Disable(GL_POLYGON_OFFSET_FILL);
}

// --- M4b: шейдеры и буферы вершин (ключ shader) ---
// Немедленный режим ×144 стоил главному потоку ~8 % ядра. Теперь вершины снимка B грузятся в VBO раз на пару
// снимков (30 раз в секунду): позиция A (пара в предыдущем тике; без пары, при скачке и у «плохого» объекта — B)
// и B — в NDC 4:3 без множителя Hor+ и с Z вида. Интерполяция в вершинном шейдере теми же формулами: N·d и z
// линейно по α, затем N = (N·d)/d(z) — при z < 0 это то же, что интерполяция экранных X·d (NDC — аффинная
// функция X). Экранные вершины — сразу в NDC своей ortho. Индексы и списки отрисовки (состояние, полупрозрачность,
// широкий оверлей, режим ABR) строятся по снимку B; на кадр остаются uniform-ы и glDrawElements на список.
// PS1-смешивание (ключ ps1_blend): у текстуры-варианта восстанавливается исходный цвет (обратная гамма 1/1,2 и
// вычитание сдвига 255 − max, max — из альфы варианта: ABR 0 — α·2, 1 — α·4/3, 3 — α·4; ABR 2 — цвет как есть)
// и смешивается по формуле режима: 0 — B/2 + F/2, 1 — B + F, 2 — B − F, 3 — B + F/4 (F = min(цвет × вершина × gain, 1),
// у спрайтов gain 1; затухание тумана — альфой вершины). Обращение — по каждому текселю до фильтрации (texelFetch ×4). Блендинг один: ONE, ONE_MINUS_SRC_ALPHA (премультипликация), для ABR 2 —
// уравнение REVERSE_SUBTRACT.
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STREAM_DRAW 0x88e0
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_VERTEX_SHADER 0x8b31
#define GL_FRAGMENT_SHADER 0x8b30
#define GL_COMPILE_STATUS 0x8b81
#define GL_LINK_STATUS 0x8b82
#define GL_FUNC_ADD 0x8006
#define GL_FUNC_REVERSE_SUBTRACT 0x800b
#define GL_ONE 1
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_FLAT 0x1d00
#define GL_SMOOTH 0x1d01

static struct {
    unsigned (WINAPI *CreateShader)(unsigned);
    void (WINAPI *ShaderSource)(unsigned, int, const char *const *, const int *);
    void (WINAPI *CompileShader)(unsigned);
    void (WINAPI *GetShaderiv)(unsigned, unsigned, int *);
    void (WINAPI *GetShaderInfoLog)(unsigned, int, int *, char *);
    unsigned (WINAPI *CreateProgram)(void);
    void (WINAPI *AttachShader)(unsigned, unsigned);
    void (WINAPI *LinkProgram)(unsigned);
    void (WINAPI *GetProgramiv)(unsigned, unsigned, int *);
    void (WINAPI *GetProgramInfoLog)(unsigned, int, int *, char *);
    void (WINAPI *UseProgram)(unsigned);
    int (WINAPI *GetUniformLocation)(unsigned, const char *);
    void (WINAPI *Uniform1i)(int, int);
    void (WINAPI *Uniform1f)(int, float);
    void (WINAPI *GenBuffers)(int, unsigned *);
    void (WINAPI *BindBuffer)(unsigned, unsigned);
    void (WINAPI *BufferData)(unsigned, ptrdiff_t, const void *, unsigned);
    void (WINAPI *GenVertexArrays)(int, unsigned *);
    void (WINAPI *BindVertexArray)(unsigned);
    void (WINAPI *VertexAttribPointer)(unsigned, int, unsigned, unsigned char, int, const void *);
    void (WINAPI *EnableVertexAttribArray)(unsigned);
    void (WINAPI *BlendEquation)(unsigned);
    void (WINAPI *ActiveTexture)(unsigned);
    void (WINAPI *UniformMatrix3fv)(int, int, unsigned char, const float *);
    void (WINAPI *Uniform3f)(int, float, float, float);
    void (WINAPI *Uniform4f)(int, float, float, float, float);
    void (WINAPI *FramebufferTexture2D)(unsigned, unsigned, unsigned, unsigned, int);
    void (WINAPI *GenerateMipmap)(unsigned);
    void (WINAPI *DrawElements)(unsigned, int, unsigned, const void *);
} gx;

static const char *const gpu_vs =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_pa;\n"          // NDC x, y (мир — 4:3 без Hor+, экран — своей ortho) и Z вида в A
    "layout(location = 1) in vec3 a_pb;\n"          // то же в тике B
    "layout(location = 2) in vec4 a_uv;\n"          // uv в A (xy) и в B (zw)
    "layout(location = 3) in vec4 a_ca;\n"          // цвет в A
    "layout(location = 4) in vec4 a_cb;\n"          // цвет в B
    "layout(location = 5) in vec4 a_flags;\n"       // x — билборд мира, y — экранная, z — спрайт (метка BB), w — без тумана (задник)
    "layout(location = 6) in vec2 a_sz;\n"          // экранные: глубина ortho в A и B; тень: запас глубины
    "layout(location = 7) in float a_fade;\n"       // M5b: проявление объекта на α = 0 (≥ 1 — виден целиком)
    "layout(location = 8) in vec2 a_hw;\n"          // M5b: высота в мире (вверх = −Y) в A и B — для дымки
    "uniform int u_bgpass;\n"
    "uniform float u_alpha, u_xs_world, u_xs_bb, u_fadek;\n"
    "uniform int u_fogs;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_col;\n"
    "out float v_w;\n"                           // глубина вида (наш туман)
    "flat out vec4 v_colf;\n"
    "flat out float v_spr;\n"
    "flat out float v_nofog;\n"
    "flat out float v_fade;\n"
    "out float v_h;\n"
    "float dv(float z) { return z >= 0.0 ? z + 1.0 : max(-z, 1e-4); }\n"   // делитель 0x40fa90 (0 — переход за камеру)
    "void main() {\n"
    "    if (u_bgpass != 0) {\n"                     // M5b: полноэкранный треугольник фона (задник + дымка)
    "        vec2 q = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "        gl_Position = vec4(q * 2.0 - 1.0, 1.0, 1.0);\n"
    "        v_w = 0.0; v_h = 0.0; v_fade = 1.0; v_nofog = 1.0; v_spr = 0.0;\n"
    "        v_uv = vec2(0.0); v_col = vec4(1.0); v_colf = vec4(1.0);\n"
    "        return;\n"
    "    }\n"
    "    float t = u_alpha;\n"
    "    v_fade = a_fade + u_alpha * u_fadek;\n"
    "    v_h = mix(a_hw.x, a_hw.y, t);\n"
    "    vec4 c = mix(a_ca, a_cb, t);\n"
    "    vec2 n = a_pb.xy;\n"                       // без пары (A = B) — ровно B
    "    float z = a_pb.z;\n"
    "    if (a_pa != a_pb) {\n"
    "        z = mix(a_pa.z, a_pb.z, t);\n"
    "        n = mix(a_pa.xy * dv(a_pa.z), a_pb.xy * dv(a_pb.z), t) / dv(z);\n"
    "    }\n"
    "    v_w = 0.0;\n"
    "    int fw = int(a_flags.w + 0.5);\n"
    "    v_nofog = float(fw & 1);\n"
    "    if (a_flags.y > 0.5) {\n"
    "        gl_Position = vec4(n, mix(a_sz.x, a_sz.y, t), 1.0);\n"
    "        v_nofog = 1.0;\n"
    "    } else {\n"
    "        const float ZF = 65536.0, ZN = 8.0;\n"
    "        float d = dv(z), w = -z;\n"
    "        v_w = w;\n"
    "        n.x *= a_flags.x > 0.5 ? u_xs_bb : u_xs_world;\n"
    "        float wz = (fw & 2) != 0 ? max(w - a_sz.x, ZN * 1.01) : w;\n"   // тень: глубина ближе на δ (a_sz.x), экран тот же
    "        gl_Position = vec4(n * d, w * ((ZF + ZN) / (ZF - ZN) - 2.0 * ZF * ZN / ((ZF - ZN) * wz)), w);\n"
    // «резкий» туман — как emit_world, в целых: только у дальних вершин мира, не у билбордов
    "        if (c.a < 1.0 && a_flags.x < 0.5 && u_fogs > 10 && z < -8000.0) {\n"
    "            int ai = int(c.a * 255.0 + 0.5);\n"
    "            c.a = float(min(255, ai * u_fogs / 10)) / 255.0;\n"
    "        }\n"
    "    }\n"
    "    v_uv = mix(a_uv.xy, a_uv.zw, t);\n"
    "    v_col = c;\n"
    "    v_colf = c;\n"
    "    v_spr = a_flags.z;\n"
    "}\n";

static const char *const gpu_fs =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "in vec4 v_col;\n"
    "in float v_w;\n"
    "flat in vec4 v_colf;\n"
    "flat in float v_spr;\n"
    "flat in float v_nofog;\n"
    "flat in float v_fade;\n"
    "uniform sampler2D u_smp, u_sky;\n"
    "uniform int u_tex, u_flat, u_afunc, u_abr, u_fadd, u_stp;\n"
    "uniform float u_aref, u_gain, u_fogD, u_fogS;\n"
    "in float v_h;\n"
    "uniform int u_bgpass;\n"
    "uniform mat3 u_ci;\n"                          // C⁻¹ кадра (камера A↔B по α)
    "uniform vec4 u_vp;\n"                          // вьюпорт мира
    "uniform vec3 u_scr;\n"                         // w/2, h/2 ortho движка, множитель Hor+ мира
    "uniform float u_hz, u_hzTop, u_hzThick, u_hzNear, u_hzFar, u_hzMax, u_hzA0, u_hzA1;\n"
    "uniform vec3 u_hzCol;\n"                       // цвет дымки кадра (среднее по горизонту задника, сглажено)
    "out vec4 o;\n"
    // M5b: туман — линейно по глубине вида от u_fogS·D до D к копии задника (пиксель в пиксель)
    "float fogf() {\n"
    "    if (u_fogD <= 0.0 || v_nofog > 0.5) return 0.0;\n"
    "    return clamp((v_w - u_fogS * u_fogD) / ((1.0 - u_fogS) * u_fogD), 0.0, 1.0);\n"
    "}\n"
    "vec3 sky() { return texelFetch(u_sky, ivec2(gl_FragCoord.xy), 0).rgb; }\n"
    // M5b: дымка у земли. Луч пикселя в мир: X, Y экрана движка → вид (X/H, Y/H, −1) → мир C⁻¹·v (верх = −Y).
    // Цвет дымки — задник на линии горизонта в этом же столбце; фон ниже горизонта — дымка (под скалами пусто).
    "vec2 scr(vec2 fc) { vec2 n = (fc - u_vp.xy) / u_vp.zw * 2.0 - 1.0; return vec2(n.x / u_scr.z * u_scr.x, n.y * u_scr.y); }\n"
    "vec3 hazecol() { return u_hzCol; }\n"
    "vec3 background() {\n"
    "    vec3 s = sky();\n"
    "    if (u_hz <= 0.0) return s;\n"
    "    vec2 p = scr(gl_FragCoord.xy);\n"
    "    vec3 d = normalize(u_ci * vec3(p / 400.0, -1.0));\n"
    "    return mix(s, hazecol(), smoothstep(u_hzA0, u_hzA1, d.y) * u_hz);\n"   // d.y — синус угла вниз от горизонта
    "}\n"
    "float hazef() {\n"
    "    if (u_hz <= 0.0 || v_nofog > 0.5) return 0.0;\n"
    "    float fh = clamp((u_hzTop - v_h) / u_hzThick, 0.0, 1.0);\n"
    "    float fd = clamp((v_w - u_hzNear) / (u_hzFar - u_hzNear), 0.0, 1.0);\n"
    "    return fh * fd * u_hz * u_hzMax;\n"
    "}\n"
    "bool atest(float a) {\n"                       // GL_NEVER 0x200 … GL_ALWAYS 0x207; −1 — тест выключен
    "    switch (u_afunc) {\n"
    "    case 0x200: return false;\n"
    "    case 0x201: return a < u_aref;\n"
    "    case 0x202: return a == u_aref;\n"
    "    case 0x203: return a <= u_aref;\n"
    "    case 0x204: return a > u_aref;\n"
    "    case 0x205: return a != u_aref;\n"
    "    case 0x206: return a >= u_aref;\n"
    "    default: return true;\n"
    "    }\n"
    "}\n"
    "vec3 ps1_color(vec4 T) {\n"                // исходный цвет текселя варианта (обращение 0x420d90)
    "    if (u_abr == 2) return T.rgb;\n"
    "    float m = min(1.0, T.a * (u_abr == 0 ? 2.0 : u_abr == 1 ? 4.0 / 3.0 : 4.0));\n"
    "    return pow(max(pow(T.rgb, vec3(1.2)) - (1.0 - m), 0.0), vec3(1.0 / 1.2));\n"
    "}\n"
    // M5b: плавное появление — упорядоченный дизеринг 4×4 (Bayer) по доле проявления
    "float bayer4(ivec2 p) {\n"
    "    const float m[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);\n"
    "    return (m[(p.x & 3) + 4 * (p.y & 3)] + 0.5) / 16.0;\n"
    "}\n"
    "void main() {\n"
    "    if (u_bgpass != 0) { o = vec4(background(), 1.0); return; }\n"
    "    if (v_fade < 1.0 && bayer4(ivec2(gl_FragCoord.xy)) > v_fade) discard;\n"
    "    vec4 col = u_flat != 0 ? v_colf : v_col;\n"
    "    if (u_abr < 0) {\n"
    "        vec4 t = u_tex != 0 ? texture(u_smp, v_uv) : vec4(1.0);\n"
    "        vec4 f = t * col;\n"                   // GL_MODULATE (движок glTexEnv не вызывает)
    "        if (!atest(f.a)) discard;\n"
    "        float fg = fogf();\n"                   // смешиваемое со сложением гаснет, остальное уходит в задник
    "        float hf = hazef();\n"
    "        if (hf > 0.0) f.rgb = u_fadd != 0 ? f.rgb * (1.0 - hf) : mix(f.rgb, hazecol(), hf);\n"
    "        if (fg > 0.0) f.rgb = u_fadd != 0 ? f.rgb * (1.0 - fg) : mix(f.rgb, background(), fg);\n"
    "        o = f;\n"
    "        return;\n"
    "    }\n"
    // PS1: обращение нелинейно — каждый тексель обращается и насыщается отдельно (как на PS1: тексель × вершина,
    // предел 1), потом билинейно с REPEAT, как GL_LINEAR движка. Слово палитры 0 (RGB≈0, α=0) — не покрытие;
    // 0x8000 (RGB=1, α=0) — покрытие с нулевым цветом: в режиме 0 гасит фон вдвое, как на PS1.
    "    ivec2 sz = textureSize(u_smp, 0);\n"
    "    vec2 p = v_uv * vec2(sz) - 0.5, fl = floor(p), fr = p - fl;\n"
    "    float g = v_spr > 0.5 ? 1.0 : u_gain;\n"  // спрайтам ПК уже ставит 1.0 = нейтраль PS1 0x80
    "    vec3 F = vec3(0.0);\n"
    "    float cov = 0.0, dk = 0.0, co = 0.0;\n"
    "    vec3 Fo = vec3(0.0);\n"
    // тексель без бита STP на PS1 непрозрачен и в полупрозрачном полигоне; порт кодирует режим альфой варианта, и у
    // смешиваемых текселей она не выше max/2 (ABR 0), 0,75·max (1), max/4 (3) — альфа выше предела = непрозрачный
    // тексель (силуэты корабля и пальм у горизонта LS01: на PS1 тёмные, сложением выходили бледными)
    "    float alim = u_abr == 0 ? 0.52 : u_abr == 1 ? 0.76 : u_abr == 3 ? 0.26 : 2.0;\n"
    "    for (int k = 0; k < 4; k++) {\n"
    "        ivec2 d = ivec2(k & 1, k >> 1);\n"
    "        vec2 q = fl + vec2(d);\n"
    "        q -= vec2(sz) * floor(q / vec2(sz));\n"
    "        vec4 T = texelFetch(u_smp, clamp(ivec2(q), ivec2(0), sz - 1), 0);\n"
    "        if (T.a <= 0.0 && max(T.r, max(T.g, T.b)) < 0.5) continue;\n"
    "        float w = (d.x == 1 ? fr.x : 1.0 - fr.x) * (d.y == 1 ? fr.y : 1.0 - fr.y);\n"
    "        if (u_stp != 0 && T.a > alim) { Fo += w * min(T.rgb * col.rgb * g, vec3(1.0)); co += w; continue; }\n"
    "        cov += w;\n"
    // вычитание, тексель с цветом ~0 и формой в альфе (тени: tex 133) — затемнение по альфе, как ПК-порт: dst·(1 − α)
    "        if (u_abr == 2 && max(T.r, max(T.g, T.b)) < 0.25 * T.a) { dk += w * T.a; continue; }\n"
    "        F += w * min(ps1_color(T) * col.rgb * g, vec3(1.0));\n"
    "    }\n"
    "    if (cov + co <= 0.0) discard;\n"
    "    F *= col.a;\n"                              // затухание в тумане движка
    "    o = u_abr == 0 ? vec4(0.5 * F, 0.5 * cov * col.a) : vec4(u_abr == 3 ? 0.25 * F : F, u_abr == 2 ? dk * col.a : 0.0);\n"
    "    o += vec4(Fo, co) * col.a;\n"              // непрозрачные тексели — поверх (премультипликация)
    "    float fg = fogf();\n"                       // премультипликация: к заднику с весом своей альфы (у сложения — гаснет)
    "    float hf = hazef();\n"
    "    if (hf > 0.0) { if (u_abr == 2) o *= 1.0 - hf; else o.rgb = mix(o.rgb, hazecol() * o.a, hf); }\n"
    "    if (fg > 0.0) { if (u_abr == 2) o *= 1.0 - fg; else o.rgb = mix(o.rgb, background() * o.a, fg); }\n"
    "}\n";

struct gpu_vert {
    float pa[3], pb[3];
    float uv[4];                    // u, v в A и в B
    uint32_t ca, cb;                // цвет в A и в B
    uint8_t flags[4];
    float sz[2];                    // экранные: глубина ortho в A и B; тень: запас глубины
    float fade;                     // M5b: проявление на α = 0 (≥ 1 — целиком)
    float hw[2];                    // M5b: высота в мире (вверх = −Y) в A и B
};

struct gpu_cmd {
    uint32_t first, count;          // в индексах
    uint16_t state;
    int8_t abr;                     // режим текстуры-варианта, −1 — обычная
    uint8_t trans, wide;
};

static struct gpu_vert gverts[BB_SNAP_MAX_VERTS];
static uint32_t gidx[BB_SNAP_MAX_PRIMS * 6];
// списки: [0] экран, [1] мир (акторы, полупрозрачное…), [2] задник (M5b: первым), [3] непрозрачные блоки комнат,
// [4] тени — после земли, до акторов: тень без записи глубины не ложится на ноги (их рисуют позже);
// [5] грани A без B (M4e) — состояния из A, после блоков, сдвинуты назад по глубине
#define NLIST 6
static struct gpu_cmd gcmd[NLIST][BB_SNAP_MAX_PRIMS];
static uint32_t gncmd[NLIST];

// --- Наклейки на земле ---
// Нора в LS01 (актор 034a6db4, текстуры 434/435, 32 вершины) — неглубокая ямка: край на высоте земли (0), середина
// на 20 ед. ниже, а земля над ней сплошная — PS1 рисовал её поверх земли порядком списка. С буфером глубины ямка
// уходила под землю, край спорил с землёй: на бегу мерцала, в иной точке не рисовалась вовсе (2026-10-17).
// Наклейка — актор из 3–64 вершин, почти плоский и горизонтальный: перепад высоты < 10 % ширины и ≤ 40 ед.
// Рисуется как тень — списком 4 после земли и до акторов, глубина подтянута к камере, без записи глубины.
// Камень или ящик сюда не попадает (высота сравнима с шириной), иначе сквозь них просвечивали бы акторы позади.
#define DC_BITS 12
static struct dc_obj { uint32_t obj; float lo, hi, x0, x1, z0, z1; uint32_t nv; } dc_tab[1 << DC_BITS];
static uint32_t dc_build = ~0u, dc_count;

static struct dc_obj *dc_find(uint32_t obj, int add)
{
    uint32_t h = (obj * 0x9e3779b1u) >> (32 - DC_BITS);
    for (uint32_t n = 0; n < (1u << DC_BITS); n++, h = (h + 1) & ((1u << DC_BITS) - 1)) {
        if (dc_tab[h].obj == obj)
            return &dc_tab[h];
        if (!dc_tab[h].obj) {
            if (!add)
                return NULL;
            dc_tab[h].obj = obj;
            dc_tab[h].lo = dc_tab[h].x0 = dc_tab[h].z0 = 1e30f;
            dc_tab[h].hi = dc_tab[h].x1 = dc_tab[h].z1 = -1e30f;
            dc_tab[h].nv = 0;
            return &dc_tab[h];
        }
    }
    return NULL;
}

static int dc_is_flat(const struct dc_obj *e)
{
    float h = e->hi - e->lo, wd = fmaxf(e->x1 - e->x0, e->z1 - e->z0);
    return e->nv >= 3 && e->nv <= 64 && h <= 40.0f && (h < 2.0f || h < 0.1f * wd);
}

static void dc_scan(const struct bb_snapshot *b)
{
    struct hr_cam k;
    static uint32_t logged_lvl = ~0u, logged_n = ~0u;
    if (b->build_id == dc_build)
        return;
    dc_build = b->build_id;
    memset(dc_tab, 0, sizeof dc_tab);
    dc_count = 0;
    if (!bb_cfg.decals || !hr_cam_of(b, &k))
        return;
    for (uint32_t i = 0; i < b->nprims; i++) {
        const struct bb_prim *p = &b->prims[i];
        const struct bb_vert *v0 = &b->verts[p->first];
        if (v0->tag != BB_TAG_ACTOR || !v0->obj)
            continue;
        struct dc_obj *e = dc_find(v0->obj, 1);
        if (!e)
            continue;
        for (unsigned j = 0; j < p->nverts; j++) {
            double q[3], w[3];
            to_view(&v0[j].x, 1.0 / 400.0, q);
            hr_world(&k, q, w);
            e->lo = fminf(e->lo, (float)-w[1]);
            e->hi = fmaxf(e->hi, (float)-w[1]);
            e->x0 = fminf(e->x0, (float)w[0]);
            e->x1 = fmaxf(e->x1, (float)w[0]);
            e->z0 = fminf(e->z0, (float)w[2]);
            e->z1 = fmaxf(e->z1, (float)w[2]);
            e->nv++;
        }
    }
    for (uint32_t h = 0; h < (1u << DC_BITS); h++)
        dc_count += dc_tab[h].obj && dc_is_flat(&dc_tab[h]);
    uint32_t lvl = b->level_gen;
    if (lvl != logged_lvl || (dc_count && logged_n == 0)) {
        logged_lvl = lvl;
        logged_n = dc_count;
        bb_log("наклейки на земле: %u объектов", dc_count);
    }
}

static int dc_flat(uint32_t obj)
{
    const struct dc_obj *e = dc_count ? dc_find(obj, 0) : NULL;
    return e && dc_is_flat(e);
}

static int prim_list(const struct bb_snapshot *b, const struct bb_prim *p)
{
    const struct bb_vert *v0 = &b->verts[p->first];
    if (!is_world(v0))
        return 0;
    if (v0->fl & BB_VF_SKY)
        return 2;
    if (v0->tag == BB_TAG_SHADOW)
        return 4;
    // плоское пятно-вычитание у актора (ABR 2, квад): нарисованная тень кактуса и т. п. — как тень: тонуло в земле
    const struct bb_state *st = &b->states[p->state];
    if (v0->tag == BB_TAG_ACTOR && p->nverts == 4 && st->caps & BB_CAP_TEXTURE && st->caps & BB_CAP_BLEND &&
        st->tex < sizeof variant_mode && variant_mode[st->tex] == 2)
        return 4;
    if (v0->tag == BB_TAG_BLOCK && !is_translucent(b, p))
        return 3;
    if (v0->tag == BB_TAG_ACTOR && dc_flat(v0->obj))     // наклейка на земле — как тень
        return 4;
    return 1;
}
static unsigned gprog, gvao, gvbo, gibo, gpu_gen, gpu_trans_mode;
static int gpu_state;               // 0 — не создан, 1 — готов, −1 — отказ (до нового контекста)
static int u_bgpass, u_ci, u_vp, u_scr, u_hz, u_hzTop, u_hzThick, u_hzNear, u_hzFar, u_hzMax, u_hzA0, u_hzA1, u_hzCol;
static int u_fadek, u_alpha, u_xs_world, u_xs_bb, u_fogs, u_tex, u_flat, u_afunc, u_abr, u_aref, u_gain, u_fogD, u_fogS, u_fadd, u_stp;

static unsigned gpu_shader(unsigned type, const char *src)
{
    char info[512];
    int ok = 0;
    unsigned sh = gx.CreateShader(type);
    gx.ShaderSource(sh, 1, &src, NULL);
    gx.CompileShader(sh);
    gx.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        info[0] = 0;
        gx.GetShaderInfoLog(sh, sizeof info, NULL, info);
        bb_log("M4b: шейдер %#x не собран: %s", type, info);
        return 0;
    }
    return sh;
}

// в нашем контексте (текущем)
static int gpu_init(void)
{
    char info[512];
    int ok = 0;

    if (gpu_state)
        return gpu_state > 0;
    gpu_state = -1;
#define X(f, n) (*(PROC *)&gx.f = gl.GetProcAddress(n))
    X(CreateShader, "glCreateShader"); X(ShaderSource, "glShaderSource"); X(CompileShader, "glCompileShader");
    X(GetShaderiv, "glGetShaderiv"); X(GetShaderInfoLog, "glGetShaderInfoLog"); X(CreateProgram, "glCreateProgram");
    X(AttachShader, "glAttachShader"); X(LinkProgram, "glLinkProgram"); X(GetProgramiv, "glGetProgramiv");
    X(GetProgramInfoLog, "glGetProgramInfoLog"); X(UseProgram, "glUseProgram");
    X(GetUniformLocation, "glGetUniformLocation"); X(Uniform1i, "glUniform1i"); X(Uniform1f, "glUniform1f");
    X(GenBuffers, "glGenBuffers"); X(BindBuffer, "glBindBuffer"); X(BufferData, "glBufferData");
    X(GenVertexArrays, "glGenVertexArrays"); X(BindVertexArray, "glBindVertexArray");
    X(VertexAttribPointer, "glVertexAttribPointer"); X(EnableVertexAttribArray, "glEnableVertexAttribArray");
    X(BlendEquation, "glBlendEquation"); X(ActiveTexture, "glActiveTexture");
    X(UniformMatrix3fv, "glUniformMatrix3fv"); X(Uniform3f, "glUniform3f"); X(Uniform4f, "glUniform4f");
    X(FramebufferTexture2D, "glFramebufferTexture2D"); X(GenerateMipmap, "glGenerateMipmap");
#undef X
    *(FARPROC *)&gx.DrawElements = GetProcAddress(GetModuleHandleA("opengl32.dll"), "glDrawElements");
    for (unsigned i = 0; i < sizeof gx / sizeof(void *); i++)
        if (!((void **)&gx)[i]) {
            bb_log("M4b: нет функции GL №%u — шейдеры выключены", i);
            return 0;
        }
    unsigned vs = gpu_shader(GL_VERTEX_SHADER, gpu_vs), fs = gpu_shader(GL_FRAGMENT_SHADER, gpu_fs);
    if (!vs || !fs)
        return 0;
    gprog = gx.CreateProgram();
    gx.AttachShader(gprog, vs);
    gx.AttachShader(gprog, fs);
    gx.LinkProgram(gprog);
    gx.GetProgramiv(gprog, GL_LINK_STATUS, &ok);
    if (!ok) {
        info[0] = 0;
        gx.GetProgramInfoLog(gprog, sizeof info, NULL, info);
        bb_log("M4b: программа не связана: %s", info);
        return 0;
    }
#define U(v) (v = gx.GetUniformLocation(gprog, #v))
    U(u_alpha); U(u_xs_world); U(u_xs_bb); U(u_fogs); U(u_tex); U(u_flat); U(u_afunc); U(u_abr); U(u_aref); U(u_gain); U(u_stp);
    U(u_fogD); U(u_fogS); U(u_fadd); U(u_fadek);
    U(u_bgpass); U(u_ci); U(u_vp); U(u_scr); U(u_hz); U(u_hzTop); U(u_hzThick); U(u_hzNear); U(u_hzFar);
    U(u_hzMax); U(u_hzA0); U(u_hzA1); U(u_hzCol);
#undef U
    gx.UseProgram(gprog);
    gx.Uniform1i(gx.GetUniformLocation(gprog, "u_smp"), 0);
    gx.Uniform1i(gx.GetUniformLocation(gprog, "u_sky"), 1);
    gx.UseProgram(0);
    gx.GenVertexArrays(1, &gvao);
    gx.GenBuffers(1, &gvbo);
    gx.GenBuffers(1, &gibo);
    gx.BindVertexArray(gvao);
    gx.BindBuffer(GL_ARRAY_BUFFER, gvbo);
    gx.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, gibo);   // привязка индексов — состояние VAO
    const int st = sizeof(struct gpu_vert);
    gx.VertexAttribPointer(0, 3, GL_FLOAT, 0, st, (void *)offsetof(struct gpu_vert, pa));
    gx.VertexAttribPointer(1, 3, GL_FLOAT, 0, st, (void *)offsetof(struct gpu_vert, pb));
    gx.VertexAttribPointer(2, 4, GL_FLOAT, 0, st, (void *)offsetof(struct gpu_vert, uv));
    gx.VertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, 1, st, (void *)offsetof(struct gpu_vert, ca));
    gx.VertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, 1, st, (void *)offsetof(struct gpu_vert, cb));
    gx.VertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, 0, st, (void *)offsetof(struct gpu_vert, flags));
    gx.VertexAttribPointer(6, 2, GL_FLOAT, 0, st, (void *)offsetof(struct gpu_vert, sz));
    gx.VertexAttribPointer(7, 1, GL_FLOAT, 0, st, (void *)offsetof(struct gpu_vert, fade));
    gx.VertexAttribPointer(8, 2, GL_FLOAT, 0, st, (void *)offsetof(struct gpu_vert, hw));
    for (unsigned i = 0; i < 9; i++)
        gx.EnableVertexAttribArray(i);
    gx.BindVertexArray(0);
    gpu_gen = 0;
    gpu_state = 1;
    bb_log("M4b: шейдеры готовы: program %u, VAO %u, VBO %u/%u, err=%#x", gprog, gvao, gvbo, gibo, gl.GetError());
    return 1;
}

static void gpu_forget(void)        // наш контекст удалён: объекты ушли вместе с ним
{
    gpu_state = 0;
    gprog = gvao = gvbo = gibo = 0;
}

static void ndc_xy(double x, double y, const double *o, float *out)
{
    out[0] = (float)((2.0 * x - (o[1] + o[0])) / (o[1] - o[0]));
    out[1] = (float)((2.0 * y - (o[3] + o[2])) / (o[3] - o[2]));
}

// P15: примитивы-варианты по режимам ABR (мир / экран), за ~1 с
static uint32_t p15_abr[2][4];
static void p23_log(void);
static void p27_log(void);
static void pair_take(const LARGE_INTEGER *q);

static void p15_log(void)
{
    static DWORD last;
    DWORD now = GetTickCount();
    if (now - last < 1000)
        return;
    last = now;
    bb_log_mute = !bb_cfg.log_sec;
    bb_log("P15 ABR мир %lu/%lu/%lu/%lu экран %lu/%lu/%lu/%lu", (unsigned long)p15_abr[1][0], (unsigned long)p15_abr[1][1],
           (unsigned long)p15_abr[1][2], (unsigned long)p15_abr[1][3], (unsigned long)p15_abr[0][0],
           (unsigned long)p15_abr[0][1], (unsigned long)p15_abr[0][2], (unsigned long)p15_abr[0][3]);
    memset(p15_abr, 0, sizeof p15_abr);
    p16_log();
    p17_log();
    p23_log();
    p27_log();
    bb_log_mute = 0;
}

// отладка: [live] dump=1 — пара снимков в BIN\\pair_dump.txt (каждая вершина B с парой в A), ключ сбрасывается
int bb_dump_req;

static void dump_pair(const struct bb_snapshot *a, const struct bb_snapshot *b)
{
    char path[MAX_PATH], line[400];
    DWORD n = GetModuleFileNameA(bb_self, path, MAX_PATH), wr;
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/')
        n--;
    lstrcpyA(path + n, "pair_dump.txt");
    HANDLE f = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return;
    int len = wsprintfA(line, "# build %lu -> %lu, prims %lu, verts %lu\r\n", (unsigned long)(a ? a->build_id : 0),
                        (unsigned long)b->build_id, (unsigned long)b->nprims, (unsigned long)b->nverts);
    WriteFile(f, line, (DWORD)len, &wr, NULL);
    for (uint32_t i = 0; i < b->nprims; i++) {
        const struct bb_prim *p = &b->prims[i];
        const struct bb_state *st = &b->states[p->state];
        for (unsigned k = 0; k < p->nverts; k++) {
            uint32_t vi = p->first + k;
            const struct bb_vert *v = &b->verts[vi];
            int32_t j = a ? match_idx[vi] : -1;
            const struct bb_vert *u = j >= 0 ? &a->verts[j] : v;
            // целые ×100 / ×10000: wsprintf не печатает float
            uint32_t act = 0;
            int atype = v->tag == BB_TAG_ACTOR ? bb_part_type(v->obj, &act) : -1;
            len = wsprintfA(line, "%lu %u %s %08lx %u tex=%u caps=%x abr=%d | %d %d %d %d %d %08lx | %d %d %d %d %d %08lx | j=%ld jump=%d bad=%d syn=%d type=%d actor=%08lx\r\n",
                            (unsigned long)i, k, bb_tag_name(v->tag), (unsigned long)v->obj, v->local, st->tex, st->caps,
                            st->tex < sizeof variant_mode ? variant_mode[st->tex] : -1,
                            (int)(v->x * 100), (int)(v->y * 100), (int)(v->z * 100), (int)(v->u * 10000), (int)(v->v * 10000),
                            (unsigned long)v->rgba, (int)(u->x * 100), (int)(u->y * 100), (int)(u->z * 100),
                            (int)(u->u * 10000), (int)(u->v * 10000), (unsigned long)u->rgba, (long)j, j >= 0 ? vjump[vi] : -1,
                            v->obj ? bad_has(v->obj) : -1, vsyn[vi], atype, (unsigned long)act);
            WriteFile(f, line, (DWORD)len, &wr, NULL);
        }
    }
    CloseHandle(f);
    bb_log("dump: пара %lu -> %lu записана в %s", (unsigned long)(a ? a->build_id : 0), (unsigned long)b->build_id, path);
    if (!a)
        return;
    // снимок A целиком (грани A — для сопоставления граней: uv, цвет, текстура), формат тот же, пара — сама вершина
    lstrcpyA(path + n, "pair_dump_a.txt");
    f = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return;
    len = wsprintfA(line, "# build %lu (A), prims %lu, verts %lu\r\n", (unsigned long)a->build_id, (unsigned long)a->nprims,
                    (unsigned long)a->nverts);
    WriteFile(f, line, (DWORD)len, &wr, NULL);
    for (uint32_t i = 0; i < a->nprims; i++) {
        const struct bb_prim *p = &a->prims[i];
        const struct bb_state *st = &a->states[p->state];
        for (unsigned k = 0; k < p->nverts; k++) {
            const struct bb_vert *v = &a->verts[p->first + k];
            int x = (int)(v->x * 100), y = (int)(v->y * 100), z = (int)(v->z * 100), tu = (int)(v->u * 10000), tv = (int)(v->v * 10000);
            len = wsprintfA(line, "%lu %u %s %08lx %u tex=%u caps=%x abr=%d | %d %d %d %d %d %08lx | %d %d %d %d %d %08lx | j=-1 jump=-1 bad=-1 syn=0\r\n",
                            (unsigned long)i, k, bb_tag_name(v->tag), (unsigned long)v->obj, v->local, st->tex, st->caps,
                            st->tex < sizeof variant_mode ? variant_mode[st->tex] : -1, x, y, z, tu, tv,
                            (unsigned long)v->rgba, x, y, z, tu, tv, (unsigned long)v->rgba);
            WriteFile(f, line, (DWORD)len, &wr, NULL);
        }
    }
    CloseHandle(f);
}

static void gpu_cmd_add(int list, uint32_t first, uint32_t n, uint16_t state, int abr, int trans, int wide)
{
    struct gpu_cmd *c = gncmd[list] ? &gcmd[list][gncmd[list] - 1] : NULL;
    if (c && c->first + c->count == first && c->state == state && c->abr == abr && c->trans == trans &&
        c->wide == wide) {
        c->count += n;
        return;
    }
    c = &gcmd[list][gncmd[list]++];
    c->first = first;
    c->count = n;
    c->state = state;
    c->abr = (int8_t)abr;
    c->trans = (uint8_t)trans;
    c->wide = (uint8_t)wide;
}

// --- M5b: плавное появление объектов ---
// Объект мира (ключ obj), которого не было в прошлом тике и который появился дальше FADE_W (дальние акторы по M5-AR,
// дальние комнаты, их тени), проявляется дизерингом за FADE_TICKS тиков, а не «выскакивает». Ближние появления
// (выстрелы, эффекты) — сразу. M4e: только если объект был бы виден камерой A (поза B при камере A в кадре): въехавшие
// из-за края при повороте и появившиеся при смене ракурса — сразу (сеткой проявлялись кромка и весь новый вид).
#define FADE_TICKS 8
#define FADE_W 2000.0f
#define FD_BITS 13
static struct fd_obj { uint32_t obj, last, first; uint8_t distant, inview; } fd_tab[1 << FD_BITS];
static uint32_t fd_used;

static struct fd_obj *fd_find(uint32_t obj)
{
    uint32_t h = (obj * 2654435761u) >> (32 - FD_BITS);
    while (fd_tab[h].obj && fd_tab[h].obj != obj)
        h = (h + 1) & ((1u << FD_BITS) - 1);
    if (!fd_tab[h].obj) {
        fd_tab[h].obj = obj;
        fd_tab[h].last = fd_tab[h].first = 0;
        fd_tab[h].distant = fd_tab[h].inview = 0;
        fd_used++;
    }
    return &fd_tab[h];
}

static int fd_in_a(const struct bb_vert *v, const double *o)   // вершина B при камере A — в кадре A
{
    double p[3], w[3], q[3];
    float sc[3], n[2];
    to_view(&v->x, 1.0 / 400.0, p);
    hr_world(&rkb, p, w);
    hr_view(&rka, w, q);
    if (q[2] > -Z_NEAR || !hr_screen(q, 1.0 / 400.0, sc))
        return 0;
    ndc_xy(sc[0], sc[1], o, n);
    return fabs(n[0] * world_xscale) <= 1.0 && fabs(n[1]) <= 1.0;
}

static void fd_scan(const struct bb_snapshot *a, const struct bb_snapshot *b)
{
    const double *wo = NULL;                        // ortho мира (первая грань мира)
    for (uint32_t i = 0; i < b->nprims && !wo; i++)
        if (is_world(&b->verts[b->prims[i].first]))
            wo = b->states[b->prims[i].state].ortho;
    int test = a && !cam_cut && wo;
    if (fd_used > (1u << FD_BITS) / 2) {            // переполнение (новые адреса) — живые за два тика оставить
        static struct fd_obj old[1 << FD_BITS];
        memcpy(old, fd_tab, sizeof old);
        memset(fd_tab, 0, sizeof fd_tab);
        fd_used = 0;
        for (uint32_t i = 0; i < (1u << FD_BITS); i++)
            if (old[i].obj && old[i].last + 2 >= b->build_id)
                *fd_find(old[i].obj) = old[i];
    }
    for (uint32_t i = 0; i < b->nverts; i++) {
        const struct bb_vert *v = &b->verts[i];
        if (!v->obj || !is_world(v) || v->fl & BB_VF_SKY)
            continue;
        struct fd_obj *o = fd_find(v->obj);
        if (o->last != b->build_id) {               // первая вершина объекта в этом тике
            if (o->last + 1 < b->build_id) {        // в прошлом тике не было — появился
                o->first = b->build_id;
                o->distant = o->last != 0 || b->build_id > 2;
                o->inview = !test;                  // без камеры A — как раньше
                fd_sec += o->distant;
            }
            o->last = b->build_id;
        }
        if (o->first == b->build_id && -v->z < FADE_W)
            o->distant = 0;                          // хоть одна вершина ближе — появляется сразу
        if (o->first == b->build_id && o->distant && !o->inview && test && fd_in_a(v, wo))
            o->inview = 1;
    }
}

static float fd_fade(const struct bb_snapshot *b, const struct bb_vert *v)
{
    if (!v->obj || !bb_cfg.fade_in)
        return 1.0f;
    struct fd_obj *o = fd_find(v->obj);
    return o->distant && o->inview ? (float)(b->build_id - o->first) / FADE_TICKS : 1.0f;
}

// --- M5 HUD: элементы по краям широкого экрана ---
// Экранные примитивы (кроме полноширинных оверлеев) собираются в элементы по соседству (иконка + цифры рядом); элемент
// целиком в левой/правой трети 4:3 и в верхней/нижней полосе (|y| > 0,45) прижимается к краю окна, остальное (меню,
// заголовки, центр) — как было, в 4:3. Так строки меню не рвутся, а счётчики уезжают к краям (решение Д6, 2026-10-16).
static double hud_k = 1.0;                      // ширина 4:3 / ширина окна
static uint8_t hud_anchor[BB_SNAP_MAX_PRIMS];   // 0 — как есть, 1 — к левому краю, 2 — к правому
static int hud_par[BB_SNAP_MAX_PRIMS];
static uint32_t hud_idx[2048];
static float hud_box[2048][4];
static uint32_t p29_left, p29_right;

static int hud_root(int i)
{
    while (hud_par[i] != i)
        i = hud_par[i] = hud_par[hud_par[i]];
    return i;
}

static void hud_scan(const struct bb_snapshot *b)
{
    uint32_t n = 0;
    memset(hud_anchor, 0, b->nprims);
    if (!bb_cfg.hud_edges || hud_k >= 0.999)
        return;
    for (uint32_t i = 0; i < b->nprims && n < 2048; i++) {
        const struct bb_prim *p = &b->prims[i];
        if (is_world(&b->verts[p->first]) || is_wide_overlay(b, p))
            continue;
        const double *o = b->states[p->state].ortho;
        float x0 = 9, x1 = -9, y0 = 9, y1 = -9, q[2];
        for (unsigned k = 0; k < p->nverts; k++) {
            const struct bb_vert *v = &b->verts[p->first + k];
            ndc_xy(v->x, v->y, o, q);
            x0 = fminf(x0, q[0]); x1 = fmaxf(x1, q[0]);
            y0 = fminf(y0, q[1]); y1 = fmaxf(y1, q[1]);
        }
        hud_idx[n] = i;
        hud_box[n][0] = x0; hud_box[n][1] = x1; hud_box[n][2] = y0; hud_box[n][3] = y1;
        hud_par[n] = (int)n;
        n++;
    }
    for (uint32_t i = 0; i < n; i++)                // соседи: зазор по x ≤ 0,06, по y перекрытие или зазор ≤ 0,02
        for (uint32_t j = i + 1; j < n; j++) {
            float gx = fmaxf(hud_box[j][0] - hud_box[i][1], hud_box[i][0] - hud_box[j][1]);
            float gy = fmaxf(hud_box[j][2] - hud_box[i][3], hud_box[i][2] - hud_box[j][3]);
            if (gx <= 0.06f && gy <= 0.02f) {
                int ri = hud_root((int)i), rj = hud_root((int)j);
                if (ri != rj)
                    hud_par[ri] = rj;
            }
        }
    static float cb[2048][4];
    for (uint32_t i = 0; i < n; i++)
        cb[i][0] = cb[i][2] = 9, cb[i][1] = cb[i][3] = -9;
    for (uint32_t i = 0; i < n; i++) {
        int r = hud_root((int)i);
        cb[r][0] = fminf(cb[r][0], hud_box[i][0]); cb[r][1] = fmaxf(cb[r][1], hud_box[i][1]);
        cb[r][2] = fminf(cb[r][2], hud_box[i][2]); cb[r][3] = fmaxf(cb[r][3], hud_box[i][3]);
    }
    for (uint32_t i = 0; i < n; i++) {
        int r = hud_root((int)i);
        int band = cb[r][2] > 0.45f || cb[r][3] < -0.45f;
        if (band && cb[r][1] < -0.25f) {
            hud_anchor[hud_idx[i]] = 1;
            p29_left++;
        } else if (band && cb[r][0] > 0.25f) {
            hud_anchor[hud_idx[i]] = 2;
            p29_right++;
        }
    }
}

// --- Титул: плашки ближе к камере ---
// Логотип титула (уровень 0) — плоский актор в сцене (z −2160…−2120), морковка машины времени (до −2062) его протыкает
// (так и в оригинале). Панель меню («Загрузка / Новая игра / Выход», слоты) — тоже плоский актор, z −2000, перед
// логотипом; плашка «CITY…» внизу — z −871. Глубина всех крупных плоских акторов умножается на title_logo/100:
// на экране они те же (x, y — экранные), порядок между собой сохраняется (общий множитель), а машина — позади.
// 85 %: логотип −1836…−1802 перед машиной (её ближние части −1916…−2062), панель −1700 — за Багзом (−1200…−1290,
// кончики ушей заходят на рамку панели; при 60 % панель закрывала бы их).
// Плашка: актор из 4–64 вершин, разброс z < 3 %, шире 0,5 экрана. Замер 2026-10-17 (снимок кадра меню).
#define TITLE_MAX 8
static uint32_t title_objs[TITLE_MAX], title_n;

static int title_has(uint32_t obj)
{
    for (uint32_t i = 0; i < title_n; i++)
        if (title_objs[i] == obj)
            return 1;
    return 0;
}

static void title_scan(const struct bb_snapshot *b)
{
    uint32_t ram = *(volatile uint32_t *)(uintptr_t)0x52fd00u, tried[64], ntried = 0;
    static uint32_t logged_n, logged_sum;
    title_n = 0;
    if (bb_cfg.title_logo <= 0 || bb_cfg.title_logo >= 100 || !ram || *(volatile uint32_t *)(uintptr_t)(ram + 0x10000) != 0)
        return;
    for (uint32_t i = 0; i < b->nprims && title_n < TITLE_MAX; i++) {
        const struct bb_vert *v0 = &b->verts[b->prims[i].first];
        if (v0->tag != BB_TAG_ACTOR || !v0->obj)
            continue;
        uint32_t t = 0;
        while (t < ntried && tried[t] != v0->obj)
            t++;
        if (t < ntried)
            continue;
        if (ntried == 64)
            break;
        tried[ntried++] = v0->obj;
        uint32_t nv = 0;
        float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f, q[2];
        for (uint32_t j = i; j < b->nprims; j++) {
            const struct bb_prim *p = &b->prims[j];
            if (b->verts[p->first].obj != v0->obj)
                continue;
            for (unsigned k = 0; k < p->nverts; k++) {
                const struct bb_vert *v = &b->verts[p->first + k];
                ndc_xy(v->x, v->y, b->states[p->state].ortho, q);
                x0 = fminf(x0, q[0]); x1 = fmaxf(x1, q[0]);
                z0 = fminf(z0, v->z); z1 = fmaxf(z1, v->z);
                nv++;
            }
        }
        if (nv >= 4 && nv <= 64 && z1 < 0 && z1 - z0 < -z1 * 0.03f && x1 - x0 > 0.5f)
            title_objs[title_n++] = v0->obj;
    }
    uint32_t sum = 0;
    for (uint32_t i = 0; i < title_n; i++)
        sum += title_objs[i];
    if (title_n != logged_n || sum != logged_sum) {
        logged_n = title_n;
        logged_sum = sum;
        bb_log("титул: плашек ближе к камере %u (глубина ×%d%%)", title_n, bb_cfg.title_logo);
    }
}

// вершины, индексы и списки — при смене пары снимков (или trans_mode)
// --- M4e: грани A, которых нет в B ---
// Рисуются только грани B: грань, которую движок отсёк в B (ушла за край кадра B при повороте, объект пропал),
// исчезала уже в начале тика, хотя промежуточный кадр показывает камеру между A и B, — у края, откуда уходит поворот,
// была дыра. Такая грань дорисовывается до конца тика: из позы A в позу A при камере B (неподвижная в мире).
// Только непрозрачные блоки и акторы, пропавшие целиком; грань сдвинута по глубине назад — закрывает лишь дыры.
#define PK_BITS 16
#define OB_BITS 13
static uint64_t pk_tab[1 << PK_BITS];
static uint32_t ob_tab[1 << OB_BITS], ob_gen[1 << OB_BITS], ob_cur, pk_gen;
static uint32_t p24_ghost, p24_ghost_skip;
static uint32_t ghost_src[BB_SNAP_MAX_VERTS];    // вершина A у дорисованной (для trace_full)
static uint32_t gup_nv, gup_ni;                  // вершин и индексов в буферах пары (с гранями A)

static uint64_t pk_mix(uint32_t obj, uint16_t local)
{
    uint64_t x = ((uint64_t)obj << 16 | local) * 0x9e3779b97f4a7c15ull;
    return x ^ x >> 29;
}

static uint64_t prim_key(const struct bb_snapshot *s, const struct bb_prim *p)   // набор вершин грани, 0 — без ключа
{
    uint64_t h = 0;
    for (unsigned k = 0; k < p->nverts; k++) {
        const struct bb_vert *v = &s->verts[p->first + k];
        if (!v->obj)
            return 0;
        h += pk_mix(v->obj, v->local);
    }
    return h ? h : 1;
}

static int edge_kind(const struct bb_snapshot *s, const struct bb_prim *p)  // 0 — блок, 1 — актор, −1 — не считаем
{
    const struct bb_vert *v = &s->verts[p->first];
    if (!is_world(v) || v->fl & BB_VF_SKY)
        return -1;
    return v->tag == BB_TAG_BLOCK ? 0 : v->tag == BB_TAG_ACTOR ? 1 : -1;
}

static int pk_has(uint64_t k)
{
    const uint32_t m = (1u << PK_BITS) - 1;
    for (uint32_t h = (uint32_t)(k >> 48) & m; pk_tab[h]; h = (h + 1) & m)
        if (pk_tab[h] == k)
            return 1;
    return 0;
}

static int ob_has(uint32_t o)
{
    const uint32_t m = (1u << OB_BITS) - 1;
    for (uint32_t h = (o * 2654435761u) >> (32 - OB_BITS); ob_gen[h] == ob_cur; h = (h + 1) & m)
        if (ob_tab[h] == o)
            return 1;
    return 0;
}

static void pk_build(const struct bb_snapshot *b)   // ключи граней и объекты B, по разу на пару
{
    if (pk_gen == pair_gen)
        return;
    pk_gen = pair_gen;
    const uint32_t pm = (1u << PK_BITS) - 1, om = (1u << OB_BITS) - 1;
    memset(pk_tab, 0, sizeof pk_tab);
    ob_cur++;
    for (uint32_t i = 0; i < b->nverts; i++) {
        uint32_t o = b->verts[i].obj, h;
        if (!o)
            continue;
        for (h = (o * 2654435761u) >> (32 - OB_BITS); ob_gen[h] == ob_cur && ob_tab[h] != o; h = (h + 1) & om)
            ;
        ob_tab[h] = o;
        ob_gen[h] = ob_cur;
    }
    for (uint32_t i = 0; i < b->nprims; i++) {
        uint64_t k = edge_kind(b, &b->prims[i]) >= 0 ? prim_key(b, &b->prims[i]) : 0;
        if (!k)
            continue;
        uint32_t h = (uint32_t)(k >> 48) & pm;
        while (pk_tab[h] && pk_tab[h] != k)
            h = (h + 1) & pm;
        pk_tab[h] = k;
    }
}

// вершины граней A без B — в gverts с nv, индексы — с ni, команды — в список 5; возвращает число вершин
static uint32_t ghost_build(const struct bb_snapshot *a, const struct bb_snapshot *b, uint32_t nv, uint32_t ni)
{
    gncmd[5] = 0;
    if (!a || !bb_cfg.ghost || cam_cut)
        return 0;
    pk_build(b);
    const double ih = 1.0 / 400.0;
    uint32_t v0 = nv;
    for (uint32_t i = 0; i < a->nprims; i++) {
        const struct bb_prim *p = &a->prims[i];
        int kd = edge_kind(a, p), list = kd >= 0 ? prim_list(a, p) : -1;
        if (list != 3 && !(list == 1 && kd == 1))
            continue;
        const struct bb_state *st = &a->states[p->state];
        if (is_translucent(a, p) || st->caps & BB_CAP_BLEND)
            continue;
        uint64_t k = prim_key(a, p);
        if (!k || pk_has(k) || (kd == 1 && ob_has(a->verts[p->first].obj)))
            continue;                   // грань есть в B; у актора — объект есть в B (сменился кадр анимации)
        if (nv + p->nverts > BB_SNAP_MAX_VERTS || ni + 6 > BB_SNAP_MAX_PRIMS * 6) {
            p24_ghost_skip++;
            break;
        }
        int ok = 1;
        for (unsigned m = 0; m < p->nverts && ok; m++) {
            const struct bb_vert *v = &a->verts[p->first + m];
            struct gpu_vert *g = &gverts[nv + m];
            double q[3], w[3], r[3];
            float sb[3];
            to_view(&v->x, ih, q);
            hr_world(&rka, q, w);
            hr_view(&rkb, w, r);
            if (v->z > -REP_ZMIN || !hr_screen(r, ih, sb)) {   // B за камерой — отсекает GPU (вид линеен)
                ok = 0;
                break;
            }
            ndc_xy(v->x, v->y, st->ortho, g->pa);
            g->pa[2] = v->z;
            ndc_xy(sb[0], sb[1], st->ortho, g->pb);
            g->pb[2] = sb[2];
            g->flags[0] = g->flags[1] = g->flags[2] = 0;
            g->flags[3] = (uint8_t)(v->fl & BB_VF_NOFOG);
            g->sz[0] = g->sz[1] = 0;
            g->fade = 1.0f;
            g->hw[0] = g->hw[1] = (float)-w[1];
            g->uv[0] = g->uv[2] = v->u;
            g->uv[1] = g->uv[3] = v->v;
            g->ca = g->cb = v->rgba;
            ghost_src[nv + m] = p->first + m;
        }
        if (!ok) {
            p24_ghost_skip++;
            continue;
        }
        uint32_t *ix = &gidx[ni];
        ix[0] = nv; ix[1] = nv + 1; ix[2] = nv + 2;
        uint32_t n = 3;
        if (p->nverts == 4) {
            ix[3] = nv; ix[4] = nv + 2; ix[5] = nv + 3;
            n = 6;
        }
        gpu_cmd_add(5, ni, n, p->state, -1, 0, 0);
        nv += p->nverts;
        ni += n;
        p24_ghost++;
    }
    return nv - v0;
}

// --- M8: Багз не утопает в толкаемом камне (только картинка) ---
// Форма столкновения камня (актор типа 5) в логике смещена относительно меша: толкая с одной стороны, Багз держится
// в 170 от центра и уходит в меш на ~65 ед., с другой — в 240, почти касаясь (замер L01A 2026-10-16; так и в оригинале,
// и на PS1). Логику не трогаем (головоломки): мировой прямоугольник камня — из его вершин в снимке, «цилиндр» Багза
// радиуса push_r; при пересечении модель Багза сдвигается наружу по кратчайшей оси. Сдвиг считается на каждом тике
// и интерполируется. Логическая позиция, тень и частицы — как были.
#define PU_PARTS 128
static uint32_t pu_ply_parts[PU_PARTS], pu_nply, pu_player;
static struct { uint32_t build; double d[3]; } pu_cache[4];
static uint32_t p30_ticks;
static double p30_max;

static int pu_is_ply(uint32_t obj)
{
    for (uint32_t i = 0; i < pu_nply; i++)
        if (pu_ply_parts[i] == obj)
            return 1;
    return 0;
}

static const double *pu_get(uint32_t build)
{
    for (int i = 0; i < 4; i++)
        if (pu_cache[i].build == build)
            return pu_cache[i].d;
    return NULL;
}

// сдвиг Багза (мир) для снимка B — по живым акторам (логика следующего тика ещё не шла) и вершинам камня в снимке
static void pu_compute(const struct bb_snapshot *b)
{
    if (pu_get(b->build_id))
        return;
    double d[3] = { 0, 0, 0 };
    uint32_t P = *(volatile uint32_t *)(uintptr_t)0x4b324cu;
    if (P != pu_player) {                       // части игрока: список +0x78, узел [0] — следующий
        pu_player = P;
        pu_nply = 0;
        for (uint32_t part = P ? *(volatile uint32_t *)(uintptr_t)(P + 0x78) : 0; part && pu_nply < PU_PARTS;
             part = *(volatile uint32_t *)(uintptr_t)part)
            pu_ply_parts[pu_nply++] = part;
    }
    uint32_t pool = *(volatile uint32_t *)(uintptr_t)0x4b24a0u;
    int cnt = *(volatile uint16_t *)(uintptr_t)0x4b28c0u;
    double cx = b->ply[0], cy = b->ply[1], cz = b->ply[2], R = bb_cfg.push_r;
    struct hr_cam k;
    if (bb_cfg.push_r > 0 && P && pool && cnt > 0 && cnt < 2048 && hr_cam_of(b, &k)) {
        double best = 0;
        for (int i = 0; i < cnt; i++) {
            uint32_t a = pool + 500u * (uint32_t)i;
            if (a == P || *(volatile int16_t *)(uintptr_t)(a + 0x2c) != 5)
                continue;
            double ax = *(volatile int32_t *)(uintptr_t)(a + 0xe4), az = *(volatile int32_t *)(uintptr_t)(a + 0xec);
            uint32_t part = *(volatile uint32_t *)(uintptr_t)(a + 0x78);
            if (!part || fabs(ax - cx) > 800 || fabs(az - cz) > 800)
                continue;
            double x0 = 1e30, x1 = -1e30, y0 = 1e30, y1 = -1e30, z0 = 1e30, z1 = -1e30;
            int nv = 0;
            for (uint32_t vi = 0; vi < b->nverts; vi++) {     // мировой прямоугольник меша камня (первая часть)
                const struct bb_vert *v = &b->verts[vi];
                if (v->obj != part)
                    continue;
                double q[3], w[3];
                to_view(&v->x, 1.0 / 400.0, q);
                hr_world(&k, q, w);
                x0 = fmin(x0, w[0]); x1 = fmax(x1, w[0]);
                y0 = fmin(y0, w[1]); y1 = fmax(y1, w[1]);
                z0 = fmin(z0, w[2]); z1 = fmax(z1, w[2]);
                nv++;
            }
            if (nv < 4 || cy < y0 + 50 || y1 < cy - 300)  // стоит на камне (верх −Y) или камень не по росту — не трогать
                continue;
            if (cx <= x0 - R || cx >= x1 + R || cz <= z0 - R || cz >= z1 + R)
                continue;
            double m[4] = { cx - (x0 - R), (x1 + R) - cx, cz - (z0 - R), (z1 + R) - cz };   // −x, +x, −z, +z
            int j = 0;
            for (int t = 1; t < 4; t++)
                if (m[t] < m[j])
                    j = t;
            if (m[j] > 120 || m[j] <= best)
                continue;
            best = m[j];
            d[0] = j == 0 ? -m[j] : j == 1 ? m[j] : 0;
            d[2] = j == 2 ? -m[j] : j == 3 ? m[j] : 0;
        }
        if (best > 0) {
            p30_ticks++;
            p30_max = fmax(p30_max, best);
        }
    }
    static int slot;
    pu_cache[slot].build = b->build_id;
    memcpy(pu_cache[slot].d, d, sizeof d);
    slot = (slot + 1) & 3;
}

// экранная вершина (X, Y экрана движка, Z вида) + мировой сдвиг при камере снимка
static void pu_shift(const float *in, const struct bb_snapshot *s, const double *d, float *out)
{
    struct hr_cam k;
    double q[3];
    if (!hr_cam_of(s, &k)) {
        memcpy(out, in, 3 * sizeof *out);
        return;
    }
    to_view(in, 1.0 / 400.0, q);
    for (int r = 0; r < 3; r++)
        q[r] += k.c[3 * r] * d[0] + k.c[3 * r + 1] * d[1] + k.c[3 * r + 2] * d[2];
    if (!hr_screen(q, 1.0 / 400.0, out))
        memcpy(out, in, 3 * sizeof *out);
}

static void gpu_upload(const struct bb_snapshot *a, const struct bb_snapshot *b)
{
    static int hud_was = -1;
    static double hud_k_was;
    if (gpu_gen == pair_gen && gpu_trans_mode == (unsigned)bb_cfg.trans_mode && hud_was == bb_cfg.hud_edges &&
        hud_k_was == hud_k)
        return;
    gpu_gen = pair_gen;
    gpu_trans_mode = (unsigned)bb_cfg.trans_mode;
    hud_was = bb_cfg.hud_edges;
    hud_k_was = hud_k;
    hud_scan(b);
    title_scan(b);
    dc_scan(b);
    pu_compute(b);
    const double *pdb = pu_get(b->build_id), *pda = a ? pu_get(a->build_id) : NULL;
    int pu_on = pdb && pu_nply && ((pdb[0] != 0 || pdb[2] != 0) || (pda && (pda[0] != 0 || pda[2] != 0)));
    static const double pzero[3] = { 0, 0, 0 };
    uint32_t nidx[NLIST] = { 0 }, start[NLIST] = { 0 }, ntotal = 0;
    for (uint32_t i = 0; i < b->nprims; i++) {          // диапазон индексов на каждый список
        const struct bb_prim *p = &b->prims[i];
        nidx[prim_list(b, p)] += p->nverts == 4 ? 6 : 3;
    }
    fd_scan(a, b);
    struct hr_cam ka, kb;                               // M5b: высота вершин в мире для дымки
    hr_cam_of(b, &kb);
    if (a)
        hr_cam_of(a, &ka);
    else
        ka = kb;
    for (int l = 0; l < NLIST; l++) {
        start[l] = ntotal;
        ntotal += nidx[l];
        nidx[l] = 0;
        gncmd[l] = 0;
    }
    for (uint32_t i = 0; i < b->nprims; i++) {
        const struct bb_prim *p = &b->prims[i];
        const struct bb_state *st = &b->states[p->state];
        const double *o = st->ortho;
        int world = is_world(&b->verts[p->first]);
        int list = prim_list(b, p);
        // uv и цвет — из B: пара по вершине пула не означает пару граней (вершина входит в несколько граней с разными
        // uv — замер 2026-10-16: «сдвиги» ±0,98 у соседних граней). Атрибуты A оставлены под сопоставление граней.
        for (unsigned k = 0; k < p->nverts; k++) {
            uint32_t vi = p->first + k;
            const struct bb_vert *v = &b->verts[vi];
            struct gpu_vert *g = &gverts[vi];
            const float *u = pair_a_half(a, b, vi), *ub = u ? u : &v->x, *w = u ? pair_b_half(b, vi) : &v->x;
            float pu_b[3], pu_a[3];
            if (pu_on && world && v->tag == BB_TAG_ACTOR && pu_is_ply(v->obj)) {   // Багз у камня — наружу
                pu_shift(w, b, pdb, pu_b);
                w = pu_b;
                if (u) {
                    pu_shift(u, a, pda ? pda : pzero, pu_a);
                    u = pu_a;
                }
            }
            ndc_xy(w[0], w[1], o, g->pb);
            g->pb[2] = w[2];
            if (u) {
                ndc_xy(u[0], u[1], o, g->pa);
                g->pa[2] = u[2];
            } else {
                memcpy(g->pa, g->pb, sizeof g->pa);
            }
            if (world && title_n && title_has(v->obj)) {      // плашки титула — ближе к камере
                g->pa[2] *= (float)bb_cfg.title_logo / 100.0f;
                g->pb[2] *= (float)bb_cfg.title_logo / 100.0f;
            }
            if (world) {
                g->flags[0] = v->tag == BB_TAG_BB;
                g->flags[1] = 0;
                g->sz[0] = g->sz[1] = 0;
            } else {                    // экран: 3D-HUD и спрайты у камеры с ключом сглаживаются той же формулой
                if (hud_anchor[i]) {    // к краю: x 4:3 → x окна (рисуется в широком вьюпорте)
                    float sh = (float)(hud_anchor[i] == 1 ? hud_k - 1.0 : 1.0 - hud_k);
                    g->pa[0] = (float)(g->pa[0] * hud_k) + sh;
                    g->pb[0] = (float)(g->pb[0] * hud_k) + sh;
                }
                g->flags[0] = 0;
                g->flags[1] = 1;
                g->sz[1] = (float)((-2.0 * w[2] - (o[5] + o[4])) / (o[5] - o[4]));
                g->sz[0] = (float)((-2.0 * ub[2] - (o[5] + o[4])) / (o[5] - o[4]));
            }
            g->flags[2] = v->tag == BB_TAG_BB;   // спрайт: цвет 1.0 = нейтраль PS1
            g->flags[3] = (uint8_t)((world && v->fl & BB_VF_NOFOG) | (list == 4) << 1);   // 1 — без тумана, 2 — тень
            g->fade = world ? fd_fade(b, v) : 1.0f;
            if (world) {
                double q[3], wq[3];
                to_view(w, 1.0 / 400.0, q);
                hr_world(&kb, q, wq);
                g->hw[1] = (float)-wq[1];
                to_view(ub, 1.0 / 400.0, q);
                hr_world(u ? &ka : &kb, q, wq);
                g->hw[0] = (float)-wq[1];
            } else {
                g->hw[0] = g->hw[1] = 0;
            }
            g->uv[0] = g->uv[2] = v->u;
            g->uv[1] = g->uv[3] = v->v;
            g->ca = g->cb = v->rgba;
        }
        int abr0 = st->caps & BB_CAP_TEXTURE && st->caps & BB_CAP_BLEND && st->tex < sizeof variant_mode
                       ? variant_mode[st->tex] : -1;
        if (list == 4) {
            // плоский квад тени на выпуклой/наклонной земле уходит под неё краями: запас глубины ∝ размеру тени
            double ih = 1.0 / 400.0, q[4][3], diag = 0;
            for (unsigned k = 0; k < p->nverts; k++)
                to_view(&b->verts[p->first + k].x, ih, q[k]);
            for (unsigned k = 0; k < p->nverts; k++)
                for (unsigned m = k + 1; m < p->nverts; m++)
                    diag = fmax(diag, sqrt((q[k][0] - q[m][0]) * (q[k][0] - q[m][0]) + (q[k][1] - q[m][1]) * (q[k][1] - q[m][1]) +
                                           (q[k][2] - q[m][2]) * (q[k][2] - q[m][2])));
            for (unsigned k = 0; k < p->nverts; k++)
                gverts[p->first + k].sz[0] = gverts[p->first + k].sz[1] = (float)(0.35 * diag + 8.0);
        }
        uint32_t base = start[list] + nidx[list], n = p->nverts == 4 ? 6 : 3, f = p->first;
        uint32_t *ix = &gidx[base];
        ix[0] = f; ix[1] = f + 1; ix[2] = f + 2;
        if (n == 6) {
            ix[3] = f; ix[4] = f + 2; ix[5] = f + 3;
        }
        nidx[list] += n;
        int abr = abr0;
        if (abr0 >= 0)
            p15_abr[world][abr0]++;
        // тень — плоский квад на неровной земле: края уходили под холм, на бегу мелькали; её глубина подтянута к камере
        // в шейдере (список 4, рисуется до акторов)
        int trans = world && (is_translucent(b, p) || list == 4);   // тени и наклейки — без записи глубины
        gpu_cmd_add(list, base, n, p->state, abr, trans, !world && (hud_anchor[i] || is_wide_overlay(b, p)));
    }
    uint32_t nvg = ghost_build(a, b, b->nverts, ntotal), nig = 0;
    for (uint32_t c = 0; c < gncmd[5]; c++)
        nig += gcmd[5][c].count;
    gup_nv = b->nverts + nvg;
    gup_ni = ntotal + nig;
    gx.BindBuffer(GL_ARRAY_BUFFER, gvbo);
    gx.BufferData(GL_ARRAY_BUFFER, (ptrdiff_t)(b->nverts + nvg) * sizeof(struct gpu_vert), gverts, GL_STREAM_DRAW);
    gx.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, gibo);
    gx.BufferData(GL_ELEMENT_ARRAY_BUFFER, (ptrdiff_t)(ntotal + nig) * 4, gidx, GL_STREAM_DRAW);
}

static void gpu_pass(const struct bb_snapshot *s, int list, const int vp43[4], const int vpwide[4])
{
    int world = list != 0;
    const struct bb_state *st_now = NULL;
    int trans_now = -1, wide_now = -1, abr_now = -2, eq_now = GL_FUNC_ADD;
    if (list == 5)                  // грани A без B: состояния — из A
        s = m_a;
    for (uint32_t i = 0; s && i < gncmd[list]; i++) {
        const struct gpu_cmd *c = &gcmd[list][i];
        const struct bb_state *st = &s->states[c->state];
        int abr = bb_cfg.ps1_blend ? c->abr : -1;
        if (c->trans != trans_now) {
            gl.DepthMask(c->trans ? 0 : 1);
            if (c->trans) {
                gl.Enable(GL_POLYGON_OFFSET_FILL);
                gl.PolygonOffset(-1.0f, -4.0f);
            } else if (list == 5) {     // позади граней B на той же поверхности — видна только в дыре
                gl.Enable(GL_POLYGON_OFFSET_FILL);
                gl.PolygonOffset(2.0f, 16.0f);
            } else {
                gl.Disable(GL_POLYGON_OFFSET_FILL);
            }
            trans_now = c->trans;
        }
        if (!world && c->wide != wide_now) {
            const int *vp = c->wide ? vpwide : vp43;
            gl.Viewport(vp[0], vp[1], vp[2], vp[3]);
            wide_now = c->wide;
        }
        if (st != st_now) {
            if (st->caps & BB_CAP_TEXTURE && st->tex) {
                gl.BindTexture(GL_TEXTURE_2D, st->tex);
                gx.Uniform1i(u_tex, 1);
            } else {
                gx.Uniform1i(u_tex, 0);
            }
            gx.Uniform1i(u_afunc, st->caps & BB_CAP_ALPHA ? (int)st->afunc : -1);
            gx.Uniform1f(u_aref, st->aref);
            gx.Uniform1i(u_flat, st->shade == GL_FLAT);
            if (st->caps & BB_CAP_DEPTH) {
                gl.Enable(GL_DEPTH_TEST);
                gl.DepthFunc(st->dfunc);
            } else {
                gl.Disable(GL_DEPTH_TEST);
            }
            abr_now = -2;           // смешивание зависит и от состояния
            st_now = st;
        }
        if (abr != abr_now) {
            if (abr >= 0) {
                gl.Enable(GL_BLEND);
                gl.BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            } else if (st->caps & BB_CAP_BLEND) {
                gl.Enable(GL_BLEND);
                gl.BlendFunc(st->bsrc, st->bdst);
            } else {
                gl.Disable(GL_BLEND);
            }
            gx.Uniform1i(u_fadd, abr < 0 && st->caps & BB_CAP_BLEND && st->bdst == GL_ONE);
            int eq = abr == 2 ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD;
            if (eq != eq_now) {
                gx.BlendEquation((unsigned)eq);
                eq_now = eq;
            }
            gx.Uniform1i(u_abr, abr);
            abr_now = abr;
        }
        gx.DrawElements(GL_TRIANGLES, (int)c->count, GL_UNSIGNED_INT, (void *)(uintptr_t)(c->first * 4));
    }
    if (eq_now != GL_FUNC_ADD)
        gx.BlendEquation(GL_FUNC_ADD);
    gl.DepthMask(1);
    gl.Disable(GL_POLYGON_OFFSET_FILL);
}

// --- отладка: запись выведенных кадров ([live] cap=N кадров, cap_lvl — ждать уровень, cap_div — уменьшение) ---
// Каждый наш кадр (после resolve MSAA) уменьшается blit-ом и читается в кольцо; поток-писатель сбрасывает кольцо
// в BIN\cap.raw (RGB, строки снизу вверх), строки метаданных — в BIN\cap.txt. Кадр, которому нет места, пропускается.
#define GL_LINEAR 0x2601
#define GL_RGB 0x1907
#define GL_PACK_ALIGNMENT 0x0d05
#define CAP_RING 128
int bb_cap_req, bb_cap_lvl = -1, bb_cap_div = 2;
static unsigned cap_fbo, cap_rb;
static int cap_w, cap_h, cap_fw, cap_fh;
static uint8_t *cap_buf;
static char cap_meta[CAP_RING][720];
static volatile LONG cap_head, cap_tail, cap_done;
static uint32_t cap_left, cap_n, cap_drop, cap_pair_seen;
static HANDLE cap_thread, cap_raw, cap_txt;

static void cap_forget(void)
{
    cap_fbo = cap_rb = 0;
    cap_w = cap_h = 0;
}

static DWORD WINAPI cap_writer(LPVOID unused)
{
    DWORD wr;
    uint32_t fsz = (uint32_t)cap_fw * cap_fh * 3, n = 0;
    for (;;) {
        LONG head = cap_head, done = cap_done;
        while (cap_tail != head) {
            uint32_t slot = (uint32_t)cap_tail % CAP_RING;
            WriteFile(cap_raw, cap_buf + (size_t)slot * fsz, fsz, &wr, NULL);
            WriteFile(cap_txt, cap_meta[slot], (DWORD)lstrlenA(cap_meta[slot]), &wr, NULL);
            InterlockedIncrement(&cap_tail);
            n++;
        }
        if (done && cap_tail == cap_head)
            break;
        Sleep(5);
    }
    CloseHandle(cap_raw);
    CloseHandle(cap_txt);
    VirtualFree(cap_buf, 0, MEM_RELEASE);
    cap_buf = NULL;
    bb_log("cap: записано %lu кадров %dx%d, пропущено %lu", (unsigned long)n, cap_fw, cap_fh, (unsigned long)cap_drop);
    return 0;
}

static HANDLE cap_open(const char *name)
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(bb_self, path, MAX_PATH);
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/')
        n--;
    lstrcpyA(path + n, name);
    return CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
}

static void cap_start(int cw, int ch)
{
    if (cap_thread) {               // прошлая запись ещё сбрасывается
        if (WaitForSingleObject(cap_thread, 0) != WAIT_OBJECT_0)
            return;
        CloseHandle(cap_thread);
        cap_thread = NULL;
    }
    int div = bb_cap_div >= 1 && bb_cap_div <= 8 ? bb_cap_div : 2;
    cap_fw = (cw / div) & ~3;
    cap_fh = ch / div;
    cap_buf = VirtualAlloc(NULL, (size_t)cap_fw * cap_fh * 3 * CAP_RING, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    cap_raw = cap_open("cap.raw");
    cap_txt = cap_open("cap.txt");
    if (!cap_buf || cap_raw == INVALID_HANDLE_VALUE || cap_txt == INVALID_HANDLE_VALUE) {
        bb_log("cap: не начата (буфер %p)", (void *)cap_buf);
        if (cap_buf)
            VirtualFree(cap_buf, 0, MEM_RELEASE);
        if (cap_raw != INVALID_HANDLE_VALUE)
            CloseHandle(cap_raw);
        if (cap_txt != INVALID_HANDLE_VALUE)
            CloseHandle(cap_txt);
        cap_buf = NULL;
        bb_cap_req = 0;
        return;
    }
    char line[64];
    DWORD wr;
    int len = wsprintfA(line, "%d %d\r\n", cap_fw, cap_fh);
    WriteFile(cap_txt, line, (DWORD)len, &wr, NULL);
    cap_left = (uint32_t)bb_cap_req;
    bb_cap_req = 0;
    cap_n = cap_drop = 0;
    cap_head = cap_tail = cap_done = 0;
    cap_pair_seen = pair_gen - 1;
    cap_thread = CreateThread(NULL, 0, cap_writer, NULL, 0, NULL);
    bb_log("cap: начата, %lu кадров %dx%d", (unsigned long)cap_left, cap_fw, cap_fh);
}

// в нашем контексте после resolve: кадр из fbo_resolve
static void cap_frame(int cw, int ch, const struct bb_snapshot *b, double alpha)
{
    if (bb_cap_req > 0 && !cap_left && (bb_cap_lvl < 0 || (*(volatile uint32_t *)(uintptr_t)0x52fd00u &&
        *(volatile uint32_t *)(uintptr_t)(*(volatile uint32_t *)(uintptr_t)0x52fd00u + 0x10000) == (uint32_t)bb_cap_lvl)))
        cap_start(cw, ch);
    if (!cap_left || !cap_buf || !gl.ReadPixels)
        return;
    if (cap_head - cap_tail >= CAP_RING) {
        cap_drop++;
    } else {
        if (!cap_fbo || cap_w != cap_fw || cap_h != cap_fh) {
            gl.GenRenderbuffers(1, &cap_rb);
            gl.BindRenderbuffer(GL_RENDERBUFFER, cap_rb);
            gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, 0, GL_RGBA8, cap_fw, cap_fh);
            gl.GenFramebuffers(1, &cap_fbo);
            gl.BindFramebuffer(GL_FRAMEBUFFER, cap_fbo);
            gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, cap_rb);
            cap_w = cap_fw;
            cap_h = cap_fh;
        }
        uint32_t slot = (uint32_t)cap_head % CAP_RING;
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        gl.BindFramebuffer(GL_READ_FRAMEBUFFER, fbo_resolve);
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, cap_fbo);
        gl.BlitFramebuffer(0, 0, cw, ch, 0, 0, cap_fw, cap_fh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        gl.BindFramebuffer(GL_READ_FRAMEBUFFER, cap_fbo);
        gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
        gl.ReadPixels(0, 0, cap_fw, cap_fh, GL_RGB, GL_UNSIGNED_BYTE, cap_buf + (size_t)slot * cap_fw * cap_fh * 3);
        // кадр, время QPC (мкс), α ×1000, пара, тик B; у первого кадра новой пары — её плохие объекты
        int len = wsprintfA(cap_meta[slot], "%lu %lu %d %lu %lu", (unsigned long)cap_n,
                            (unsigned long)(t.QuadPart / 10), (int)(alpha * 1000), (unsigned long)pair_gen,
                            (unsigned long)b->build_id);
        if (cap_pair_seen != pair_gen) {
            cap_pair_seen = pair_gen;
            lstrcpynA(cap_meta[slot] + len, pair_note, (int)sizeof cap_meta[0] - len - 3);
            len = lstrlenA(cap_meta[slot]);
        }
        lstrcpyA(cap_meta[slot] + len, "\r\n");
        InterlockedIncrement(&cap_head);
    }
    cap_n++;
    if (!--cap_left)
        InterlockedExchange(&cap_done, 1);
}

// --- отладка: траектории объектов ([live] trace=N кадров) → BIN\trace.bin, разбор — tools/trace_motion.py ---
// На каждую пару: по объекту мира в кадре — представительная вершина (меньший local) с позициями A/B для шейдера,
// сырыми позициями снимков и режимом сглаживания; на каждый кадр — α и время. P23 (всегда): что теряется у краёв —
// грани A, которых нет в B (рисуются только грани B), и грани B без положения в A (стоят в позе B весь тик).
#pragma pack(push, 1)
struct tr_tick {
    uint32_t magic, gen, a_build, b_build, nobj;
    uint32_t aonly[2][2], bnew[2];      // P23 этой пары: [блок/актор][объект в B есть/нет], грани B без A
    float xs;
    double at, bt, cam_a[9], eye_a[3], cam_b[9], eye_b[3];
};
struct tr_obj { uint32_t obj; uint16_t local; uint8_t tag, mode; float pa[3], pb[3], ra[3], rb[3]; };
struct tr_frame { uint32_t magic, gen; double t0, tswap, alpha; float xs; };
struct tr_full { uint32_t magic, gen, nv, nvb, ni, ncmd; };      // + вершины tr_v, команды tr_c, индексы u32
struct tr_v { float pa[3], pb[3]; uint32_t obj; uint16_t local; uint8_t tag, mode; };
struct tr_c { uint8_t list, trans; int8_t abr; uint8_t pad; uint32_t first, count, tex; };
#pragma pack(pop)
#define TR_BYTES (120u << 20)
int bb_trace_full;                      // [live] trace_full=1: вся геометрия пары (растеризация кадра вне игры)
static int tr_full;
#define TR_BITS 13
int bb_trace_req;
static uint8_t *tr_buf;
static uint32_t tr_len, tr_left, tr_gen, tr_frames, tr_ticks;
static HANDLE tr_thread;
static struct { uint32_t obj, gen, vi; } tr_tab[1 << TR_BITS];
static uint32_t tr_tab_gen;
static uint32_t p23_aonly[2][2], p23_bnew[2], p23_pairs, st_gen;
static uint32_t tr_cur_aonly[2][2], tr_cur_bnew[2];

// P23: грани у краёв, по разу на пару (после gpu_upload: gverts — NDC пары)
static void pair_stats(const struct bb_snapshot *a, const struct bb_snapshot *b)
{
    if (!a || st_gen == pair_gen || (!bb_cfg.p23 && !tr_left))   // отладка: до 0,5 мс на кадре 0 тика
        return;
    st_gen = pair_gen;
    memset(tr_cur_aonly, 0, sizeof tr_cur_aonly);
    memset(tr_cur_bnew, 0, sizeof tr_cur_bnew);
    pk_build(b);
    float xs = (float)world_xscale;
    for (uint32_t i = 0; i < b->nprims; i++) {          // грани B «без положения в A»
        const struct bb_prim *p = &b->prims[i];
        int kd = edge_kind(b, p);
        if (kd < 0)
            continue;
        int fixed = 0;
        float x0 = 9, x1 = -9, y0 = 9, y1 = -9;
        for (unsigned m = 0; m < p->nverts; m++) {
            const struct gpu_vert *g = &gverts[p->first + m];
            fixed |= !memcmp(g->pa, g->pb, sizeof g->pa) && !pair_a_half(a, b, p->first + m);
            x0 = fminf(x0, g->pb[0] * xs); x1 = fmaxf(x1, g->pb[0] * xs);
            y0 = fminf(y0, g->pb[1]); y1 = fmaxf(y1, g->pb[1]);
        }
        if (fixed && x1 > -1 && x0 < 1 && y1 > -1 && y0 < 1)
            tr_cur_bnew[kd]++;
    }
    for (uint32_t i = 0; i < a->nprims; i++) {          // грани A, которых нет в B, видимые в кадре A
        const struct bb_prim *p = &a->prims[i];
        int kd = edge_kind(a, p);
        if (kd < 0)
            continue;
        uint64_t k = prim_key(a, p);
        if (!k || pk_has(k))
            continue;
        const double *o = a->states[p->state].ortho;
        float x0 = 9, x1 = -9, y0 = 9, y1 = -9, n[2];
        for (unsigned m = 0; m < p->nverts; m++) {
            const struct bb_vert *v = &a->verts[p->first + m];
            ndc_xy(v->x, v->y, o, n);
            x0 = fminf(x0, n[0] * xs); x1 = fmaxf(x1, n[0] * xs);
            y0 = fminf(y0, n[1]); y1 = fmaxf(y1, n[1]);
        }
        if (!(x1 > -1 && x0 < 1 && y1 > -1 && y0 < 1))
            continue;
        tr_cur_aonly[kd][ob_has(a->verts[p->first].obj) ? 0 : 1]++;
    }
    for (int k = 0; k < 2; k++) {
        p23_bnew[k] += tr_cur_bnew[k];
        p23_aonly[k][0] += tr_cur_aonly[k][0];
        p23_aonly[k][1] += tr_cur_aonly[k][1];
    }
    p23_pairs++;
}

static void p23_log(void)
{
    if (p23_pairs)
        bb_log("P23 края за %lu пар: граней A без B (видны в A) блоки %lu+%lu, акторы %lu+%lu (объект есть+нет в B) | "
               "граней B без A (видны) блоки %lu, акторы %lu", (unsigned long)p23_pairs,
               (unsigned long)p23_aonly[0][0], (unsigned long)p23_aonly[0][1], (unsigned long)p23_aonly[1][0],
               (unsigned long)p23_aonly[1][1], (unsigned long)p23_bnew[0], (unsigned long)p23_bnew[1]);
    if (p24_pairs)
        bb_log("P24 M4e за %lu пар: смен ракурса %lu, перепроецировано вершин %lu (из них починенных %lu), граней A "
               "дорисовано %lu (отказ %lu) | M4f vblank %.3f мс", (unsigned long)p24_pairs, (unsigned long)p24_cut,
               (unsigned long)p24_rep, (unsigned long)p24_syn, (unsigned long)p24_ghost, (unsigned long)p24_ghost_skip,
               bb_pacer_vb_ms());
    p24_pairs = p24_cut = p24_rep = p24_ghost = p24_ghost_skip = p24_syn = 0;
    memset(p23_aonly, 0, sizeof p23_aonly);
    memset(p23_bnew, 0, sizeof p23_bnew);
    p23_pairs = 0;
}

static DWORD WINAPI tr_writer(LPVOID unused)
{
    HANDLE f = cap_open("trace.bin");
    DWORD wr;
    if (f != INVALID_HANDLE_VALUE) {
        WriteFile(f, tr_buf, tr_len, &wr, NULL);
        CloseHandle(f);
    }
    bb_log("trace: записано %lu байт, кадров %lu, пар %lu", (unsigned long)tr_len, (unsigned long)tr_frames,
           (unsigned long)tr_ticks);
    VirtualFree(tr_buf, 0, MEM_RELEASE);
    tr_buf = NULL;
    return 0;
}

static void tr_finish(void)
{
    tr_left = 0;
    tr_thread = CreateThread(NULL, 0, tr_writer, NULL, 0, NULL);
}

static void trace_pair(const struct bb_snapshot *a, const struct bb_snapshot *b)
{
    if (bb_trace_req > 0 && !tr_left && !tr_buf) {
        if (tr_thread) {
            if (WaitForSingleObject(tr_thread, 0) != WAIT_OBJECT_0)
                return;
            CloseHandle(tr_thread);
            tr_thread = NULL;
        }
        tr_buf = VirtualAlloc(NULL, TR_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        tr_left = tr_buf ? (uint32_t)bb_trace_req : 0;
        bb_trace_req = 0;
        tr_len = tr_frames = tr_ticks = 0;
        tr_gen = pair_gen - 1;
        tr_full = bb_trace_full;
        bb_log("trace: %s, %lu кадров", tr_buf ? "начата" : "нет памяти", (unsigned long)tr_left);
    }
    if (!tr_left || !a || tr_gen == pair_gen)
        return;
    tr_gen = pair_gen;
    if (tr_len + sizeof(struct tr_tick) + (sizeof(struct tr_obj) << TR_BITS) > TR_BYTES) {
        tr_finish();
        return;
    }
    struct tr_tick *h = (struct tr_tick *)(tr_buf + tr_len);
    struct tr_obj *out = (struct tr_obj *)(h + 1);
    const uint32_t om = (1u << TR_BITS) - 1;
    uint32_t n = 0;
    tr_tab_gen++;
    for (uint32_t i = 0; i < b->nverts; i++) {          // представитель объекта — вершина с меньшим local
        const struct bb_vert *v = &b->verts[i];
        if (!v->obj || !is_world(v))
            continue;
        uint32_t s = (v->obj * 2654435761u) >> (32 - TR_BITS);
        while (tr_tab[s].gen == tr_tab_gen && tr_tab[s].obj != v->obj)
            s = (s + 1) & om;
        if (tr_tab[s].gen != tr_tab_gen) {
            tr_tab[s].gen = tr_tab_gen;
            tr_tab[s].obj = v->obj;
            tr_tab[s].vi = i;
        } else if (v->local < b->verts[tr_tab[s].vi].local) {
            tr_tab[s].vi = i;
        }
    }
    float xs = (float)world_xscale;
    for (uint32_t s = 0; s <= om && n < (1u << TR_BITS); s++) {
        if (tr_tab[s].gen != tr_tab_gen)
            continue;
        uint32_t i = tr_tab[s].vi;
        const struct bb_vert *v = &b->verts[i];
        const struct gpu_vert *g = &gverts[i];
        float x = g->pb[0] * xs, y = g->pb[1], xa = g->pa[0] * xs, ya = g->pa[1];
        if ((fabsf(x) > 1.3f || fabsf(y) > 1.3f) && (fabsf(xa) > 1.3f || fabsf(ya) > 1.3f))
            continue;
        struct tr_obj *o = &out[n++];
        int32_t j = match_idx[i];
        o->obj = v->obj;
        o->local = v->local;
        o->tag = v->tag;
        o->mode = (uint8_t)((pair_a_half(a, b, i) != NULL) | bad_has(v->obj) << 1 | (vhalf[i] == 1) << 2 |
                            (vhalf[i] == 2) << 3 | (vsyn[i] != 0) << 4 | (j >= 0 && vjump[i]) << 5 | (j < 0) << 6 |
                            (vrep[i] != 0) << 7);
        memcpy(o->pa, g->pa, sizeof o->pa);
        memcpy(o->pb, g->pb, sizeof o->pb);
        memcpy(o->rb, &v->x, sizeof o->rb);
        if (j >= 0)
            memcpy(o->ra, &a->verts[j].x, sizeof o->ra);
        else
            o->ra[0] = o->ra[1] = o->ra[2] = 0;
    }
    h->magic = 0x4b434954u;     // "TICK"
    h->gen = pair_gen;
    h->a_build = a->build_id;
    h->b_build = b->build_id;
    h->nobj = n;
    memcpy(h->aonly, tr_cur_aonly, sizeof h->aonly);
    memcpy(h->bnew, tr_cur_bnew, sizeof h->bnew);
    h->xs = xs;
    h->at = a->t;
    h->bt = b->t;
    memcpy(h->cam_a, a->cam, sizeof h->cam_a);
    memcpy(h->eye_a, a->eye, sizeof h->eye_a);
    memcpy(h->cam_b, b->cam, sizeof h->cam_b);
    memcpy(h->eye_b, b->eye, sizeof h->eye_b);
    tr_len += sizeof *h + n * sizeof *out;
    tr_ticks++;
    if (!tr_full)
        return;
    uint32_t ncmd = 0;
    for (int l = 0; l < NLIST; l++)
        ncmd += gncmd[l];
    size_t need = sizeof(struct tr_full) + (size_t)gup_nv * sizeof(struct tr_v) + ncmd * sizeof(struct tr_c) + gup_ni * 4u;
    if (tr_len + need + (sizeof(struct tr_obj) << TR_BITS) > TR_BYTES) {
        tr_finish();
        return;
    }
    struct tr_full *fh = (struct tr_full *)(tr_buf + tr_len);
    fh->magic = 0x4c4c5546u;    // "FULL"
    fh->gen = pair_gen;
    fh->nv = gup_nv;
    fh->nvb = b->nverts;
    fh->ni = gup_ni;
    fh->ncmd = ncmd;
    struct tr_v *tv = (struct tr_v *)(fh + 1);
    for (uint32_t i = 0; i < gup_nv; i++) {
        const struct bb_vert *v = i < b->nverts ? &b->verts[i] : &a->verts[ghost_src[i]];
        memcpy(tv[i].pa, gverts[i].pa, sizeof tv[i].pa);
        memcpy(tv[i].pb, gverts[i].pb, sizeof tv[i].pb);
        tv[i].obj = v->obj;
        tv[i].local = v->local;
        tv[i].tag = v->tag;
        if (i < b->nverts) {
            int32_t j = match_idx[i];
            tv[i].mode = (uint8_t)((pair_a_half(a, b, i) != NULL) | (v->obj && bad_has(v->obj)) << 1 | (vhalf[i] == 1) << 2 |
                                   (vhalf[i] == 2) << 3 | (vsyn[i] != 0) << 4 | (j >= 0 && vjump[i]) << 5 | (j < 0) << 6 |
                                   (vrep[i] != 0) << 7);
        } else {
            tv[i].mode = 0xff;
        }
    }
    struct tr_c *tc = (struct tr_c *)(tv + gup_nv);
    for (int l = 0; l < NLIST; l++)
        for (uint32_t c = 0; c < gncmd[l]; c++, tc++) {
            const struct gpu_cmd *g = &gcmd[l][c];
            tc->list = (uint8_t)l;
            tc->trans = g->trans;
            tc->abr = g->abr;
            tc->pad = 0;
            tc->first = g->first;
            tc->count = g->count;
            tc->tex = (l == 5 ? a : b)->states[g->state].tex;
        }
    memcpy(tc, gidx, gup_ni * 4u);
    tr_len += (uint32_t)need;
}

static void trace_frame(double t0, double alpha)
{
    if (!tr_left || tr_gen != pair_gen)
        return;
    if (tr_len + sizeof(struct tr_frame) > TR_BYTES) {
        tr_finish();
        return;
    }
    LARGE_INTEGER t, f;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    struct tr_frame *r = (struct tr_frame *)(tr_buf + tr_len);
    r->magic = 0x4d415246u;     // "FRAM"
    r->gen = pair_gen;
    r->t0 = t0;
    r->tswap = (double)t.QuadPart / (double)f.QuadPart;
    r->alpha = alpha;
    r->xs = (float)world_xscale;
    tr_len += sizeof *r;
    tr_frames++;
    if (!--tr_left)
        tr_finish();
}

// M5b: копия задника — текстура с постоянным именем вне диапазона движка (glGenTextures не вызываем, §1.4.1)
#define SKY_TEX 0x7e00u
#define GL_TEXTURE0 0x84c0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_RGBA 0x1908

static int sky_copy(int cw, int ch)
{
    if (!gl.TexImage2D || !gx.FramebufferTexture2D || !gx.ActiveTexture)
        return 0;
    if (!sky_fbo || sky_w != cw || sky_h != ch) {
        gl.BindTexture(GL_TEXTURE_2D, SKY_TEX);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cw, ch, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, 0x2703);   // LINEAR_MIPMAP_LINEAR: цвет дымки — размытый
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.BindTexture(GL_TEXTURE_2D, 0);
        if (!sky_fbo)
            gl.GenFramebuffers(1, &sky_fbo);
        gl.BindFramebuffer(GL_FRAMEBUFFER, sky_fbo);
        gx.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, SKY_TEX, 0);
        unsigned st = gl.CheckFramebufferStatus(GL_FRAMEBUFFER);
        bb_log("M5b: копия задника %dx%d -> %#x", cw, ch, st);
        if (st != GL_FRAMEBUFFER_COMPLETE) {
            gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
            return 0;
        }
        sky_w = cw;
        sky_h = ch;
    }
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, sky_fbo);
    gl.BlitFramebuffer(0, 0, cw, ch, 0, 0, cw, ch, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gx.ActiveTexture(GL_TEXTURE0 + 1);
    gl.BindTexture(GL_TEXTURE_2D, SKY_TEX);
    if (gx.GenerateMipmap)
        gx.GenerateMipmap(GL_TEXTURE_2D);
    gx.ActiveTexture(GL_TEXTURE0);
    return 1;
}

// M5b: дымка у земли — униформы кадра и полноэкранный фон (задник + плоскость дымки) поверх копии задника.
// Верх слоя — под ногами игрока на haze_depth, следует за ним плавно (τ ≈ 1,5 с), сбрасывается на смене уровня.
static double hz_top, hz_col[3] = { -1, 0, 0 };
static uint32_t hz_level = 0xffffffffu;
static LARGE_INTEGER hz_t;

static void haze_setup(const struct bb_snapshot *a, const struct bb_snapshot *s, double alpha, int on, const int *vp)
{
    float hz = on && bb_cfg.haze > 0 ? bb_cfg.haze / 100.0f : 0.0f;
    gx.Uniform1f(u_hz, hz);
    if (hz <= 0.0f)
        return;
    struct hr_cam ka, kb;
    hr_cam_of(s, &kb);
    if (a)
        hr_cam_of(a, &ka);
    else
        ka = kb;
    double t = alpha < 0 ? 0 : alpha > ALPHA_MAX ? ALPHA_MAX : alpha, c[9], ci[9];
    for (int i = 0; i < 9; i++)
        c[i] = ka.c[i] + (kb.c[i] - ka.c[i]) * t;
    double d = c[0] * (c[4] * c[8] - c[5] * c[7]) - c[1] * (c[3] * c[8] - c[5] * c[6]) + c[2] * (c[3] * c[7] - c[4] * c[6]);
    if (fabs(d) < 1e-9) {
        gx.Uniform1f(u_hz, 0.0f);
        return;
    }
    ci[0] = (c[4] * c[8] - c[5] * c[7]) / d; ci[1] = (c[2] * c[7] - c[1] * c[8]) / d; ci[2] = (c[1] * c[5] - c[2] * c[4]) / d;
    ci[3] = (c[5] * c[6] - c[3] * c[8]) / d; ci[4] = (c[0] * c[8] - c[2] * c[6]) / d; ci[5] = (c[2] * c[3] - c[0] * c[5]) / d;
    ci[6] = (c[3] * c[7] - c[4] * c[6]) / d; ci[7] = (c[1] * c[6] - c[0] * c[7]) / d; ci[8] = (c[0] * c[4] - c[1] * c[3]) / d;
    float m[9];
    for (int i = 0; i < 9; i++)
        m[i] = (float)ci[i];
    gx.UniformMatrix3fv(u_ci, 1, 1, m);                // строки → transpose
    gx.Uniform4f(u_vp, (float)vp[0], (float)vp[1], (float)vp[2], (float)vp[3]);
    const double *o = NULL;
    for (uint32_t i = 0; i < s->nprims && !o; i++)
        if (is_world(&s->verts[s->prims[i].first]))
            o = s->states[s->prims[i].state].ortho;
    gx.Uniform3f(u_scr, o ? (float)o[1] : 400.0f, o ? (float)o[3] : 300.0f, (float)world_xscale);
    // верх слоя: высота ног игрока (вверх = −Y) − haze_depth, плавно
    LARGE_INTEGER now, f;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&f);
    double target = -s->ply[1] - bb_cfg.haze_depth, dt = hz_t.QuadPart ? (double)(now.QuadPart - hz_t.QuadPart) / f.QuadPart : 1.0;
    hz_t = now;
    if (s->level_gen != hz_level) {
        hz_level = s->level_gen;
        hz_top = target;
    } else {
        hz_top += (target - hz_top) * fmin(1.0, dt / 1.5);
    }
    gx.Uniform1f(u_hzTop, (float)hz_top);
    // цвет дымки: среднее по строке горизонта (центр экрана) в мип-уровне 6 копии задника, сглажено τ ≈ 0,7 с
    if (gl.GetTexImage && sky_w > 64 && sky_h > 64) {
        static uint8_t px[(4096 >> 6) * (4096 >> 6) * 4];
        int lw = sky_w >> 6, lh = sky_h >> 6;
        double ry = -ci[4], rz = -ci[5];                   // строка «верх» C⁻¹ (вверх = −Y), X = 0
        double yh = fabs(ry) > 1e-6 ? rz * 400.0 / ry : 0.0, oy = o ? o[3] : 300.0;
        double py = vp[1] + (yh / oy + 1.0) * 0.5 * vp[3];
        int row = (int)(fmin(fmax(py, vp[1]), vp[1] + vp[3] - 1) / 64.0);
        if (lw * lh * 4 <= (int)sizeof px && row >= 0 && row < lh) {
            gx.ActiveTexture(GL_TEXTURE0 + 1);
            gl.GetTexImage(GL_TEXTURE_2D, 6, GL_RGBA, GL_UNSIGNED_BYTE, px);
            gx.ActiveTexture(GL_TEXTURE0);
            int x0 = vp[0] / 64, x1 = (vp[0] + vp[2]) / 64;
            double cc[3] = { 0, 0, 0 };
            int n = 0;
            for (int x = x0 < 0 ? 0 : x0; x < x1 && x < lw; x++, n++)
                for (int k = 0; k < 3; k++)
                    cc[k] += px[(row * lw + x) * 4 + k] / 255.0;
            if (n) {
                double kk = hz_col[0] < 0 ? 1.0 : fmin(1.0, dt / 0.7);
                if (hz_col[0] < 0)
                    hz_col[0] = 0;
                for (int k = 0; k < 3; k++)
                    hz_col[k] += (cc[k] / n - hz_col[k]) * kk;
            }
        }
    }
    gx.Uniform3f(u_hzCol, (float)fmax(hz_col[0], 0), (float)hz_col[1], (float)hz_col[2]);
    gx.Uniform1f(u_hzThick, (float)bb_cfg.haze_thick);
    gx.Uniform1f(u_hzNear, (float)bb_cfg.haze_near);
    gx.Uniform1f(u_hzFar, (float)(bb_cfg.haze_far > bb_cfg.haze_near ? bb_cfg.haze_far : bb_cfg.haze_near + 1));
    gx.Uniform1f(u_hzMax, (float)(bb_cfg.haze_max / 100.0));
    gx.Uniform1f(u_hzA0, (float)sin(bb_cfg.haze_a0 * 3.14159265 / 180.0));
    gx.Uniform1f(u_hzA1, (float)sin((bb_cfg.haze_a1 > bb_cfg.haze_a0 ? bb_cfg.haze_a1 : bb_cfg.haze_a0 + 1) * 3.14159265 / 180.0));
    if (gl.DrawArrays) {                                // фон: задник + дымка ниже горизонта, без глубины
        gl.Disable(GL_DEPTH_TEST);
        gl.Disable(GL_BLEND);
        gx.Uniform1i(u_bgpass, 1);
        gl.DrawArrays(GL_TRIANGLES, 0, 3);
        gx.Uniform1i(u_bgpass, 0);
    }
}

int bb_render_fog_ok(void)
{
    return bb_cfg.render && !failed && bb_cfg.shader && gpu_state >= 0;
}

int bb_render_ready(void)
{
    return bb_cfg.render && !failed;
}

double bb_hor_fcull(void)
{
    return hor_fcull;
}

LARGE_INTEGER bb_swap_ret;          // возврат нашего последнего свопа (при вертикальной синхронизации — vblank)

// P27: время этапов нашего кадра (мкс, среднее/макс за секунду): вход в контекст, загрузка пары, отрисовка (CPU),
// resolve + запись кадров, glFinish (ждём GPU), выход в контекст движка, вывод в окно, своп
#define NSTG 8
static LARGE_INTEGER stg[NSTG + 1];
static uint64_t stg_sum[NSTG], stg_max[NSTG];
static uint32_t stg_n;

static void stg_take(void)
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    for (int i = 0; i < NSTG; i++) {
        uint64_t us = (uint64_t)(stg[i + 1].QuadPart - stg[i].QuadPart) * 1000000 / (uint64_t)f.QuadPart;
        stg_sum[i] += us;
        if (us > stg_max[i])
            stg_max[i] = us;
    }
    stg_n++;
}

static uint64_t pp_sum[3], pp_max[3];
static uint32_t pp_n;

static void pair_take(const LARGE_INTEGER *q)      // при смене пары: подготовка, выгрузка в GPU, статистика
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    for (int i = 0; i < 3; i++) {
        uint64_t us = (uint64_t)(q[i + 1].QuadPart - q[i].QuadPart) * 1000000 / (uint64_t)f.QuadPart;
        pp_sum[i] += us;
        if (us > pp_max[i])
            pp_max[i] = us;
    }
    pp_n++;
}

static void p27_log(void)
{
    if (p30_ticks) {
        bb_log("P30 Багз у камня: тиков со сдвигом %lu, сдвиг макс %.0f ед.", (unsigned long)p30_ticks, p30_max);
        p30_ticks = 0;
        p30_max = 0;
    }
    if (pp_n) {
        bb_log("P27 пара (%lu): подготовка %lu/%lu, выгрузка %lu/%lu, статистика %lu/%lu мкс", (unsigned long)pp_n,
               (unsigned long)(pp_sum[0] / pp_n), (unsigned long)pp_max[0], (unsigned long)(pp_sum[1] / pp_n),
               (unsigned long)pp_max[1], (unsigned long)(pp_sum[2] / pp_n), (unsigned long)pp_max[2]);
        memset(pp_sum, 0, sizeof pp_sum);
        memset(pp_max, 0, sizeof pp_max);
        pp_n = 0;
    }
    if (!stg_n)
        return;
    static const char *const nm[NSTG] = { "вход", "пара", "отрисовка", "resolve", "glFinish", "выход", "вывод", "своп" };
    char line[400];
    int len = 0;
    for (int i = 0; i < NSTG; i++)
        len += wsprintfA(line + len, " %s %lu/%lu", nm[i], (unsigned long)(stg_sum[i] / stg_n), (unsigned long)stg_max[i]);
    bb_log("P27 этапы кадра, мкс ср/макс за %lu кадров:%s | пропусков vblank %lu", (unsigned long)stg_n, line,
           (unsigned long)bb_pacer_vb_missed());
    memset(stg_sum, 0, sizeof stg_sum);
    memset(stg_max, 0, sizeof stg_max);
    stg_n = 0;
}

static int render_common(const struct bb_snapshot *a, const struct bb_snapshot *s, double alpha, int present)
{
    HDC dc;
    HGLRC eng;
    RECT rc;

    if (!bb_cfg.render || failed || !s || !s->nprims || s->overflow)
        return 0;
    LARGE_INTEGER q0, qf;
    QueryPerformanceCounter(&q0);
    QueryPerformanceFrequency(&qf);
    double t_start = (double)q0.QuadPart / (double)qf.QuadPart;
    if (!gl.MakeCurrent && !load_core())
        return 0;
    dc = gl.GetCurrentDC();
    eng = gl.GetCurrentContext();
    if (!dc || !eng)
        return 0;
    if (ours && ours_share != eng)
        destroy_ours("сменился контекст движка");
    if (!ours && !create_ours(dc, eng)) {
        failed = 1;
        bb_log("M3: свой рендер выключен до перезапуска");
        return 0;
    }
    if (!GetClientRect(WindowFromDC(dc), &rc) || rc.right <= 0 || rc.bottom <= 0)
        return 0;
    int cw = rc.right, ch = rc.bottom;

    stg[0] = q0;
    gl.MakeCurrent(dc, ours);
    QueryPerformanceCounter(&stg[1]);
    if (!ensure_fbo(cw, ch)) {
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        gl.MakeCurrent(dc, eng);
        failed = 1;
        return 0;
    }
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.Viewport(0, 0, cw, ch);
    gl.DepthMask(1);
    gl.ClearColor(0, 0, 0, 1);
    gl.ClearDepth(1.0);
    gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl.Enable(GL_MULTISAMPLE);

    // 4:3 на всю высоту окна; мир при Hor+ — на весь экран (vFOV тот же), экран/HUD — в центре 4:3
    int vh = ch, vw = ch * 4 / 3;
    if (vw > cw) {
        vw = cw;
        vh = cw * 3 / 4;
    }
    int vp43[4] = { (cw - vw) / 2, (ch - vh) / 2, vw, vh }, vpwide[4] = { 0, (ch - vh) / 2, cw, vh };
    hud_k = (double)vw / cw;
    scan_variants(s->build_id);
    int use_gpu = bb_cfg.shader && gpu_init();
    if (use_gpu) {
        gx.UseProgram(gprog);
        gx.BindVertexArray(gvao);
        LARGE_INTEGER pq[4];
        uint32_t gen0 = pair_gen;
        QueryPerformanceCounter(&pq[0]);
        prepare_pair(a, s);
        QueryPerformanceCounter(&pq[1]);
        gpu_upload(a, s);
        QueryPerformanceCounter(&pq[2]);
        pair_stats(a, s);
        trace_pair(a, s);
        QueryPerformanceCounter(&pq[3]);
        if (pair_gen != gen0)
            pair_take(pq);
        p15_log();
        if (bb_dump_req && a) {
            bb_dump_req = 0;
            dump_pair(a, s);
        }
    } else {
        interp_positions(a, s, alpha);
    }
    QueryPerformanceCounter(&stg[2]);
    double fcull = s->fcull > 0 ? s->fcull : 1.0;
    if (fcull < 1.0) {
        double k43 = (double)vw / cw;    // доля 4:3 в ширине окна
        world_xscale = k43 / fcull;
        bb_xscale = k43;
        gl.Viewport(vpwide[0], vpwide[1], vpwide[2], vpwide[3]);
    } else {
        world_xscale = bb_xscale = 1.0;
        gl.Viewport(vp43[0], vp43[1], vp43[2], vp43[3]);
    }
    if (use_gpu) {
        gl.Disable(GL_ALPHA_TEST);      // в compat-профиле тест альфы после шейдера всё ещё действует
        gx.Uniform1f(u_alpha, (float)(alpha < 0.0 ? 0.0 : alpha > ALPHA_MAX ? ALPHA_MAX : alpha));
        gx.Uniform1f(u_xs_world, (float)world_xscale);
        gx.Uniform1f(u_xs_bb, (float)bb_xscale);
        // M5b: туман движка выключен (fog.c) — задник первым, его копия — цвет тумана, мир с туманом по глубине
        double fog_d = s->fog_d > 0 && bb_cfg.fog ? s->fog_d * s->fog_m : 0;
        gx.Uniform1i(u_fogs, fog_d > 0 ? 10 : bb_cfg.fog_sharp);
        gx.Uniform1f(u_gain, bb_cfg.ps1_gain / 10.0f);
        gx.Uniform1i(u_stp, bb_cfg.ps1_stp);
        gx.Uniform1f(u_fadek, 1.0f / FADE_TICKS);
        gx.Uniform1f(u_fogD, 0.0f);
        gx.Uniform1f(u_fogS, (float)(bb_cfg.fog_start / 100.0));
        gpu_pass(s, 2, vp43, vpwide);
        if (fog_d > 0) {
            gl.Clear(GL_DEPTH_BUFFER_BIT);   // задник не закрывает дальнее
            if (sky_copy(cw, ch))
                gx.Uniform1f(u_fogD, (float)fog_d);
            if (fcull < 1.0)
                gl.Viewport(vpwide[0], vpwide[1], vpwide[2], vpwide[3]);
            else
                gl.Viewport(vp43[0], vp43[1], vp43[2], vp43[3]);
        }
        haze_setup(a, s, alpha, fog_d > 0 && sky_fbo, fcull < 1.0 ? vpwide : vp43);
        gpu_pass(s, 3, vp43, vpwide);
        gpu_pass(s, 5, vp43, vpwide);
        gpu_pass(s, 4, vp43, vpwide);
        gpu_pass(s, 1, vp43, vpwide);
        gx.Uniform1f(u_fogD, 0.0f);
        gl.Clear(GL_DEPTH_BUFFER_BIT);
        gpu_pass(s, 0, vp43, fcull < 1.0 ? vpwide : vp43);
        gx.BindVertexArray(0);
        gx.UseProgram(0);
    } else {
        draw_pass(s, 1, vp43, vpwide);
        gl.Clear(GL_DEPTH_BUFFER_BIT);
        draw_pass(s, 0, vp43, fcull < 1.0 ? vpwide : vp43);
    }

    QueryPerformanceCounter(&stg[3]);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_resolve);
    gl.BlitFramebuffer(0, 0, cw, ch, 0, 0, cw, ch, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    cap_frame(cw, ch, s, a ? alpha : 1.0);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    unsigned err = gl.GetError();
    QueryPerformanceCounter(&stg[4]);
    // RBO resolve читает контекст движка: раньше glFinish (CPU ждал GPU ~1 мс, до 2,8 мс — кадр 0 тика не успевал к
    // vblank). Теперь метка в нашем контексте и ожидание её уже на GPU в контексте движка (sync — общий у share-группы).
    void *fence = bb_cfg.fence && gl.FenceSync && gl.WaitSync && gl.DeleteSync && gl.Flush ?
                  gl.FenceSync(0x9117 /* GL_SYNC_GPU_COMMANDS_COMPLETE */, 0) : NULL;
    if (fence)
        gl.Flush();                 // метка должна уйти в GPU до ожидания в другом контексте
    else
        gl_Finish();
    QueryPerformanceCounter(&stg[5]);
    gl.MakeCurrent(dc, eng);
    if (fence) {
        gl.WaitSync(fence, 0, 0xffffffffffffffffull /* GL_TIMEOUT_IGNORED */);
        gl.DeleteSync(fence);
    }
    QueryPerformanceCounter(&stg[6]);

    // вывод в окно в контексте движка: свой FBO-объект с общим RBO resolve
    for (int i = 0; i < 8 && gl.GetError(); i++)   // ошибки движка (его «лишний» glBegin и т. п.) — не наши
        ;
    if (eng_fbo_ctx != eng) {
        eng_fbo = eng_fbo_rb = 0;
        eng_fbo_ctx = eng;
    }
    if (!eng_fbo)
        gl.GenFramebuffers(1, &eng_fbo);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, eng_fbo);
    if (eng_fbo_rb != rb_resolve) {
        gl.FramebufferRenderbuffer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb_resolve);
        eng_fbo_rb = rb_resolve;
    }
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    gl.BlitFramebuffer(0, 0, cw, ch, 0, 0, cw, ch, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    unsigned err2 = gl.GetError();
    QueryPerformanceCounter(&stg[7]);
    if (present) {                  // M4: выводим сами (своп движка в этом тике подавлен)
        static int vsync_set;
        if (!vsync_set && gl.SwapIntervalEXT && bb_cfg.vsync >= 0) {
            gl.SwapIntervalEXT(bb_cfg.vsync);   // в контексте движка — его поверхность и показывается
            vsync_set = 1;
        }
        bb_gl_real_swap(dc);
        QueryPerformanceCounter(&bb_swap_ret);
        stg[8] = bb_swap_ret;
        stg_take();
        bb_pacer_swapped(bb_swap_ret.QuadPart);
        trace_frame(t_start, a ? alpha : 1.0);
    }

    if (++frames == 1 || err || err2 || (frames % 1000 == 0))
        bb_log("M3: кадр #%u: prims=%u verts=%u states=%u окно %dx%d 4:3=%dx%d F_cull=%.3f α=%.2f совпало %u/%u "
               "%s списков %u/%u err=%#x/%#x", frames, s->nprims, s->nverts, s->nstates, cw, ch, vw, vh, fcull, alpha,
               m_matched, m_total, use_gpu ? "шейдеры" : "немедл.", gncmd[1] + gncmd[2] + gncmd[3] + gncmd[4], gncmd[0], err, err2);
    last_logged_prims = s->nprims;
    return 1;
}

int bb_render_frame(const struct bb_snapshot *s)
{
    return render_common(NULL, s, 1.0, 0);
}

int bb_render_interp(const struct bb_snapshot *a, const struct bb_snapshot *b, double alpha)
{
    return render_common(a, b, alpha, 1);
}
