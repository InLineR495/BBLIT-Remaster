// M1d — пассивные пробы камеры, матриц и идентичности (DESIGN §1.6, §1.11, §3):
//   P7  — W = C⁻¹·M для частей акторов против workm движка (coord2 = [part+0x10], workm @+0x24);
//   P10 — скачки камеры за построение: |Δeye|, |Δtarget|, поворот направления, вырожденные снимки;
//   P12 — спавн 0x448d40 / освобождение 0x447a50 акторов, стабильность списка частей игрока;
//   P9  — useJoystick, joyMap, XInput (один раз).
// Всё только читает; раз в секунду — строки P7/P10/P12 в лог.
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "hook.h"
#include "log.h"
#include "probe_cam.h"

#define RD32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define RD16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define RD8(a)  (*(volatile uint8_t *)(uintptr_t)(a))

#define G_CAM_RT     0x4efae0u   // MATRIX C_q = S·R_cam (Q12), t = 0
#define G_NEG_EYE    0x4efb20u   // int[3] −eye
#define G_GTE_M      0x4e5e60u   // MATRIX M_q перед проекцией части
#define G_EYE        0x4b38c0u   // int[3]
#define G_TARGET     0x4b38ccu   // int[3]
#define G_CAM_FLAGS  0x4b3940u
#define G_LOOK       0x4b3da4u   // word
#define G_PLAYER     0x4b324cu

static int ptr_ok(uint32_t p)
{
    return p >= 0x10000 && p < 0x7fff0000 && !(p & 3);
}

// --- P7 ---

static int inv3(const double m[9], double r[9])
{
    double d = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (fabs(d) < 1e-12)
        return 0;
    r[0] = (m[4] * m[8] - m[5] * m[7]) / d; r[1] = (m[2] * m[7] - m[1] * m[8]) / d; r[2] = (m[1] * m[5] - m[2] * m[4]) / d;
    r[3] = (m[5] * m[6] - m[3] * m[8]) / d; r[4] = (m[0] * m[8] - m[2] * m[6]) / d; r[5] = (m[2] * m[3] - m[0] * m[5]) / d;
    r[6] = (m[3] * m[7] - m[4] * m[6]) / d; r[7] = (m[1] * m[6] - m[0] * m[7]) / d; r[8] = (m[0] * m[4] - m[1] * m[3]) / d;
    return 1;
}

static struct { uint32_t n, bad_ptr, singular; double max_r, max_t, sum_r, max_shear; } p7;

void bb_p7_part(uint32_t part)
{
    double C[9], Ci[9], M[9], WR[9], Mt[3], Wt[3], eye[3];
    uint32_t c2;
    const int16_t *cq = (const int16_t *)(uintptr_t)G_CAM_RT, *mq = (const int16_t *)(uintptr_t)G_GTE_M, *wq;
    const int32_t *mt = (const int32_t *)(uintptr_t)(G_GTE_M + 20), *ne = (const int32_t *)(uintptr_t)G_NEG_EYE, *wt;

    if (!ptr_ok(part) || !ptr_ok(c2 = RD32(part + 0x10))) {
        p7.bad_ptr++;
        return;
    }
    wq = (const int16_t *)(uintptr_t)(c2 + 0x24);
    wt = (const int32_t *)(uintptr_t)(c2 + 0x24 + 20);
    for (int i = 0; i < 9; i++) {
        C[i] = cq[i] / 4096.0;
        M[i] = mq[i] / 4096.0;
    }
    for (int i = 0; i < 3; i++) {
        Mt[i] = mt[i];
        eye[i] = -(double)ne[i];
    }
    if (!inv3(C, Ci)) {
        p7.singular++;
        return;
    }
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++)
            WR[3 * i + j] = Ci[3 * i] * M[j] + Ci[3 * i + 1] * M[3 + j] + Ci[3 * i + 2] * M[6 + j];
        Wt[i] = Ci[3 * i] * Mt[0] + Ci[3 * i + 1] * Mt[1] + Ci[3 * i + 2] * Mt[2] + eye[i];
    }
    double er = 0, et = 0, shear = 0;
    for (int i = 0; i < 9; i++)
        er = fmax(er, fabs(WR[i] - wq[i] / 4096.0));
    for (int i = 0; i < 3; i++)
        et = fmax(et, fabs(Wt[i] - wt[i]));
    // недиагональность WRᵀ·WR относительно диагонали (сдвиг/неравномерный масштаб)
    for (int i = 0; i < 3; i++)
        for (int j = i + 1; j < 3; j++) {
            double d = WR[i] * WR[j] + WR[3 + i] * WR[3 + j] + WR[6 + i] * WR[6 + j];
            double n = sqrt((WR[i] * WR[i] + WR[3 + i] * WR[3 + i] + WR[6 + i] * WR[6 + i]) *
                            (WR[j] * WR[j] + WR[3 + j] * WR[3 + j] + WR[6 + j] * WR[6 + j]));
            if (n > 1e-9)
                shear = fmax(shear, fabs(d) / n);
        }
    p7.n++;
    p7.sum_r += er;
    p7.max_r = fmax(p7.max_r, er);
    p7.max_t = fmax(p7.max_t, et);
    p7.max_shear = fmax(p7.max_shear, shear);
}

// --- P10 + P12 (список частей игрока) — на каждом построении ---

static struct { uint32_t builds, degenerate, flag_changes, look_changes, part_changes, big; double max_de, max_dt, max_ang; } p10;
static int32_t prev_eye[3], prev_tgt[3];
static uint32_t prev_flags, prev_look, prev_parts_hash;
static int have_prev;

