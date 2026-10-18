// M6 — ввод: нативный XInput вместо DInput-джойстика оригинала (DESIGN §1.12, notes/input.md).
// N2 (call-site 0x449223, вместо MapJoystickInput 0x403010): XInput → 8-байтный PS1-отчёт [0x4b18d8] — кнопки через
//   joyMap (виртуальные индексы 0 Y △, 1 B ○, 2 A ✕, 3 X □, 4 LB L1, 5 RB R1, 6 LT L2, 7 RT R2, 8 RS R3, 9 Start,
//   10 LS), крестовина. В игре — тип 0x41 без стика (стик кладёт N1), в меню и паузе — эмуляция 0x403010 (тип 0x73,
//   оси с мёртвой зоной DInput 20 %, направления у края). Без пада — оригинал (DInput).
// N1 (call-site 0x40660b, обёртка PadUpdate 0x4491c0): после разбора игры — аналог (радиальная мёртвая зона,
//   |v| = 51…114: игра считает bx = (|v| − 50)·2, бег при bx ≥ 0x78) и биты направлений по 8 секторам вместо
//   поосевой зоны 50/128 со снапом к осям; буфер прыжка; Back → Esc (пауза, «назад»), Start → Esc в игре.
#include <windows.h>
#include <stdint.h>
#include <math.h>
#include "hook.h"
#include "log.h"
#include "remaster.h"
#include "window.h"

#define RD8(a)  (*(volatile uint8_t *)(uintptr_t)(a))
#define RD16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define RD32(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define G_PAD_PTR    0x4b18d8u  // BYTE* — PS1-отчёт (0x4b31c0)
#define G_JOYMAP     0x9ca85cu  // config.joyMap[10]: слот → виртуальный индекс кнопки
#define G_EXTRA      0x4b18d0u  // extraInputKeys: бит 2 — Esc (cond 0x60 «назад»); MapKeyboardInput обнуляет
#define G_PSXRAM     0x52fd00u  // gd = [0x52fd00] + 0x10000
#define G_PLAYER     0x4b324cu
#define G_NOINPUT    0x4b3e48u  // байт
#define G_TRANS      0x4b39bcu  // слово
#define G_PAUSE      0x4b226cu  // байт: пауза и меню
#define FN_PADUPDATE BB_CODE(0x4491c0u)
#define FN_MAPJOY    BB_CODE(0x403010u)

typedef struct { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } xi_pad;
typedef struct { DWORD dwPacketNumber; xi_pad Gamepad; } xi_state;
static DWORD (WINAPI *xi_get)(DWORD, xi_state *);
static int xi_tried, xi_idx = -1, xi_scan;
static xi_state xs, xs_prev;
static int xs_ok, xs_prev_ok;
static uint32_t in_tick;

// P26 за секунду
static uint32_t p26_ticks, p26_analog, p26_menu, p26_buf, p26_esc, p26_maxv, p26_cam, p26_sneak;
static volatile int16_t cam_step;               // K2: шаг рыскания камеры за тик от правого стика (0 — нет)
static int p26_lx, p26_ly;

