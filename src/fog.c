// M5b — туман и дальность (re/notes/m5-draw-distance.md §5).
// 0x40cc80 — единственный писатель параметров тумана: D = min(0xffff, b + K·u), K = (9·fogDist)>>1, и глобалы
// [0x4efb40] D (int), [0x4efb44]/[0x4efb68] D, [0x4efb48] −D, [0x4efb60] −0,75·D, [0x4efb64] 1/(0,25·D).
// Пост-детур (когда рисует наш рендер с шейдерами): туман движка выключен ([0x4efb60] = −3e38 — эмиттеры не ставят
// α < 1 и не уводят примитивы в смешиваемые списки), отсечки F1–F5 расширены до D_c = m·D. Вызов с b = 0xfffc
// (3D-HUD, задник со словом 0x3fff) — как в стоке. Туман рисует рендер: попиксельно по глубине вида к копии задника.
// Решение (вкл., m) защёлкивается в C1, там же сбрасывается кэш вызова [0x4b1ab0] (M5-F2): первый вызов в построении
// гарантированно пересчитывает параметры.
#include <windows.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include "hook.h"
#include "log.h"
#include "remaster.h"
#include "render.h"
#include "edges.h"

#define RD16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define RD32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define G_FOGDIST 0x9ca83cu     // config.pc +0x1c
#define G_FOG_U   0x4b38a0u     // u уровня (word, без знака)
#define G_FOG_CACHE 0x4b1ab0u
#define D_CAP 200000            // ‖t‖ матриц ≥ 524 288 переполняет int32 (0x408690)

static int fog_on, fog_m100;
static void ft_yg_build_begin(void);
static volatile uint8_t ft_extra;       // ниже: P-FT2
static volatile uint16_t yg_h;          // ниже: YG
static double fog_room_d;       // D комнат текущего построения (сток)
static uint32_t calls, calls_over;

// --- M5-AR: дистанция включения акторов (re/notes/m5-draw-distance.md §5.3) ---
// ActorManager 0x447f3a: у актора с +0x3c & 2 при dist = approx|eye − pos| > word[+0x3e] (R ≈ 6–7 тыс.) он выключается
// (не обновляется и не рисуется) — объекты «выскакивают» посреди чистой зоны, от дальности тумана это не зависит.
// Сравнение заменено вызовом bb_ar_limit: предел q·R. Пул частей (исчерпание = jmp $) защищён: новых дальних включений
// за тик ≤ AR_NEW_MAX и только при запасе AR_RESERVE (оценка AR_EST частей на актора); включённые дальние держатся,
// пока свободных ≥ AR_HARD. Таймер сна +0x24 = (dist − edx) >> 7 у движка считается от того же предела — согласован.
#define AR_NEW_MAX 3
#define AR_NEW_BURST 64         // первые AR_BURST_TICKS после загрузки уровня: мир сразу, а не по 3 за тик (~3 с)
#define AR_BURST_TICKS 45
#define AR_EST 32
#define AR_RESERVE 200          // новый дальний — только если после него останется ≥ 200 свободных
#define AR_HARD 160             // свободных < 160 — дальние сбрасываются: оригиналу всегда остаётся его запас
static int ar_q8 = 256, ar_free, ar_new, ar_drops, ar_burst;
static uint32_t ar_tick = 2;
// кто был включён в прошлом тике (бит +4 & 2 не годится: 0x447d40 ставит его всем перед проверкой дистанции)
#define AR_BITS 11
static struct ar_ent { uint32_t actor, tick, lost; } ar_tab[1 << AR_BITS];   // lost — тиков подряд «комната не рисуется»
static int ar_used;

#define AR_ROOM_LOST 15

static struct ar_ent *ar_slot(uint32_t actor)
{
    uint32_t h = (actor * 2654435761u) >> (32 - AR_BITS);
    while (ar_tab[h].actor && ar_tab[h].actor != actor)
        h = (h + 1) & ((1u << AR_BITS) - 1);
    if (!ar_tab[h].actor) {
        if (ar_used > (1 << AR_BITS) * 3 / 4) {                 // переполнение (новые адреса на смене уровня) — заново
            memset(ar_tab, 0, sizeof ar_tab);
            ar_used = 0;
            h = (actor * 2654435761u) >> (32 - AR_BITS);
        }
        ar_tab[h].actor = actor;
        ar_tab[h].tick = ar_tab[h].lost = 0;
        ar_used++;
    }
    return &ar_tab[h];
}
static uint32_t ar_far_max, ar_far_now, ar_new_sec, ar_denied_sec, ar_dropped_sec;
static int ar_free_min = 1 << 30;
static uint32_t ar_type_sec[64];          // дальние включения за секунду по типу актора (+0x2c)
static uint8_t *ar_stub;

