// P6 — разметка пула вершин писателями (слоты C2, C4, C6, C7) и покрытие в рекордере:
// каждая вершина пула, которую движок отдаёт в glVertex3fv, должна нести метку источника.
// Диапазон писателя = [0x623654] до и после вызова. Сброс меток — в начале построения (C1 0x40ceb0).
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "hook.h"
#include "log.h"
#include "mark.h"
#include "glrec.h"
#include "probe_cam.h"
#include "render.h"

#define RD32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define G_POOL_IDX 0x623654u

uint8_t bb_pool_tag[BB_POOL_CAP + 16];
// M4: устойчивый ключ вершины пула = (объект, номер): блок комнаты — src*, часть актора — part*,
// тень — актор [[0x4efac0]+0x30], билборд — актор (порядковый номер билборда у актора в старших битах номера),
// 3D-HUD — part*|3. У экранных писателей (текст, оверлеи, полосы) объект 0 — без интерполяции.
uint32_t bb_pool_obj[BB_POOL_CAP + 16];
uint16_t bb_pool_local[BB_POOL_CAP + 16];
uint8_t bb_pool_fl[BB_POOL_CAP + 16];

// M5b: без тумана — ровно условие 0x44cb90 для базы тумана 0x3fff: +0xc & 0x2000000 и (тип 9 или +0xc & 0x20000000);
// без 0x2000000 флаг 0x20000000 бывает и у обычных объектов (тень наковальни L01A пропадала под землёй — 2026-10-16).
uint32_t bb_sky_seen[8];
int bb_sky_seen_n;

// отладка M5-AR: часть → актор и его тип (+0x2c) — дамп пары печатает тип у ключей ACTOR
#define PT_BITS 12
static struct { uint32_t part, actor; int16_t type; } pt_tab[1 << PT_BITS];

static void pt_note(uint32_t part, uint32_t actor)
{
    uint32_t h = (part * 2654435761u) >> (32 - PT_BITS);
    for (int n = 0; n < 64 && pt_tab[h].part && pt_tab[h].part != part; n++)
        h = (h + 1) & ((1u << PT_BITS) - 1);
    pt_tab[h].part = part;
    pt_tab[h].actor = actor;
    pt_tab[h].type = actor > 0x10000 ? *(volatile int16_t *)(uintptr_t)(actor + 0x2c) : -1;
}

int bb_part_type(uint32_t part, uint32_t *actor)
{
    uint32_t h = (part * 2654435761u) >> (32 - PT_BITS);
    for (int n = 0; n < 64 && pt_tab[h].part; n++, h = (h + 1) & ((1u << PT_BITS) - 1))
        if (pt_tab[h].part == part) {
            *actor = pt_tab[h].actor;
            return pt_tab[h].type;
        }
    *actor = 0;
    return -1;
}

static uint8_t actor_fl(uint32_t actor)
{
    if (!actor || actor < 0x10000)
        return 0;
    uint32_t f = RD32(actor + 0xc);
    int type = *(volatile int16_t *)(uintptr_t)(actor + 0x2c);
    if (!(f & 0x2000000u) || (type != 9 && !(f & 0x20000000u)))
        return 0;
    int k = 0;
    while (k < bb_sky_seen_n && bb_sky_seen[k] != actor)
        k++;
    if (k == bb_sky_seen_n && bb_sky_seen_n < 8)
        bb_sky_seen[bb_sky_seen_n++] = actor;
    // задник — только тип 9; тип 14 с 0x20000000 — летающие объекты без тумана (птеродактиль L01A): рисовать их первым
    // нельзя — оказывались за дальними скалами и красили цвет дымки
    return type == 9 ? BB_VF_NOFOG | BB_VF_SKY : BB_VF_NOFOG;
}

static const struct { uint32_t slot, expect; uint8_t kind; } writers[] = {
    { 0x4b1aa4, 0x40fa90, BB_TAG_BLOCK },    // C2 комнаты
    { 0x7c9618, 0x411350, BB_TAG_PART },     // C4 части: актор / тень / 3D-HUD — по адресу возврата
    { 0x7c624c, 0x40a6c0, BB_TAG_BB },       // C6 билборды
    { 0x7c6250, 0x40b130, BB_TAG_TEXT },     // C7 текст
    { 0x4b1a5c, 0x40b980, BB_TAG_OVL },      // C7 фейд-оверлеи
    { 0x4b1a60, 0x40be10, BB_TAG_OVL },
    { 0x4b1a64, 0x40c5e0, BB_TAG_BARS },     // C7 киношные полосы
    { 0x4b1a68, 0x40c850, BB_TAG_HUDBAR },   // C7 полоска HUD
};

static uint32_t entry_idx[256], entry_obj[256];
static uint16_t entry_local[256];
static uint8_t entry_kind[256], entry_fl[256];
static int depth;