static int xi_load(void)
{
    static const char *const dll[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    if (xi_tried)
        return xi_get != NULL;
    xi_tried = 1;
    for (int k = 0; k < 3; k++) {
        HMODULE m = LoadLibraryA(dll[k]);
        if (m && (*(FARPROC *)&xi_get = GetProcAddress(m, "XInputGetState")) != NULL) {
            bb_log("M6: XInput — %s", dll[k]);
            return 1;
        }
    }
    bb_log("M6: XInput не загрузился — пад через DInput, как в оригинале");
    return 0;
}

static void xi_poll(void)
{
    xs_prev = xs;
    xs_prev_ok = xs_ok;
    xs_ok = 0;
    if (!bb_cfg.xinput || !xi_load())
        return;
    if (xi_idx >= 0 && xi_get((DWORD)xi_idx, &xs) == ERROR_SUCCESS) {
        xs_ok = 1;
        return;
    }
    if (xi_idx >= 0) {
        bb_log("M6: пад XInput %d отключён", xi_idx);
        xi_idx = -1;
    }
    if (xi_scan++ % 30)                 // поиск пада — раз в секунду
        return;
    for (int k = 0; k < 4; k++)
        if (xi_get((DWORD)k, &xs) == ERROR_SUCCESS) {
            xi_idx = k;
            xs_ok = 1;
            bb_log("M6: пад XInput %d подключён", k);
            return;
        }
}

static int vbtn(const xi_state *s, int v)       // виртуальный индекс → нажата
{
    WORD b = s->Gamepad.wButtons;
    switch (v) {
    case 0: return (b & 0x8000) != 0;               // Y △
    case 1: return (b & 0x2000) != 0;               // B ○
    case 2: return (b & 0x1000) != 0;               // A ✕
    case 3: return (b & 0x4000) != 0;               // X □
    case 4: return (b & 0x0100) != 0;               // LB L1
    case 5: return (b & 0x0200) != 0;               // RB R1
    case 6: return s->Gamepad.bLeftTrigger > 30;    // LT L2
    case 7: return s->Gamepad.bRightTrigger > 30;   // RT R2
    case 8: return (b & 0x0080) != 0;               // RS R3
    case 9: return (b & 0x0010) != 0;               // Start
    case 10: return (b & 0x0040) != 0;              // LS
    }
    return 0;
}

static int slot_v(int k)                        // слот joyMap → виртуальный индекс (pad_map 0 — тождественно)
{
    static uint8_t warned;
    uint8_t v = bb_cfg.pad_map ? RD8(G_JOYMAP + (uint32_t)k) : (uint8_t)k;
    if (v > 10) {
        if (!(warned & 1u << (k & 7)))
            bb_log("M6: joyMap[%d] = %u — неверно, взят %d", k, v, k);
        warned |= (uint8_t)(1u << (k & 7));
        return k;
    }
    return v;
}

static uint8_t *gd_ptr(void)
{
    uint32_t ram = RD32(G_PSXRAM);
    return ram ? (uint8_t *)(uintptr_t)(ram + 0x10000) : NULL;
}

// главное меню (0), экраны загрузки (1, ≥ 100), выбор языка (2): Start — как в оригинале («нажмите Enter»), не пауза.
// Игрок P там тоже есть (на титуле P ≠ 0, gd[0x4f] = 0) — по нему не отличить.
static int front_end(void)
{
    uint8_t *gd = gd_ptr();
    uint32_t lvl = gd ? *(volatile uint32_t *)gd : 0;
    return !gd || lvl <= 2 || lvl >= 100;
}

// общий предикат N1/N2: игра ведёт персонажа (геймплей, титул, катсцены), не пауза и не демо
static int drives_stick(void)
{
    uint8_t *gd = gd_ptr();
    uint32_t P = RD32(G_PLAYER);
    return bb_cfg.analog && gd && RD8(G_NOINPUT) == 0 && P && RD16(G_TRANS) == 0 &&
           !(RD32(P + 0x14) & 0x1000000) && RD8(G_PAUSE) == 0 && gd[0x4f] == 0;
}

static int di_axis(int v)                       // как DInput оригинала: −127..128, мёртвая зона 20 % по оси
{
    double f = v / 32767.0;
    f = f > 1 ? 1 : f < -1 ? -1 : f;
    double g = (fabs(f) - 0.2) / 0.8;
    if (g <= 0)
        return 0;
    return f > 0 ? (int)lround(g * 128) : -(int)lround(g * 127);
}


// N2: вместо MapJoystickInput 0x403010
static void __cdecl n2_map_pad(void)
{
    xi_poll();
    if (!xs_ok) {
        ((void (__cdecl *)(void))(uintptr_t)FN_MAPJOY)();
        return;
    }
    uint8_t *pad = *(uint8_t **)(uintptr_t)G_PAD_PTR;
    if (!pad)
        return;
    static const uint8_t bit3[8] = { 0x10, 0x20, 0x40, 0x80, 0x04, 0x08, 0x01, 0x02 };
    uint8_t lo = 0, hi = 0;
    for (int k = 0; k < 8; k++) {
        int kk = bb_cfg.cam_swap && (k == 6 || k == 7) ? 13 - k : k;   // LT/RT: поворот камеры в сторону триггера
        if (vbtn(&xs, slot_v(kk)))
            lo |= bit3[k];
    }
    if (vbtn(&xs, slot_v(8)))
        hi |= 0x04;                             // R3
    if ((!bb_cfg.pad_esc || front_end() || !bb_game_hwnd) && vbtn(&xs, slot_v(9)))   // окна нет (borderless=0) — Esc не послать
        hi |= 0x08;                             // Start (при pad_esc в игре Start — только пауза: в меню паузы он «подтверждал»)
    WORD b = xs.Gamepad.wButtons;
    hi |= (b & 0x0001 ? 0x10 : 0) | (b & 0x0008 ? 0x20 : 0) | (b & 0x0002 ? 0x40 : 0) | (b & 0x0004 ? 0x80 : 0);
    pad[0] = 0;
    if (drives_stick()) {                       // стик кладёт N1 после разбора игры
        pad[1] = 0x41;
        pad[4] = pad[5] = pad[6] = pad[7] = 0x80;
    } else {                                    // меню, пауза, демо — как DInput-пад оригинала
        int lx = di_axis(xs.Gamepad.sThumbLX), ly = di_axis(-xs.Gamepad.sThumbLY);
        int bx = lx + 0x80 > 0xff ? 0xff : lx + 0x80, by = ly + 0x80 > 0xff ? 0xff : ly + 0x80;
        hi |= (by < 0x10 ? 0x10 : 0) | (bx > 0xe0 ? 0x20 : 0) | (by > 0xe0 ? 0x40 : 0) | (bx < 0x10 ? 0x80 : 0);
        pad[1] = 0x73;
        pad[4] = pad[6] = (uint8_t)bx;
        pad[5] = pad[7] = (uint8_t)by;
        p26_menu++;
    }
    pad[2] = (uint8_t)~hi;
    pad[3] = (uint8_t)~lo;
}

static void post_esc(void)
{
    if (!bb_game_hwnd)
        return;
    PostMessageA(bb_game_hwnd, WM_KEYDOWN, VK_ESCAPE, 0x00010001);
    PostMessageA(bb_game_hwnd, WM_KEYUP, VK_ESCAPE, 0xc0010001);
    p26_esc++;
}

static int edge(int v)                          // виртуальная кнопка нажата в этом тике
{
    return xs_ok && vbtn(&xs, v) && !(xs_prev_ok && vbtn(&xs_prev, v));
}

// буфер прыжка
static int jb_left;
static uint16_t jb_prev;
static uint32_t jb_player;

static int jump_state(uint16_t s)
{
    return s == 7 || s == 9 || s == 8 || s == 26 || s == 72 || s == 78;
}

static void n1_post(void)
{
    in_tick++;
    p26_ticks++;
    cam_step = 0;
    if (!xs_ok)
        return;
    uint8_t *gd = gd_ptr();
    uint32_t P = RD32(G_PLAYER);
    if (bb_cfg.pad_esc) {
        int bk = (xs.Gamepad.wButtons & 0x0020) && !(xs_prev_ok && (xs_prev.Gamepad.wButtons & 0x0020));
        if (bk) {                               // Back = Esc: пауза / закрыть, «назад» в подменю
            post_esc();
            *(volatile uint8_t *)(uintptr_t)G_EXTRA |= 2;
        }
        if (edge(9) && P && gd && gd[0x4f] == 0 && !front_end())   // Start — открыть и закрыть паузу (Esc в WndProc 0x40e4a8)
            post_esc();
        if (RD8(G_PAUSE) && edge(1))            // B в паузе — «назад» (cond 0x60, как Esc с клавиатуры)
            *(volatile uint8_t *)(uintptr_t)G_EXTRA |= 2;
    }
    if (!drives_stick()) {
        jb_left = 0;                            // пауза, меню, загрузка: отложенный прыжок не переживает их
        return;
    }
    if (bb_cfg.cam_stick) {                     // K2: правый стик — поворот камеры (шаг за тик, кривая t^1,5)
        double rx = xs.Gamepad.sThumbRX / 32767.0, dz = bb_cfg.stick_dz / 100.0;
        double a = fabs(rx) > 1 ? 1 : fabs(rx);
        if (a > dz) {
            double t = (a - dz) / (1 - dz);
            int st = (int)lround(bb_cfg.cam_speed * t * sqrt(t));
            if (st < 1)
                st = 1;
            cam_step = (int16_t)((rx > 0) != (bb_cfg.cam_invert != 0) ? st : -st);
            p26_cam++;
        }
    }
    uint16_t *A = (uint16_t *)(gd + 0x0a), *C = (uint16_t *)(gd + 0x0c), *raw = (uint16_t *)(gd + 0x10);
    uint16_t *E = (uint16_t *)(gd + 0x3e);
    int16_t *sx = (int16_t *)(gd + 0x12), *sy = (int16_t *)(gd + 0x14);
    // аналог: только если крестовина и клавиатура молчат
    double nx = xs.Gamepad.sThumbLX / 32767.0, ny = xs.Gamepad.sThumbLY / 32767.0, r = sqrt(nx * nx + ny * ny);
    double dz = bb_cfg.stick_dz / 100.0, full = bb_cfg.stick_full / 100.0;
    p26_lx = xs.Gamepad.sThumbLX;
    p26_ly = xs.Gamepad.sThumbLY;
    if (!(*raw & 0xf000) && r > dz && full > dz) {
        double t = (r - dz) / (full - dz);
        t = t > 1 ? 1 : t;
        // подкрадывание: лёгкий наклон на земле — ○ к ходьбе (S5 → S46, ход M47, как B на ходу; замер 2026-10-16);
        // с места тик шага, потом подкрадывание. Из стойки ○ — присед 61 → подъём 87/62, в воздухе — другой приём (S30).
        // Скорость — на весь диапазон хода подкрадывания.
        double sn = bb_cfg.sneak / 100.0;
        int sneak = 0;
        if (sn > dz && r < sn) {
            uint32_t f14 = RD32(P + 0x14);
            uint16_t st0 = RD16(P + 0x17c);
            if ((f14 & 0x12) && !(f14 & 0x200) && (st0 == 5 || st0 == 46)) {   // из ходьбы; из стойки ○ — присед 61
                sneak = 1;
                t = (r - dz) / (sn - dz);
            }
        }
        nx /= r;
        ny /= r;
        int m = 51 + (int)lround(63 * t), x, y, e;
        for (;;) {                              // игра: |v| = fsqrt + fistp; нужно ≥ 51 (иначе bx ≤ 0 — мусор shr)
            x = (int)lround(m * nx);
            y = (int)lround(m * ny);
            e = (int)lround(sqrt((double)(x * x + y * y)));
            if (e >= 51 || m >= 114)
                break;
            m++;
        }
        *sx = (int16_t)x;
        *sy = (int16_t)y;
        double ang = atan2(nx, ny) * (180.0 / 3.14159265358979323846);   // 0 — вперёд, по часовой
        int sec = ((int)lround(ang / 45.0) + 8) & 7;
        static const uint16_t dir[8] = { 0x1000, 0x3000, 0x2000, 0x6000, 0x4000, 0xc000, 0x8000, 0x9000 };
        *A |= dir[sec];
        *C |= dir[sec];
        *raw |= dir[sec];
        *E |= dir[sec];
        if (sneak) {
            uint16_t cbit = *(uint16_t *)(gd + 0x30);   // ремап ○ (присед)
            *A |= cbit;
            *C |= cbit;
            *E |= cbit;
            p26_sneak++;
        }
        p26_analog++;
        if ((uint32_t)e > p26_maxv)
            p26_maxv = (uint32_t)e;
    }
    // буфер прыжка: тап ✕ за N тиков до приземления — прыжок, пока игрок не вошёл в прыжок
    int jv = slot_v(2);
    uint16_t jbit = *(uint16_t *)(gd + 0x34);
    uint16_t st = RD16(P + 0x17c);
    if (P != jb_player) {
        jb_player = P;
        jb_left = 0;
        jb_prev = st;
    }
    if (bb_cfg.jump_buf > 0 && edge(jv)) {
        jb_left = bb_cfg.jump_buf + 1;          // в тике нажатия бит ставит сама игра
    } else if (jb_left > 0) {
        if (jump_state(st) && st != jb_prev)
            jb_left = 0;                        // вошёл в прыжок — буфер потреблён
        else if (--jb_left > 0 && jbit) {
            *A |= jbit;
            *E |= jbit;
            p26_buf++;
        }
    }
    jb_prev = st;
}

// отладка: журнал игрока по тикам ([live] ptrace=N — N тиков): состояние +0x17c, флаги +0x14, позиция, кнопки,
// которые получила игра (после наших добавок аналога и буфера прыжка)
static void pl_trace(void)
{
    if (bb_cfg.ptrace <= 0)
        return;
    bb_cfg.ptrace--;
    uint8_t *gd = gd_ptr();
    uint32_t P = RD32(G_PLAYER);
    if (!P || !gd)
        return;
    bb_log("PT %lu st %u/%u f14 %08lx pos %ld %ld %ld A %04x C %04x E %04x raw %04x s %d,%d", (unsigned long)in_tick,
           RD16(P + 0x17c), RD16(P + 0x17e), (unsigned long)RD32(P + 0x14), (long)(int32_t)RD32(P + 0xe4),
           (long)(int32_t)RD32(P + 0xe8), (long)(int32_t)RD32(P + 0xec), *(uint16_t *)(gd + 0x0a),
           *(uint16_t *)(gd + 0x0c), *(uint16_t *)(gd + 0x3e), *(uint16_t *)(gd + 0x10), *(int16_t *)(gd + 0x12),
           *(int16_t *)(gd + 0x14));
}

// N1: вместо call PadUpdate 0x4491c0
static void __cdecl n1_pad_update(void)
{
    ((void (__cdecl *)(void))(uintptr_t)FN_PADUPDATE)();
    n1_post();
    pl_trace();
}

void bb_input_log_second(void)
{
    if (p26_ticks && xs_ok)
        bb_log("P26 M6: пад %d, тиков %lu, аналог %lu, меню %lu, буфер прыжка %lu, Esc %lu, |v| макс %lu, LX/LY %d/%d, "
               "камера стиком %lu, подкрадывание %lu", xi_idx, (unsigned long)p26_ticks, (unsigned long)p26_analog, (unsigned long)p26_menu,
               (unsigned long)p26_buf, (unsigned long)p26_esc, (unsigned long)p26_maxv, p26_lx, p26_ly,
               (unsigned long)p26_cam, (unsigned long)p26_sneak);
    p26_ticks = p26_analog = p26_menu = p26_buf = p26_esc = p26_maxv = p26_cam = p26_sneak = 0;
}

static int patch_call(int site, const void *fn)
{
    if (!bb_site_check(site))
        return 0;
    uint32_t va = bb_site_va(site);
    uint8_t b[5] = { 0xe8 };
    *(int32_t *)(b + 1) = (int32_t)((uintptr_t)fn - (va + 5));
    return bb_patch(va, b, 5);
}

// K2: 0x4323ce (mov ebx,[0x4b3940]; вход только je из 0x432316 — нет захвата цели) → пещера: если камера не заперта
// (bh & 0x80) и есть шаг стика — как ветка L2/R2 оригинала (0x4323e7..0x4323ff): флаги 0x82, рыскание += шаг,
// [0x4b3e10] = шаг, выход на 0x432436; иначе — оригинал с 0x4323d4 (eax — кнопки, edx = 0 не трогаем).
static int k2_init(void)
{
    if (!bb_site_check(BB_SITE_K2_CAM))
        return 0;
    uint8_t *c = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE), *p = c, *jnz, *jz;
    if (!c)
        return 0;
    uint32_t va = bb_site_va(BB_SITE_K2_CAM);
    *p++ = 0x8b; *p++ = 0x1d; *(uint32_t *)p = 0x4b3940u; p += 4;          // mov ebx,[0x4b3940]
    *p++ = 0xf6; *p++ = 0xc7; *p++ = 0x80;                                  // test bh,0x80
    *p++ = 0x75; jnz = p++;                                                 // jnz orig
    *p++ = 0x0f; *p++ = 0xbf; *p++ = 0x0d; *(uint32_t *)p = (uint32_t)(uintptr_t)&cam_step; p += 4;   // movsx ecx,word [cam_step]
    *p++ = 0x85; *p++ = 0xc9;                                               // test ecx,ecx
    *p++ = 0x74; jz = p++;                                                  // jz orig
    *p++ = 0x80; *p++ = 0x0d; *(uint32_t *)p = 0x4b3940u; p += 4; *p++ = 0x82;   // or byte [0x4b3940],0x82
    *p++ = 0x66; *p++ = 0x01; *p++ = 0x0d; *(uint32_t *)p = 0x4b3e1au; p += 4;   // add word [0x4b3e1a],cx
    *p++ = 0x89; *p++ = 0x0d; *(uint32_t *)p = 0x4b3e10u; p += 4;          // mov [0x4b3e10],ecx
    *p++ = 0x66; *p++ = 0xc7; *p++ = 0x05; *(uint32_t *)p = (uint32_t)(uintptr_t)&cam_step; p += 4;
    *p++ = 0; *p++ = 0;                                                     // mov word [cam_step],0 — один раз за тик
    *p++ = 0xe9; *(int32_t *)p = (int32_t)(BB_CODE(0x432436u) - (uint32_t)(uintptr_t)(p + 4)); p += 4;   // jmp 0x432436
    *jnz = (uint8_t)(p - (jnz + 1));
    *jz = (uint8_t)(p - (jz + 1));
    *p++ = 0xe9; *(int32_t *)p = (int32_t)(va + 6 - (uint32_t)(uintptr_t)(p + 4)); p += 4;      // orig: jmp 0x4323d4
    FlushInstructionCache(GetCurrentProcess(), c, 64);
    uint8_t patch[6] = { 0xe9, 0, 0, 0, 0, 0x90 };
    *(int32_t *)(patch + 1) = (int32_t)((uint32_t)(uintptr_t)c - (va + 5));
    return bb_patch(va, patch, 6);
}

void bb_input_init(void)
{
    if (!bb_cfg.xinput) {
        bb_log("M6: xinput=0 — ввод как в оригинале");
        return;
    }
    int n2 = patch_call(BB_SITE_N2_MAPPAD, (const void *)n2_map_pad);
    int n1 = n2 && patch_call(BB_SITE_N1_PADUPD, (const void *)n1_pad_update);
    int k2 = n1 && k2_init();
    bb_log("M6: K2 0x4323ce (камера на правом стике) %s, скорость %d, инверсия %d, LT/RT обменены %d",
           k2 ? "поставлен" : "НЕТ", bb_cfg.cam_speed, bb_cfg.cam_invert, bb_cfg.cam_swap);
    bb_log("M6: N2 0x449223 %s, N1 0x40660b %s (аналог %d, мёртвая зона %d%%, полный ход %d%%, буфер прыжка %d тиков, "
           "Back/Start → Esc %d, joyMap %s)", n2 ? "поставлен" : "НЕТ", n1 ? "поставлен" : "НЕТ", bb_cfg.analog,
           bb_cfg.stick_dz, bb_cfg.stick_full, bb_cfg.jump_buf, bb_cfg.pad_esc, bb_cfg.pad_map ? "игры" : "фиксированная");
}