// Комната актора рисуется в этом построении: копия A2 (0x448016..0x448074) — word[actor+0x26] против int16 id записей
// списка 0x4b3460 (шаг 0x14, счётчик int16 [0x4b39dc]), решает первое совпадение, бит 0 флагов +2 — «не рисовать».
// Без этого дальние акторы висели над комнатами, которых движок не рисует (предел глубины порталов).
static int room_drawn(uint32_t actor)
{
    int16_t room = *(volatile int16_t *)(uintptr_t)(actor + 0x26);
    if (room < 0)               // актор без комнаты (глобальный): штурвал, мост L03A — раньше всегда «не рисуется», 2026-10-18
        return 1;
    int n = *(volatile int16_t *)(uintptr_t)0x4b39dcu;
    if (n > 24)
        n = 24;
    for (int i = 0; i < n; i++)
        if (*(volatile int16_t *)(uintptr_t)(0x4b3460u + 0x14 * i) == room)
            return !(*(volatile uint8_t *)(uintptr_t)(0x4b3462u + 0x14 * i) & 1);
    return 0;
}

// отладка: [live] ar_dump=1 — решения по всем акторам дальше R за один тик в BIN\ar_dump.txt
int bb_ar_dump_req;
static HANDLE ar_dump_f;
static uint32_t ar_dump_tick;

static void ar_dump_line(uint32_t actor, int32_t dist, int32_t r, int32_t lim, int was, int lost, const char *why)
{
    if (!ar_dump_f)
        return;
    char line[200];
    DWORD wr;
    int16_t room = *(volatile int16_t *)(uintptr_t)(actor + 0x26);
    int n = *(volatile int16_t *)(uintptr_t)0x4b39dcu, at = -1;
    for (int i = 0; i < n && i < 64; i++)
        if (*(volatile int16_t *)(uintptr_t)(0x4b3460u + 0x14 * i) == room) {
            at = i;
            break;
        }
    int len = wsprintfA(line, "%08lx тип %d dist %ld R %ld предел %ld комната %d позиция %d/%d флаг %d был %d lost %d — %s\r\n",
                        (unsigned long)actor, *(volatile int16_t *)(uintptr_t)(actor + 0x2c), (long)dist, (long)r, (long)lim,
                        room, at, n, at >= 0 ? *(volatile uint8_t *)(uintptr_t)(0x4b3462u + 0x14 * at) : -1, was, lost, why);
    WriteFile(ar_dump_f, line, (DWORD)len, &wr, NULL);
}

