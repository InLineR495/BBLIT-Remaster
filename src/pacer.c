// M2 — T1 в режиме «только сон»: в пре-хуке пейсера 0x406ee0 спим до дедлайна (target + tpf) с запасом,
// дальше оригинал делает target += tpf, видит остаток < tpf/2 и возвращается без прокрутки QPC.
// Учёт пропусков, ресинк и «речь» остаются оригинальными; средняя скорость не меняется (дедлайн накопительный).
#include <windows.h>
#include <stdint.h>
#include <math.h>
#include "hook.h"
#include "log.h"
#include "remaster.h"
#include "pacer.h"
#include "glrec.h"
#include "render.h"

#define G_TARGET   0x7c95e8u   // int64 QPC: дедлайн прошлого тика
#define G_TPF      0x7c95c8u   // int64 QPC на тик
#define G_SYNC_ON  0x45f2a4u   // 0 при /no_sync — тогда не ждём

void (*bb_pacer_observer)(void);
LARGE_INTEGER bb_pacer_exit_ts;   // момент выхода из нашего пре-хука (начало следующей итерации цикла)
static LARGE_INTEGER qpf;
static NTSTATUS (NTAPI *nt_delay)(BOOLEAN, PLARGE_INTEGER);
static uint64_t slept_us, spun_us, waits;
static uint32_t m4_frames, m4_ticks;
static double frame_period;
static int64_t vb_period;                // M4f: период vblank по свопам (QPC); 0 — ещё не измерен
static double vb_grid;                   // M4f: точка сглаженной сетки vblank (QPC); 0 — нет
static int vb_miss;
static uint32_t vb_missed;                // M4g: пропущенных vblank (кадр показан на vblank позже) с прошлого лога

// после нашего свопа: при vsync своп возвращается на vblank — период экрана меряется по ним. Частоте из
// EnumDisplaySettings верить нельзя: под gamescope Wine отдаёт 144 Гц панели при локе 120 (2026-10-16), поэтому и
// захват не гейтится ею: при локе 60 на «144-герцовой» панели интервалы 16,7 мс не попадали в окно [p0/2, 1,6·p0]
// и сетка vblank не появлялась вовсе. Захват — нижний кластер 16 интервалов 2–40 мс (пропуски vblank кратны периоду),
// дальше окно — от измеренного периода; 120 отброшенных подряд (смена режима экрана) — захват заново.
static int64_t cap_dt[16];
static int cap_n, vb_rej;

void bb_pacer_swapped(int64_t qpc)
{
    static int64_t prev;
    int64_t dt = qpc - prev, ms = qpf.QuadPart / 1000;
    prev = qpc;
    if (!vb_period) {
        if (dt < 2 * ms || dt > 40 * ms)
            return;
        cap_dt[cap_n++] = dt;
        if (cap_n < (int)(sizeof cap_dt / sizeof cap_dt[0]))
            return;
        int64_t mn = cap_dt[0], sum = 0;
        int k = 0;
        for (int i = 1; i < cap_n; i++)
            mn = cap_dt[i] < mn ? cap_dt[i] : mn;
        for (int i = 0; i < cap_n; i++)
            if (cap_dt[i] <= mn * 23 / 20) {
                sum += cap_dt[i];
                k++;
            }
        cap_n = 0;
        if (k < 6)                       // разброс без нижнего кластера — ещё серия
            return;
        vb_period = sum / k;
        vb_grid = 0;
        vb_rej = 0;
        bb_log("M4f: период vblank %.3f мс (по свопам)", (double)vb_period * 1000.0 / (double)qpf.QuadPart);
        return;
    }
    if (dt > vb_period * 23 / 20 && dt < vb_period * 4)
        vb_missed++;                     // кадр опоздал к vblank (видно парой интервалов ~10,3 + 6,3 мс при 8,34)
    if (dt < vb_period / 2 || dt > vb_period * 8 / 5) {   // пропуск vblank, пауза
        if (++vb_rej >= 120) {           // все свопы мимо окна — сменился режим экрана
            vb_period = 0;
            vb_grid = 0;
            vb_rej = 0;
            cap_n = 0;
        }
        return;
    }
    vb_rej = 0;
    vb_period = vb_period + (dt - vb_period) / 16;
    // сетка: возврат свопа гуляет (пары 9,5/7,2 мс при ровном показе) — фаза подтягивается на 1/8 ошибки
    double P = (double)vb_period, r = (double)qpc;
    if (!vb_grid) {
        vb_grid = r;
        return;
    }
    double n = floor((r - vb_grid) / P + 0.5), e = r - (vb_grid + n * P);
    if (fabs(e) < P / 4) {
        vb_grid += n * P + e / 8;
        vb_miss = 0;
    } else if (++vb_miss >= 8) {        // сетка ушла (смена режима экрана) — заново
        vb_grid = r;
        vb_miss = 0;
    }
}