void bb_cam_on_build(void)
{
    int32_t e[3], t[3];
    double de = 0, dt = 0, ang = 0, d0[3], d1[3], n0 = 0, n1 = 0, dot = 0;
    uint32_t flags = RD32(G_CAM_FLAGS), look = RD16(G_LOOK), h = 0, p;

    for (int i = 0; i < 3; i++) {
        e[i] = (int32_t)RD32(G_EYE + 4 * i);
        t[i] = (int32_t)RD32(G_TARGET + 4 * i);
    }
    p10.builds++;
    if (!memcmp(e, t, sizeof e))
        p10.degenerate++;
    if (have_prev) {
        for (int i = 0; i < 3; i++) {
            de += (double)(e[i] - prev_eye[i]) * (e[i] - prev_eye[i]);
            dt += (double)(t[i] - prev_tgt[i]) * (t[i] - prev_tgt[i]);
            d0[i] = prev_tgt[i] - prev_eye[i];
            d1[i] = t[i] - e[i];
            n0 += d0[i] * d0[i];
            n1 += d1[i] * d1[i];
            dot += d0[i] * d1[i];
        }
        de = sqrt(de);
        dt = sqrt(dt);
        if (n0 > 0 && n1 > 0)
            ang = acos(fmax(-1.0, fmin(1.0, dot / sqrt(n0 * n1)))) * 180.0 / 3.14159265358979;
        p10.max_de = fmax(p10.max_de, de);
        p10.max_dt = fmax(p10.max_dt, dt);
        p10.max_ang = fmax(p10.max_ang, ang);
        p10.flag_changes += flags != prev_flags;
        p10.look_changes += look != prev_look;
        if ((de > 400 || ang > 25) && p10.big++ < 5)
            bb_log("P10: скачок |Δeye|=%.0f |Δtarget|=%.0f угол=%.1f° flags %#x→%#x look %u→%u",
                   de, dt, ang, prev_flags, flags, prev_look, look);
    }
    memcpy(prev_eye, e, sizeof e);
    memcpy(prev_tgt, t, sizeof t);
    prev_flags = flags;
    prev_look = look;
    have_prev = 1;

    // P12: части игрока — список actor+0x78, связь part+0
    p = RD32(G_PLAYER);
    if (ptr_ok(p)) {
        p = RD32(p + 0x78);
        for (int n = 0; ptr_ok(p) && n < 64; n++, p = RD32(p))
            h = h * 2654435761u ^ p;
    }
    p10.part_changes += h != prev_parts_hash;
    prev_parts_hash = h;
}

// --- P12: спавн / освобождение ---

static uint32_t spawns, frees, id_logged;

static void spawn_post(int k, uint32_t *esp)
{
    uint32_t a = esp[-1];   // результат 0x448d40 — слот актора
    (void)k;
    spawns++;
    if (ptr_ok(a) && id_logged++ < 40)
        bb_log("P12: spawn %08x eid=%u type=%u (ret %08x, arg %#x)", a, RD16(a + 0x1c), RD16(a + 0x2c), esp[0], esp[1] & 0xffff);
}

static void free_pre(int k, uint32_t *esp)
{
    uint32_t a = esp[1];
    (void)k;
    frees++;
    if (ptr_ok(a) && id_logged++ < 40)
        bb_log("P12: free  %08x eid=%u type=%u (ret %08x)", a, RD16(a + 0x1c), RD16(a + 0x2c), esp[0]);
}

// --- P9: пад (один раз) ---

void bb_p9_log_pad(void)
{
    char map[64];
    int len = 0;
    for (int i = 0; i < 10; i++)
        len += wsprintfA(map + len, "%02x ", RD8(0x9ca85cu + i));
    bb_log("P9: useJoystick [0x9ca858]=%u joyMap 0x9ca85c..65 = %s", RD8(0x9ca858), map);

    HMODULE x = LoadLibraryA("xinput1_4.dll");
    if (!x)
        x = LoadLibraryA("xinput1_3.dll");
    DWORD (WINAPI *get)(DWORD, void *) = x ? (void *)GetProcAddress(x, "XInputGetState") : NULL;
    if (!get) {
        bb_log("P9: XInput недоступен");
        return;
    }
    for (DWORD i = 0; i < 4; i++) {
        struct { DWORD packet; WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; } s = { 0 };
        DWORD r = get(i, &s);
        bb_log("P9: XInput[%lu] %s buttons=%#x L=(%d,%d) R=(%d,%d)", i, r == 0 ? "подключён" : "нет",
               s.buttons, s.lx, s.ly, s.rx, s.ry);
    }
}

void bb_cam_log_second(void)
{
    bb_log("P7 parts=%u max|ΔR|=%.5f mean|ΔR|=%.5f max|Δt|=%.2f shear=%.4f bad=%u sing=%u | "
           "P10 builds=%u max|Δeye|=%.0f max|Δtgt|=%.0f max угол=%.1f° degen=%u flags±=%u look±=%u | "
           "P12 spawn=%u free=%u player_parts±=%u",
           p7.n, p7.max_r, p7.n ? p7.sum_r / p7.n : 0.0, p7.max_t, p7.max_shear, p7.bad_ptr, p7.singular,
           p10.builds, p10.max_de, p10.max_dt, p10.max_ang, p10.degenerate, p10.flag_changes, p10.look_changes,
           spawns, frees, p10.part_changes);
    memset(&p7, 0, sizeof p7);
    memset(&p10, 0, sizeof p10);
    spawns = frees = 0;
}

void bb_cam_init(void)
{
    bb_around_site(8, BB_SITE_ACTOR_SPAWN, NULL, spawn_post);
    bb_around_site(9, BB_SITE_ACTOR_FREE, free_pre, NULL);
}

void bb_cam_level_reset(void)
{
    have_prev = 0;
    id_logged = 0;
}