int32_t __cdecl bb_ar_limit(int32_t dist, uint32_t actor)
{
    int32_t r = *(volatile uint16_t *)(uintptr_t)(actor + 0x3e);
    if (ar_dump_f && ar_tick != ar_dump_tick) {        // тик дампа закончился
        CloseHandle(ar_dump_f);
        ar_dump_f = NULL;
        bb_log("ar_dump: записан");
    }
    if (bb_ar_dump_req && !ar_dump_f) {
        char path[MAX_PATH];
        extern HINSTANCE bb_self;
        DWORD n = GetModuleFileNameA(bb_self, path, MAX_PATH);
        while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/')
            n--;
        lstrcpyA(path + n, "ar_dump.txt");
        ar_dump_f = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
        if (ar_dump_f == INVALID_HANDLE_VALUE)
            ar_dump_f = NULL;
        bb_ar_dump_req = 0;
        ar_dump_tick = ar_tick;
        if (ar_dump_f) {
            char line[160];
            DWORD wr;
            int cnt = *(volatile int16_t *)(uintptr_t)0x4b39dcu;
            int len = wsprintfA(line, "# тик %lu, комнат в списке %d, свободных частей %d, новых в тике %d, q8 %d\r\n",
                                (unsigned long)ar_tick, cnt, ar_free, ar_new, ar_q8);
            WriteFile(ar_dump_f, line, (DWORD)len, &wr, NULL);
        }
    }
    if (ar_q8 <= 256)
        return r;
    struct ar_ent *e = ar_slot(actor);
    uint32_t *t = &e->tick, was = *t + 1 >= ar_tick;          // включён в прошлом (или этом) тике
    if (dist <= r) {
        *t = ar_tick;
        return r;
    }
    int32_t lim = (int32_t)(((int64_t)r * ar_q8) >> 8);
    if (dist > lim) {
        ar_dump_line(actor, dist, r, lim, (int)was, (int)e->lost, "дальше предела");
        return r;
    }
    *t = ar_tick;
    if (was) {                                                 // уже включён — держим, пока комната рисуется
        if (room_drawn(actor))
            e->lost = 0;
        else if (++e->lost > AR_ROOM_LOST) {                   // комнату не рисуют ~0,5 с — над пустотой не висим
            *t = 0;
            e->lost = 0;
            ar_dropped_sec++;
            ar_dump_line(actor, dist, r, lim, 1, AR_ROOM_LOST + 1, "сброшен: комната не рисуется");
            return r;
        }
        // Мало частей: выключенный актор свои части не отдаёт (кроме флага +8 & 0x1000, см. ниже) — сброс такого пулу не
        // поможет, а только погасит объект. Раньше за тик сбрасывались все дальние разом (L03A: 67 за тик), и мир
        // мигал: гасли и по 3 за тик проявлялись снова (2026-10-18). Теперь — только отдающие части, по одному за тик.
        if (ar_free < AR_HARD) {
            if (!(RD32(actor + 8) & 0x1000) || ar_drops >= 1) {
                ar_far_now++;
                ar_dump_line(actor, dist, r, lim, 1, (int)e->lost, "держится (частей мало, сброс не поможет)");
                return lim;
            }
            *t = 0;
            ar_drops++;
            for (uint32_t p = RD32(actor + 0x78), n = 0; p && n < 64; p = RD32(p), n++)
                ar_free++;                                  // эти части вернутся в пул
            ar_dropped_sec++;
            ar_dump_line(actor, dist, r, lim, 1, (int)e->lost, "сброшен: мало частей");
            return r;
        }
        ar_far_now++;
        ar_dump_line(actor, dist, r, lim, 1, (int)e->lost, "держится");
        return lim;
    }
    // Выключенный актор свои части не отдаёт (+0x78 — список частей), кроме флага +8 & 0x1000: тот освобождает их при
    // отсеве и собирает заново при возврате. Включение актора с частями пул не тратит — довольно запаса, при котором
    // держатся включённые (раньше «оценка 32 части» отказывала динозавру L01A после возврата, 2026-10-16).
    int est = *(volatile uint32_t *)(uintptr_t)(actor + 0x78) ? 0 : AR_EST;
    int poor = est ? ar_free - est < AR_RESERVE : ar_free < AR_HARD;
    int new_max = ar_burst ? AR_NEW_BURST : AR_NEW_MAX;
    if (ar_new >= new_max || poor || !room_drawn(actor)) {
        *t = 0;
        ar_denied_sec++;
        ar_dump_line(actor, dist, r, lim, 0, (int)e->lost, ar_new >= new_max ? "отказ: лимит новых в тике" :
                     poor ? "отказ: запас частей" : "отказ: комната не рисуется");
        return r;
    }
    ar_new++;
    ar_new_sec++;
    {
        int ty = *(volatile int16_t *)(uintptr_t)(actor + 0x2c);
        if (ty >= 0 && ty < 64)
            ar_type_sec[ty]++;
    }
    ar_free -= est;
    ar_far_now++;
    ar_dump_line(actor, dist, r, lim, 0, 0, est ? "включён (части соберёт)" : "включён (части есть)");
    return lim;
}

// C1 (раз в тик, до следующего ActorManager): защёлкнуть множитель, пересчитать свободные части
static void pp_keep(void);

static void ar_build_begin(void)
{
    pp_keep();
    int cap, used = bb_part_pool_used(&cap);
    ar_q8 = bb_cfg.actor_dist > 100 && bb_cfg.actor_dist <= 400 && ar_stub ? bb_cfg.actor_dist * 256 / 100 : 256;
    ar_free = cap ? cap - used : 0;
    ar_free_min = ar_free < ar_free_min ? ar_free : ar_free_min;
    ar_new = 0;
    ar_drops = 0;
    static uint32_t seen_loads;
    if (bb_level_loads() != seen_loads) {       // загрузка уровня: дальние включаются пачкой
        seen_loads = bb_level_loads();
        ar_burst = AR_BURST_TICKS;
    } else if (ar_burst) {
        ar_burst--;
    }
    ar_tick++;
    ar_far_max = ar_far_now > ar_far_max ? ar_far_now : ar_far_max;
    ar_far_now = 0;
}

// --- M5-AR: пул частей побольше ---
// Пул частей (база [0x4b2240], ёмкость word [0x4b2244] = число из заголовка уровня + 100) выкраивается в 4 МБ
// «PSX RAM» вплотную к другим пулам и данным уровня. Дальние акторы (×4) съедают этот запас: в L03A на тик держалось
// 330+ дальних, свободных частей ~175 — новые получали отказ, объекты не появлялись (штурвал за сундуками, 2026-10-18).
// Перед инициализацией пула (0x430964, следом 0x448890 чистит его) база подменяется своим буфером, ёмкость
// увеличивается на part_extra. Следующие пулы движок уже посчитал от старой базы — раскладка уровня не меняется.
// Аллокатор (0x448980/0x4489f0) ищет свободную часть линейно по базе и ёмкости, исчерпание — jmp $.
// Уровни, загружаемые из меню (L03A), пулы заново не выкраивают: движок только переписывает ёмкость числом из
// заголовка (0x42f900), база остаётся нашей — запас возвращает pp_keep() раз в тик (недостающие слоты — свободными,
// как 0x448890: часть обнулена, [+4] = 1).
#define PP_MAX 8000
static uint8_t *pp_buf;
static int pp_cap, pp_inited;   // ёмкость, которую поставили мы, и сколько слотов буфера уже готовы