// M4: снимок опубликован в этом тике (C8 нашего рендера), значит выводим мы
static int m4_fresh(void)
{
    const struct bb_snapshot *b = bb_snap_latest();
    return bb_cfg.interp && bb_render_ready() && b && b->build_id == *(volatile uint32_t *)(uintptr_t)0x4b18fcu &&
           bb_swap_suppress_pending();
}

static void sleep_until(int64_t when)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    int64_t remain = when - now.QuadPart;
    if (remain > qpf.QuadPart / 2000) {   // > 0,5 мс — спим, остаток докручиваем
        LARGE_INTEGER rel;
        rel.QuadPart = -(int64_t)((remain - qpf.QuadPart / 2000) * 10000000 / qpf.QuadPart);
        if (nt_delay)
            nt_delay(FALSE, &rel);
    }
    do {
        YieldProcessor();
        QueryPerformanceCounter(&now);
    } while (now.QuadPart < when);
}

// Кадры с частотой экрана до дедлайна тика: α — от времени (показ отстаёт на тик), хотя бы один кадр.
// M4f: время кадра — его показ, а не начало рендера. Своп при vsync возвращается на vblank, кадр выйдет на
// следующем после прошлого свопа; первый кадр тика начинается сразу после публикации снимка (через ~2 мс после
// vblank), остальные — у самого vblank: по началу рендера шаги содержимого шли 0,21/0,24/0,25/0,30 тика вместо
// 0,25 — весь мир 30 раз в секунду ускорялся и тормозил на ±20 % (замер trace 2026-10-16). Показ после следующего
// тика (фаза тика к vblank плывёт) — экстраполяция до ALPHA_MAX, а не повтор позы.
// Время тиков — тоже по сглаженной сетке: публикация снимка гуляет на время логики тика (σ 0,5 мс, до 6 мс).
static uint32_t tk_build, tk_prev_build;
static double tk_t, tk_prev_t;

static void m4_loop(int64_t deadline, int64_t tpf)
{
    const struct bb_snapshot *b = bb_snap_latest(), *a = bb_snap_prev();
    double qf = (double)qpf.QuadPart, tick = (double)tpf / qf;
    int64_t period = (int64_t)(frame_period * qf);
    if (!a || a->level_gen != b->level_gen || b->t - a->t > 3 * tick || b->t - a->t < 0.25 * tick)
        a = NULL;                        // нет пары — показываем последний тик как есть
    double ta = a ? a->t : 0, tb = b->t;
    if (b->build_id != tk_build) {       // сетка тиков: подтягивается к публикации на 1/16, сбой (> 1/3 тика) — заново
        uint32_t gap = b->build_id - tk_build;
        double pred = tk_t + gap * tick;
        tk_prev_build = tk_build;
        tk_prev_t = tk_t;
        tk_t = tk_build && gap <= 3 && fabs(b->t - pred) < tick / 3 ? pred + (b->t - pred) / 16 : b->t;
        tk_build = b->build_id;
    }
    if (bb_cfg.alpha_vb && a) {
        tb = tk_t;
        ta = a->build_id == tk_prev_build ? tk_prev_t : tb - (b->t - a->t);
    }
    bb_swap_suppress_done();
    m4_ticks++;
    // M4g: первый vblank после дедлайна рисуется ДО логики тика (старой парой, α до ~1,25): кадр 0 после логики
    // начинался через логику X (1–6 мс) от дедлайна и не успевал к vblank, если тот был ближе ~5 мс — повтор кадра
    // (~1 раз в 4–5 с, в тяжёлых местах чаще). Теперь кадру 0 остаётся полный период. vb_guard — % периода vblank.
    double guard = bb_cfg.alpha_vb && bb_cfg.vb_guard > 0 && vb_grid > 0 && vb_period > 0 ?
                   (double)vb_period * bb_cfg.vb_guard / 100.0 : -1;
    for (int n = 0;; n++) {
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        double t = (double)t0.QuadPart / qf, disp = -1;
        if (bb_cfg.alpha_vb && vb_grid > 0 && vb_period > 0) {   // показ — следующий vblank сетки (+1 мс: успеть)
            double P = (double)vb_period, x = (double)t0.QuadPart + qf / 1000 - vb_grid;
            if (x > -P && x < 8 * P) {
                disp = vb_grid + (floor(x / P) + 1) * P;
                t = disp / qf;
            }
        }
        if (n > 0) {
            if (guard >= 0 && disp >= 0) {
                if (disp - (double)deadline > guard)
                    break;               // этот vblank — уже кадру 0 после логики
            } else if (deadline - t0.QuadPart < period / 2) {
                break;                   // следующий кадр до тика уже не успеть
            }
        }
        double alpha = a ? (t - tb) / (tb - ta) : 1.0;
        alpha = alpha < 0 ? 0 : alpha > (bb_cfg.alpha_vb ? ALPHA_MAX : 1.0) ? (bb_cfg.alpha_vb ? ALPHA_MAX : 1.0) : alpha;
        bb_render_interp(a, b, alpha);
        m4_frames++;
        int64_t next = t0.QuadPart + period;
        if (guard >= 0 && disp >= 0) {
            if (disp + (double)vb_period - (double)deadline > guard)
                break;                   // следующий vblank — кадру 0 после логики
        } else if (next > deadline - qpf.QuadPart / 2000) {
            break;
        }
        sleep_until(next);
    }
}