// порядковые номера билбордов у актора в текущем построении
static struct { uint32_t actor; uint16_t n; } bb_ord[512];
static int bb_ord_n;

static uint16_t bb_ordinal(uint32_t actor)
{
    for (int i = 0; i < bb_ord_n; i++)
        if (bb_ord[i].actor == actor)
            return ++bb_ord[i].n;
    if (bb_ord_n < 512) {
        bb_ord[bb_ord_n].actor = actor;
        bb_ord[bb_ord_n].n = 0;
        bb_ord_n++;
    }
    return 0;
}
uint32_t bb_mark_written[BB_TAG_COUNT], bb_mark_anomaly;

static void w_pre(int k, uint32_t *esp)
{
    uint8_t kind = writers[k].kind;
    if (kind == BB_TAG_PART) {
        switch (esp[0]) {
        case 0x44ca31: kind = BB_TAG_ACTOR; bb_p7_part(esp[1]); break;
        case 0x44be06: kind = BB_TAG_SHADOW; break;
        case 0x44b456: case 0x44b482: kind = BB_TAG_HUD3D; break;
        }
    }
    uint32_t obj = 0;
    uint16_t local = 0;
    uint8_t fl = 0;
    switch (kind) {
    case BB_TAG_BLOCK:  obj = esp[2]; break;                          // 0x40fa90(n, src*)
    case BB_TAG_ACTOR: {                                              // 0x411350(part*); актор — [gd+0x34] (0x44c4b0)
        uint32_t gd = RD32(0x4efac0);
        obj = esp[1];
        fl = gd ? actor_fl(RD32(gd + 0x34)) : 0;
        if (gd)
            pt_note(obj, RD32(gd + 0x34));
        break;
    }
    case BB_TAG_HUD3D:  obj = esp[1] ? esp[1] | 3u : 0; break;        // 3D-HUD (голова с вентилятором) — тоже part*
    case BB_TAG_SHADOW: {                                             // part* у теней — общий скретч
        uint32_t o = RD32(0x4efac0);
        obj = o ? RD32(o + 0x30) | 1u : 0;                            // |1 — отличить от части того же адреса
        break;
    }
    case BB_TAG_BB: {                                                 // 10-й dword — экземпляр, +0x34 → актор
        uint32_t inst = esp[10];
        obj = inst ? RD32(inst + 0x34) | 2u : 0;
        local = (uint16_t)(bb_ordinal(obj) << 4);
        fl = inst ? actor_fl(RD32(inst + 0x34)) : 0;
        break;
    }
    }
    if (depth < 256) {
        entry_idx[depth] = RD32(G_POOL_IDX);
        entry_kind[depth] = kind;
        entry_obj[depth] = obj;
        entry_local[depth] = local;
        entry_fl[depth] = fl;
    }
    depth++;
}

static void w_post(int k, uint32_t *esp)
{
    (void)k; (void)esp;
    if (--depth >= 256)
        return;
    uint32_t a = entry_idx[depth], b = RD32(G_POOL_IDX);
    if (b < a || b > BB_POOL_CAP) {
        bb_mark_anomaly++;
        return;
    }
    memset(bb_pool_tag + a, entry_kind[depth], b - a);
    memset(bb_pool_fl + a, entry_fl[depth], b - a);
    for (uint32_t i = a; i < b; i++) {
        bb_pool_obj[i] = entry_obj[depth];
        bb_pool_local[i] = (uint16_t)(entry_local[depth] + (i - a));
    }
    bb_mark_written[entry_kind[depth]] += b - a;
}

static void on_begin_build(int k, uint32_t *esp)
{
    (void)k;
    memset(bb_pool_tag, 0, sizeof bb_pool_tag);
    memset(bb_pool_obj, 0, sizeof bb_pool_obj);
    bb_ord_n = 0;
    bb_fog_build_begin();
    bb_cam_on_build();
    bb_hor_build_begin(esp[0]);
}

static void on_begin_build_post(int k, uint32_t *esp)
{
    (void)k; (void)esp;
    bb_hor_build_end();
}

void bb_mark_install_slots(void)
{
    for (int k = 0; k < (int)(sizeof writers / sizeof writers[0]); k++)
        if (bb_around_slot(k, writers[k].slot, writers[k].expect, w_pre, w_post))
            bb_log("P6: слот [%08x] (%08x) -> обёртка %d", writers[k].slot, writers[k].expect, k);
}

void bb_mark_init(void)
{
    bb_around_site(11, BB_SITE_BEGINBUILD, on_begin_build, on_begin_build_post);
}

const char *bb_tag_name(int t)
{
    static const char *const names[BB_TAG_COUNT] = {
        "none", "block", "part?", "actor", "shadow", "hud3d", "bb", "text", "ovl", "bars", "hudbar",
    };
    return t >= 0 && t < BB_TAG_COUNT ? names[t] : "?";
}