static void on_part_pool(uint32_t *esp)
{
    (void)esp;
    int cap = *(volatile int16_t *)(uintptr_t)0x4b2244u;
    if (bb_cfg.part_extra <= 0 || cap <= 0)
        return;
    if (!pp_buf && !(pp_buf = VirtualAlloc(NULL, (size_t)PP_MAX * 0xb8, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)))
        return;
    int ncap = cap + bb_cfg.part_extra > PP_MAX ? PP_MAX : cap + bb_cfg.part_extra;
    *(volatile uint32_t *)(uintptr_t)0x4b2240u = (uint32_t)(uintptr_t)pp_buf;
    *(volatile int16_t *)(uintptr_t)0x4b2244u = (int16_t)ncap;
    pp_cap = pp_inited = ncap;                      // следом 0x448890 готовит все ncap слотов
    bb_log("M5-AR: пул частей %d -> %d (свой буфер %p)", cap, ncap, (void *)pp_buf);
}

static void pp_keep(void)
{
    if (!pp_buf || bb_cfg.part_extra <= 0 || RD32(0x4b2240u) != (uint32_t)(uintptr_t)pp_buf)
        return;
    int cap = *(volatile int16_t *)(uintptr_t)0x4b2244u;
    if (cap <= 0 || cap == pp_cap)
        return;
    int ncap = cap + bb_cfg.part_extra > PP_MAX ? PP_MAX : cap + bb_cfg.part_extra;
    for (; pp_inited < ncap; pp_inited++) {
        memset(pp_buf + (size_t)pp_inited * 0xb8, 0, 0xb8);
        *(uint32_t *)(pp_buf + (size_t)pp_inited * 0xb8 + 4) = 1;
    }
    *(volatile int16_t *)(uintptr_t)0x4b2244u = (int16_t)ncap;
    pp_cap = ncap;
    bb_log("M5-AR: движок переписал ёмкость пула частей (%d) — снова %d", cap, ncap);
}

static void ar_init(void)
{
    if (bb_cfg.part_extra > 0 && bb_detour_pre(BB_SITE_PARTPOOL, on_part_pool))
        bb_log("M5-AR: пул частей уровня — свой буфер, +%d частей", bb_cfg.part_extra);
    if (!bb_site_check(BB_SITE_AR_CMP)) {
        bb_log("M5-AR: сайт 0x447f3a не совпал — дистанция акторов как в оригинале");
        return;
    }
    ar_stub = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!ar_stub)
        return;
    uint8_t *p = ar_stub;
    uint32_t back = bb_site_va(BB_SITE_AR_CMP) + 6;            // 0x447f40 jle
    *p++ = 0x51; *p++ = 0x50;                                  // push ecx; push eax (сохранить)
    *p++ = 0x51; *p++ = 0x50;                                  // push ecx (актор); push eax (dist)
    *p++ = 0xe8; *(int32_t *)p = (int32_t)((uintptr_t)bb_ar_limit - (uintptr_t)(p + 4)); p += 4;
    *p++ = 0x83; *p++ = 0xc4; *p++ = 0x08;                     // add esp, 8
    *p++ = 0x89; *p++ = 0xc2;                                  // mov edx, eax
    *p++ = 0x58; *p++ = 0x59;                                  // pop eax; pop ecx
    *p++ = 0x3b; *p++ = 0xc2;                                  // cmp eax, edx (как в оригинале)
    *p++ = 0xe9; *(int32_t *)p = (int32_t)(back - (uintptr_t)(p + 4)); p += 4;
    FlushInstructionCache(GetCurrentProcess(), ar_stub, 64);
    uint8_t patch[6] = { 0xe9, 0, 0, 0, 0, 0x90 };
    *(int32_t *)(patch + 1) = (int32_t)((uintptr_t)ar_stub - (bb_site_va(BB_SITE_AR_CMP) + 5));
    if (!bb_patch(bb_site_va(BB_SITE_AR_CMP), patch, 6)) {
        ar_stub = NULL;
        return;
    }
    bb_log("M5-AR: сравнение 0x447f3a -> %p (дистанция акторов ×%d%%)", (void *)ar_stub, bb_cfg.actor_dist);
}


static double fog_stock_d(int32_t b)
{
    int32_t f = (int32_t)RD32(G_FOGDIST);
    if (f < 0 || f > 2)
        return b;
    int32_t d = b + ((9 * f) >> 1) * (int32_t)RD16(G_FOG_U);
    return d > 0xffff ? 0xffff : d;
}