static void on_pacer(uint32_t *esp)
{
    (void)esp;
    if (bb_pacer_observer)
        bb_pacer_observer();
    int64_t tpf = *(volatile int64_t *)(uintptr_t)G_TPF;
    int64_t deadline = *(volatile int64_t *)(uintptr_t)G_TARGET + tpf;
    LARGE_INTEGER now, t0;
    if (m4_fresh()) {                    // M4: промежуточные кадры до дедлайна (или хотя бы один)
        QueryPerformanceCounter(&now);
        int64_t r = deadline - now.QuadPart;
        m4_loop(tpf > 0 && r > 0 && r <= 2 * tpf ? deadline : now.QuadPart, tpf);
    }
    if (!bb_cfg.pacer_sleep || !*(volatile uint32_t *)(uintptr_t)G_SYNC_ON) {
        QueryPerformanceCounter(&bb_pacer_exit_ts);
        return;
    }
    QueryPerformanceCounter(&now);
    t0 = now;
    int64_t remain = deadline - now.QuadPart;
    if (tpf <= 0 || remain <= 0 || remain > 2 * tpf) {  // опоздали или дедлайн сброшен — решает оригинал
        bb_pacer_exit_ts = now;
        return;
    }
    int64_t margin = qpf.QuadPart / 1000;               // 1 мс: точность сна под Wine
    if (remain > margin) {
        LARGE_INTEGER rel;
        rel.QuadPart = -(int64_t)((remain - margin) * 10000000 / qpf.QuadPart);   // 100 нс, относительное
        if (nt_delay)
            nt_delay(FALSE, &rel);
        else
            Sleep((DWORD)((remain - margin) * 1000 / qpf.QuadPart));
    }
    LARGE_INTEGER mid;
    QueryPerformanceCounter(&mid);
    do {                                                 // добор до дедлайна (≤ ~1 мс)
        YieldProcessor();
        QueryPerformanceCounter(&now);
    } while (now.QuadPart < deadline - qpf.QuadPart / 5000);   // −0,2 мс: остаток доспит оригинал (< tpf/2 — не ждёт)
    bb_pacer_exit_ts = now;
    slept_us += (uint64_t)(mid.QuadPart - t0.QuadPart) * 1000000 / qpf.QuadPart;
    spun_us += (uint64_t)(now.QuadPart - mid.QuadPart) * 1000000 / qpf.QuadPart;
    waits++;
}

void bb_pacer_take(uint32_t *n, uint32_t *sleep_us, uint32_t *spin_us)
{
    *n = (uint32_t)waits;
    *sleep_us = (uint32_t)(waits ? slept_us / waits : 0);
    *spin_us = (uint32_t)(waits ? spun_us / waits : 0);
    waits = slept_us = spun_us = 0;
}

uint32_t bb_pacer_vb_missed(void)
{
    uint32_t n = vb_missed;
    vb_missed = 0;
    return n;
}

double bb_pacer_vb_ms(void)
{
    return vb_period > 0 ? (double)vb_period * 1000.0 / (double)qpf.QuadPart : 0;
}

void bb_pacer_take_m4(uint32_t *frames, uint32_t *ticks)
{
    *frames = m4_frames;
    *ticks = m4_ticks;
    m4_frames = m4_ticks = 0;
}

void bb_pacer_init(void)
{
    DEVMODEA dm = { .dmSize = sizeof dm };
    int hz = bb_cfg.fps > 0 ? bb_cfg.fps : 0;
    if (hz <= 0 && EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
        hz = (int)dm.dmDisplayFrequency;
    if (hz < 30 || hz > 500)
        hz = 60;
    if (bb_cfg.fps < 0)                  // опция: наибольшая частота, кратная 30, не выше экрана (144 → 120); без VRR
        hz = hz / 30 * 30;               // неровно (кадр через два vblank) — по умолчанию частота экрана
    frame_period = 1.0 / hz;
    bb_log("M4: интерполяция %s, частота кадров %d Гц", bb_cfg.interp ? "вкл" : "выкл", hz);
    QueryPerformanceFrequency(&qpf);
    nt_delay = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtDelayExecution");
    bb_detour_pre(BB_SITE_PACER, on_pacer);
}
