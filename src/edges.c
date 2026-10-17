// M5a — края Hor+ (notes/m5-h1h2-actor-cull-shadows.md, m5-projection-paths.md, m5-billboard-cull.md):
//   H1   — экранный тест акторов в ActorManager (несжатая матрица 0x52fce0): X-границы ±0x100 → ±N, N = ceil(256/F_cull);
//   H1-G — страховка: если в пуле частей мало свободных (исчерпание = jmp $), N возвращается к 0x100;
//   H2   — 41-я тень: jge на jmp $ 0x44cb77 → на штатную «тень не добавлять» 0x44c9ae (всегда, и в стоке);
//   R2′  — разрез квада движком по флагу |2 (смена диагонали, чужая альфа тумана у второй половины) выключен.
// Байты сайтов сверяются один раз при установке; дальше значения переписываются только при смене режима.
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include "hook.h"
#include "log.h"
#include "remaster.h"
#include "edges.h"

#define PART_POOL      0x4b2240u   // база пула частей
#define PART_POOL_CAP  0x4b2244u   // word: ёмкость (BZE@2 + 100)
#define PART_STRIDE    0xb8u
#define H1_RESERVE     48          // свободных частей меньше — логику не расширяем

static int sites_ok, cur_n = 0x100, cur_split_off, guard_trips;

int bb_part_pool_used(int *cap)
{
    uint32_t base = *(volatile uint32_t *)(uintptr_t)PART_POOL;
    int n = *(volatile uint16_t *)(uintptr_t)PART_POOL_CAP, used = 0;
    if (!base || n <= 0 || n > 8192) {
        *cap = 0;
        return 0;
    }
    for (int i = 0; i < n; i++)
        used += !(*(volatile uint8_t *)(uintptr_t)(base + i * PART_STRIDE + 4) & 1);
    *cap = n;
    return used;
}

static void write_n(int n)
{
    int32_t neg = -n, pos = n;
    int16_t neg16 = (int16_t)-n, pos16 = (int16_t)n;
    bb_patch(bb_site_va(BB_SITE_H1A) + 2, &neg, 4);
    bb_patch(bb_site_va(BB_SITE_H1B) + 2, &pos, 4);
    bb_patch(bb_site_va(BB_SITE_H1C) + 2, &neg16, 2);
    bb_patch(bb_site_va(BB_SITE_H1D) + 2, &pos16, 2);
}

void bb_edges_apply(int hor_active, double fcull)
{
    if (!sites_ok)
        return;
    int n = 0x100;
    if (hor_active && bb_cfg.hor_logic && fcull > 0.3 && fcull < 1.0) {
        n = (int)ceil(256.0 / fcull);
        int cap, used = bb_part_pool_used(&cap);
        if (cap && cap - used < H1_RESERVE) {
            n = 0x100;
            if (guard_trips++ < 20)
                bb_log("H1-G: свободных частей %d из %d — экранный тест акторов возвращён к ±256", cap - used, cap);
        }
    }
    if (n != cur_n) {
        write_n(n);
        bb_log("H1: экранный тест акторов ±%d (было ±%d)", n, cur_n);
        cur_n = n;
    }
    int split_off = hor_active && bb_cfg.no_quad_split;
    if (split_off != cur_split_off) {
        uint8_t imm = split_off ? 0x00 : 0x02;   // test cl,imm8 @0x414c0a: 0 → je 0x415386 всегда (без разреза)
        bb_patch(bb_site_va(BB_SITE_QUAD_SPLIT) + 2, &imm, 1);
        bb_log("R2': разрез квадов движком %s", split_off ? "выключен" : "как в оригинале");
        cur_split_off = split_off;
    }
}

void bb_edges_init(void)
{
    sites_ok = bb_site_check(BB_SITE_H1A) && bb_site_check(BB_SITE_H1B) && bb_site_check(BB_SITE_H1C) &&
               bb_site_check(BB_SITE_H1D) && bb_site_check(BB_SITE_QUAD_SPLIT);
    if (bb_cfg.fix_shadow40 && bb_site_check(BB_SITE_SHADOW40)) {
        static const uint8_t rel[4] = { 0x27, 0x00, 0x00, 0x00 };   // jge 0x44c9ae («тень не добавлять»)
        if (bb_patch(bb_site_va(BB_SITE_SHADOW40) + 2, rel, 4))
            bb_log("H2: 41-я тень больше не вешает игру (jge -> 0x44c9ae)");
    }
    bb_log("edges: сайты H1/R2' %s", sites_ok ? "сверены" : "НЕ совпали — края не трогаем");
}