void bb_fog_build_begin(void)
{
    fog_on = bb_cfg.fog && bb_render_fog_ok();
    fog_m100 = bb_cfg.draw_dist >= 50 && bb_cfg.draw_dist <= 400 ? bb_cfg.draw_dist : 100;
    fog_room_d = fog_stock_d(4 * (int32_t)RD16(G_FOG_U));
    *(volatile uint16_t *)(uintptr_t)G_FOG_CACHE = 0x8000;    // movsx −32768 не совпадёт ни с одним словом
    ft_yg_build_begin();
    ar_build_begin();
}

// для снимка: D комнат (сток) и множитель; 0 — наш туман не рисуется (туман движка как есть)
void bb_fog_params(double *d, double *m)
{
    *d = fog_on ? fog_room_d : 0;
    *m = fog_m100 / 100.0;
}

static void fog_post(int k, uint32_t *esp)
{
    (void)k;
    calls++;
    if (!fog_on || esp[1] == 0xfffc)
        return;
    int32_t de = *(volatile int32_t *)(uintptr_t)0x4efb40u, dc = de;
    if (fog_m100 != 100) {
        double x = (double)de * fog_m100 / 100.0;
        dc = x > D_CAP ? D_CAP : (int32_t)x;
        calls_over += x > D_CAP;
    }
    float fd = (float)dc;
    *(volatile int32_t *)(uintptr_t)0x4efb40u = dc;
    *(volatile float *)(uintptr_t)0x4efb44u = fd;
    *(volatile float *)(uintptr_t)0x4efb48u = -fd;
    *(volatile float *)(uintptr_t)0x4efb68u = fd;
    *(volatile float *)(uintptr_t)0x4efb60u = -3.0e38f;
    *(volatile float *)(uintptr_t)0x4efb64u = 1.0f / (0.25f * fd);
}

static uint32_t qc_kept;                // M5c: квадов оставлено, которые оригинал отбросил бы (за секунду)

void bb_fog_log_second(void)
{
    if (qc_kept)
        bb_log("P25 M5c: квадов оставлено (одна половина к камере) %lu", (unsigned long)qc_kept);
    qc_kept = 0;
    extern uint32_t bb_sky_seen[8];
    extern int bb_sky_seen_n;
    static uint32_t last_sky[8];
    static int last_sky_n = -1;
    if (bb_sky_seen_n != last_sky_n || memcmp(last_sky, bb_sky_seen, sizeof last_sky)) {
        char line[200];
        int len = 0;
        for (int k = 0; k < bb_sky_seen_n; k++) {
            uint32_t a = bb_sky_seen[k];
            len += wsprintfA(line + len, " %08lx(тип %d, +0xc %08lx)", (unsigned long)a,
                             *(volatile int16_t *)(uintptr_t)(a + 0x2c), (unsigned long)RD32(a + 0xc));
            if (len > 150)
                break;
        }
        line[len] = 0;
        bb_log("P20 без тумана (тип 9 — задник, рисуется первым): %d акторов:%s", bb_sky_seen_n, line);
        memcpy(last_sky, bb_sky_seen, sizeof last_sky);
        last_sky_n = bb_sky_seen_n;
    }
    memset(bb_sky_seen, 0, sizeof last_sky);
    bb_sky_seen_n = 0;
    static int last_on = -1, last_m = -1;
    if (fog_on != last_on || fog_m100 != last_m) {
        bb_log("M5b: туман %s, дальность ×%d.%02d, D комнат %d (вызовов 0x40cc80 за с: %lu, в кап: %lu)",
               fog_on ? "наш" : "движка", fog_m100 / 100, fog_m100 % 100, (int)fog_room_d, (unsigned long)calls,
               (unsigned long)calls_over);
        last_on = fog_on;
        last_m = fog_m100;
    }
    calls = calls_over = 0;
    if (ar_q8 > 256 || ar_new_sec || ar_denied_sec || ar_dropped_sec)
        bb_log("P19 акторы ×%d%%: дальних включено за с %lu, отказано %lu, сброшено %lu, дальних на тик макс %lu, "
               "свободных частей мин %d | комнат в списке %d, глубина +%d, отсечение по высоте %u/%u",
               ar_q8 * 100 / 256, (unsigned long)ar_new_sec, (unsigned long)ar_denied_sec, (unsigned long)ar_dropped_sec,
               (unsigned long)ar_far_max, ar_free_min, *(volatile int16_t *)(uintptr_t)0x4b39dcu, ft_extra,
               (unsigned)yg_h, (unsigned)*(volatile uint16_t *)(uintptr_t)0x4efb4eu);
    {
        char tl[300];
        int tn = 0;
        for (int ty = 0; ty < 64; ty++)
            if (ar_type_sec[ty] && tn < (int)sizeof tl - 16)
                tn += wsprintfA(tl + tn, " %d:%lu", ty, (unsigned long)ar_type_sec[ty]);
        tl[tn] = 0;
        if (tn)
            bb_log("P22 дальние включения по типам (тип:число):%s", tl);
        memset(ar_type_sec, 0, sizeof ar_type_sec);
    }
    ar_new_sec = ar_denied_sec = ar_dropped_sec = ar_far_max = 0;
    ar_free_min = 1 << 30;
}

// --- M5b P-FT2: глубина обхода порталов (re/notes: workflow 2026-10-16, проверено скептиком) ---
// 0x410f6c: mov cl,[gd+4] (уровень+1); mov dl,[gd+5] (room+0xb5 родителя, у корня 1); cmp cl,dl; jg → флаг 1 («не
// рисовать, не раскрывать, акторы &1 спят»). У всех комнат L01A b5 = 1 — рисуются корень и соседи, дальний рельеф
// пропадал. Заглушка: глубже предела, но не глубже b5 + ft_extra — флаг 2 (0x44c270 смотрит только бит 0: рисуется
// и раскрывается); A2 с маской 3 (0x448077) считает флаг 2 «помеченной» комнатой — акторы &1 спят, как в стоке.
// ft_extra = 0 — бит-в-бит сток (так в A/B и при отказе рендера).
static volatile uint8_t ft_extra;
static int ft_cfg_eff = -1, ft_calm;
static uint8_t *ft_stub;

static void ft_init(void)
{
    if (!bb_site_check(BB_SITE_FT_DEPTH) || !bb_site_check(BB_SITE_A2_MASK)) {
        bb_log("M5b P-FT2: сайты не совпали — глубина комнат как в оригинале");
        return;
    }
    ft_stub = VirtualAlloc(NULL, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!ft_stub)
        return;
    uint8_t *p = ft_stub;
#define B(...) do { static const uint8_t b_[] = { __VA_ARGS__ }; memcpy(p, b_, sizeof b_); p += sizeof b_; } while (0)
#define JMP(to) do { *p++ = 0xe9; *(int32_t *)p = (int32_t)((uintptr_t)(to) - (uintptr_t)(p + 4)); p += 4; } while (0)
    B(0x8a, 0x55, 0x01);                        // +00 mov dl,[ebp+1]
    B(0x3a, 0xca);                              // +03 cmp cl,dl
    B(0x7e, 0x12);                              // +05 jle +0x19 (сток)
    B(0x02, 0x15); *(uint32_t *)p = (uint32_t)(uintptr_t)&ft_extra; p += 4;   // +07 add dl,[ft_extra]
    B(0x3a, 0xca);                              // +0d cmp cl,dl
    B(0x7e, 0x0d);                              // +0f jle +0x1e (флаг 2)
    B(0x8d, 0x0c, 0x80);                        // +11 lea ecx,[eax+eax*4]
    JMP(BB_CODE(0x410fb4u));                    // +14 → флаг 1
    JMP(BB_CODE(0x410f74u));                    // +19 → сток (lea; jg по тем же флагам)
    B(0x8d, 0x0c, 0x80);                        // +1e lea ecx,[eax+eax*4]
    B(0xc1, 0xe1, 0x02);                        // +21 shl ecx,2
    B(0x33, 0xd2);                              // +24 xor edx,edx
    B(0x66, 0x8b, 0x15, 0xde, 0x39, 0x4b, 0x00);                // +26 mov dx,[0x4b39de]
    B(0x66, 0xc7, 0x81, 0x62, 0x34, 0x4b, 0x00, 0x02, 0x00);    // +2d mov word [ecx+0x4b3462],2
    B(0x3b, 0xd3);                              // +36 cmp edx,ebx
    *p++ = 0x0f; *p++ = 0x8e; *(int32_t *)p = (int32_t)(BB_CODE(0x410fc0u) - (uintptr_t)(p + 4)); p += 4;   // +38 jle 0x410fc0
    JMP(BB_CODE(0x410f90u));                    // +3e → скрытый список
#undef B
#undef JMP
    if (p - ft_stub != 0x43) {
        bb_log("M5b P-FT2: длина заглушки %d != 0x43 — не ставлю", (int)(p - ft_stub));
        ft_stub = NULL;
        return;
    }
    FlushInstructionCache(GetCurrentProcess(), ft_stub, 128);
    static const uint8_t mask3 = 0x03;
    uint8_t patch[5] = { 0xe9 };
    *(int32_t *)(patch + 1) = (int32_t)((uintptr_t)ft_stub - (bb_site_va(BB_SITE_FT_DEPTH) + 5));
    if (!bb_patch(bb_site_va(BB_SITE_A2_MASK) + 3, &mask3, 1) || !bb_patch(bb_site_va(BB_SITE_FT_DEPTH), patch, 5)) {
        ft_stub = NULL;
        return;
    }
    bb_log("M5b P-FT2: глубина порталов -> заглушка %p, A2 маска 3", (void *)ft_stub);
}

// --- M5b: запас теста порталов ---
// Портал (оп. 0x10) выкидывается, если все 4 угла правее [0x621614] = w/2, левее [0x621610] = −w/2 (0x410b62/0x410bbe)
// или выше [0x62161c] = h/2 (0x410c15). Геометрия дальней комнаты выступает за свой проём далеко — комната целиком
// пропадала, когда проём уходил за край кадра движка (L01A: ~25 блоков слева, дампы 2026-10-16). Пока рисует наш
// рендер, тест портала идёт по границам ×(1 + portal_guard); иначе — стоковые.
static volatile int32_t pt_xmax, pt_xmin;
static int pt_ok;

// --- M5b YG: вертикальный запас отсечения движка ---
// Пополигонные тесты и группы режут «всё выше края кадра» по h = word[0x4efb4e]; рендер показывает промежуточные
// кадры между тиками — край в них другой, и на снижении камеры (прыжок) сверху видны вырезанные полосы неба.
// Три imm32 (комнаты 0x410072/0x4100bc, части 0x411c80) смотрят в yg_h = h·(1 + v_guard) пока рисует наш рендер,
// портал (0x410c17) — в стоковый h/2 (список комнат и логика акторов не меняются). Иначе yg_h = h — сток.
static volatile uint16_t yg_h;
static volatile int32_t yg_portal;
static int yg_ok;

static void yg_init(void)
{
    if (!bb_site_check(BB_SITE_YG1) || !bb_site_check(BB_SITE_YG2) || !bb_site_check(BB_SITE_YG3) ||
        !bb_site_check(BB_SITE_YG_PORTAL)) {
        bb_log("M5b YG: сайты не совпали — вертикальный запас не ставлю");
        return;
    }
    uint16_t h = *(volatile uint16_t *)(uintptr_t)0x4efb4eu;
    yg_h = h;
    yg_portal = h >> 1;
    uint32_t ah = (uint32_t)(uintptr_t)&yg_h, ap = (uint32_t)(uintptr_t)&yg_portal;
    yg_ok = bb_patch(bb_site_va(BB_SITE_YG1) + 2, &ah, 4) && bb_patch(bb_site_va(BB_SITE_YG2) + 2, &ah, 4) &&
            bb_patch(bb_site_va(BB_SITE_YG3) + 3, &ah, 4) && bb_patch(bb_site_va(BB_SITE_YG_PORTAL) + 2, &ap, 4);
    bb_log("M5b YG: высота отсечения -> %p, портал -> %p (%s)", (void *)&yg_h, (void *)&yg_portal, yg_ok ? "ок" : "ОШИБКА");
    if (bb_site_check(BB_SITE_PT_XMAX) && bb_site_check(BB_SITE_PT_XMIN)) {
        uint16_t w = *(volatile uint16_t *)(uintptr_t)0x4efb4cu;
        pt_xmax = w >> 1;
        pt_xmin = -(int32_t)(w >> 1);
        uint32_t ax = (uint32_t)(uintptr_t)&pt_xmax, an = (uint32_t)(uintptr_t)&pt_xmin;
        pt_ok = bb_patch(bb_site_va(BB_SITE_PT_XMAX) + 2, &ax, 4) && bb_patch(bb_site_va(BB_SITE_PT_XMIN) + 2, &an, 4);
        bb_log("M5b: границы теста порталов по X -> %p/%p (%s)", (void *)&pt_xmax, (void *)&pt_xmin, pt_ok ? "ок" : "ОШИБКА");
    }
}

// C1: запас по вертикали и глубина комнат на это построение (+ страховка ёмкостей движка)
static void ft_yg_build_begin(void)
{
    extern uint32_t bb_last_pool, bb_last_arena, bb_last_nsort;
    int active = bb_render_ready();
    uint16_t h = *(volatile uint16_t *)(uintptr_t)0x4efb4eu, w = *(volatile uint16_t *)(uintptr_t)0x4efb4cu;
    int32_t pg = active ? 100 + bb_cfg.portal_guard : 100;
    yg_portal = (int32_t)(h >> 1) * pg / 100;
    pt_xmax = (int32_t)(w >> 1) * pg / 100;
    pt_xmin = -pt_xmax;
    if (yg_ok) {
        uint32_t g = active && bb_cfg.interp ? (uint32_t)h * (100u + (uint32_t)bb_cfg.v_guard) / 100u : h;
        yg_h = (uint16_t)(g > 0xffff ? 0xffff : g);
    }
    // страховка G4: пул 35 000, арена 1 000 000, сортировка 2000 — выше 70 % глубина комнат уменьшается на 1,
    // ниже 50 % 60 построений подряд — растёт обратно до ключа
    if (ft_cfg_eff < 0 || ft_cfg_eff > bb_cfg.room_depth)
        ft_cfg_eff = bb_cfg.room_depth;
    int hot = bb_last_pool > 24500 || bb_last_arena > 700000 || bb_last_nsort > 1400;
    int calm = bb_last_pool < 17500 && bb_last_arena < 500000 && bb_last_nsort < 1000;
    if (hot && ft_cfg_eff > 0) {
        ft_cfg_eff--;
        ft_calm = 0;
        bb_log("M5b G4: ёмкость (пул %lu, арена %lu, сортировка %lu) — глубина комнат +%d", (unsigned long)bb_last_pool,
               (unsigned long)bb_last_arena, (unsigned long)bb_last_nsort, ft_cfg_eff);
    } else if (calm && ft_cfg_eff < bb_cfg.room_depth && ++ft_calm >= 60) {
        ft_cfg_eff++;
        ft_calm = 0;
    }
    ft_extra = (uint8_t)(ft_stub && active ? ft_cfg_eff : 0);
}

// --- M5c: отбраковка обратных граней комнат (0x40fdc0) ---
// Движок проверяет знак площади по (v0, v1, v2) дескриптора — у четырёхугольника (вершины по-PS1 «зигзагом», вторая
// половина (v1, v3, v2)) только по одной половине. Неплоский квад, у которого видимая половина смотрит на камеру, а
// тестовая чуть отвёрнута, пропадал целиком: на изгибах скал при скользящем взгляде — щель с небом на тик-два
// (trace_full 2026-10-16: скала 034e892c, квад 4-5-1-2 — половина (4,5,1) −5251 пкс², тестовая (4,1,2) +44).
// Теперь квад отбрасывается, только если отвёрнуты обе половины; треугольники и двусторонние списки (бит 2 первой
// грани) — как в оригинале. Дескриптор: [v0, v1, v2, v3, число вершин, 0], индексы — int16 в записи грани.
#define G_FACE_LIST 0x621608u
#define G_VPOOL     0x623688u

static float qc_area(uint32_t face, const int *d, int i0, int i1, int i2, uintptr_t pool)
{
    const float *p0 = (const float *)(pool + ((uint32_t)(int32_t)*(volatile int16_t *)(uintptr_t)(face + d[i0] * 2) << 4));
    const float *p1 = (const float *)(pool + ((uint32_t)(int32_t)*(volatile int16_t *)(uintptr_t)(face + d[i1] * 2) << 4));
    const float *p2 = (const float *)(pool + ((uint32_t)(int32_t)*(volatile int16_t *)(uintptr_t)(face + d[i2] * 2) << 4));
    return (p2[1] - p1[1]) * (p0[0] - p1[0]) - (p2[0] - p1[0]) * (p0[1] - p1[1]);
}

static void __cdecl qc_backface(const int *d)
{
    uint32_t *node = *(uint32_t **)(uintptr_t)(*(volatile uint32_t *)(uintptr_t)G_FACE_LIST + 4);
    uint32_t face = node[0];
    if (!face || (*(volatile uint8_t *)(uintptr_t)(face + 2) & 2) == 2)
        return;
    uintptr_t pool = *(volatile uint32_t *)(uintptr_t)G_VPOOL;
    int quad = d[4] == 4 && bb_cfg.quad_cull;
    while ((face = node[0]) != 0) {
        if (qc_area(face, d, 0, 1, 2, pool) < 0.0f) {
            if (quad && qc_area(face, d, 1, 3, 2, pool) >= 0.0f) {
                qc_kept++;
            } else {
                ((uint32_t *)(uintptr_t)node[2])[1] = node[1];     // prev->next = next
                ((uint32_t *)(uintptr_t)node[1])[2] = node[2];     // next->prev = prev
            }
        }
        node = (uint32_t *)(uintptr_t)node[1];
    }
}

static void qc_init(void)
{
    if (!bb_site_check(BB_SITE_BACKFACE)) {
        bb_log("M5c: сайт 0x40fdc0 не совпал — отбраковка квадов как в оригинале");
        return;
    }
    if (bb_patch_jmp(bb_site_va(BB_SITE_BACKFACE), (const void *)qc_backface))
        bb_log("M5c: отбраковка граней 0x40fdc0 заменена (квад — по обеим половинам, quad_cull=%d)", bb_cfg.quad_cull);
}

void bb_fog_init(void)
{
    qc_init();
    ft_init();
    yg_init();
    ar_init();
    if (bb_around_site(12, BB_SITE_FOGPARAMS, NULL, fog_post))
        bb_log("M5b: детур 0x40cc80 (параметры тумана) поставлен");
    else
        bb_log("M5b: детур 0x40cc80 НЕ поставлен — туман движка остаётся");
}
