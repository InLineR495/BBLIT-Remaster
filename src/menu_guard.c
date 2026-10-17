// M2 — I11 (вариант «оригинал + откат»): охрана видео-меню паузы. Слот actionTable[0x76] = [0x4ac8b8]
// (оригинал 0x42e050) подменяется обёрткой: оригинал выполняется целиком (туман, WriteConfig, [0x45f2b0]=0x13 →
// Reinit — через него меню и закрывается), а сразу после него W/H/fullscreen/renderer возвращаются к значениям
// до вызова (из командной строки) и config.pc перезаписывается. Так «OK» не снимает /win, не пишет 0×0
// и не уводит в софт, но меню ведёт себя как в оригинале.
// Проверено 2026-10-15: вариант «только туман, без Reinit» (DESIGN I11) оставляет меню открытым — OK «не работает».
#include <windows.h>
#include <stdint.h>
#include "hook.h"
#include "log.h"
#include "remaster.h"

#define RD32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define WR32(a, v) (*(volatile uint32_t *)(uintptr_t)(a) = (v))
#define SLOT_VIDEO_APPLY  0x4ac8b8u
#define FN_VIDEO_APPLY    0x42e050u
#define FN_WRITE_CONFIG   0x409a10u
#define G_W 0x9ca830u
#define G_H 0x9ca834u
#define G_FULL 0x9ca838u
#define G_RENDERER 0x9ca824u

static uint32_t __cdecl g2_video_apply(uint32_t ent, uint32_t item)
{
    uint32_t w = RD32(G_W), h = RD32(G_H), full = RD32(G_FULL), rend = RD32(G_RENDERER);
    int guard = bb_cfg.video_guard && RD32(0x4ac094) == 2;
    bb_log("I11: запрос видео-меню: fullscreen=%d W=%u H=%u fog=%u renderer=%u | сейчас W=%u H=%u full=%u fog=%u renderer=%u",
           (int)(int8_t)RD32(0x4b3e6c), RD32(0x4b3e60), RD32(0x4b3e64), RD32(0x4b3e68), RD32(0x4b3e70),
           w, h, full, RD32(0x9ca83c), rend);
    uint32_t r = ((uint32_t (__cdecl *)(uint32_t, uint32_t))FN_VIDEO_APPLY)(ent, item);
    if (!guard)
        return r;
    if (RD32(G_W) != w || RD32(G_H) != h || RD32(G_FULL) != full || RD32(G_RENDERER) != rend) {
        bb_log("I11: оригинал записал W=%u H=%u full=%u renderer=%u — возвращаю %u/%u/%u/%u и перезаписываю config.pc",
               RD32(G_W), RD32(G_H), RD32(G_FULL), RD32(G_RENDERER), w, h, full, rend);
        WR32(G_W, w);
        WR32(G_H, h);
        WR32(G_FULL, full);
        WR32(G_RENDERER, rend);
        ((void (__cdecl *)(void))FN_WRITE_CONFIG)();
    }
    bb_log("I11: туман %u, Reinit поставлен ([0x45f2b0]=%#x)", RD32(0x9ca83c), RD32(0x45f2b0));
    return r;
}

// P4: каждый Reinit (0x40e790) с адресом возврата — уход в софт или пересоздание окна видны сразу.
static uint32_t reinit_count;

static void on_reinit(uint32_t *esp)
{
    bb_log("P4: Reinit #%u (ret %08x) [0x45f2b0]=%#x full=%u renderer=%u [0x4ac094]=%u W×H=%u×%u",
           ++reinit_count, esp[0], RD32(0x45f2b0), RD32(0x9ca838), RD32(0x9ca824), RD32(0x4ac094),
           RD32(0x9ca830), RD32(0x9ca834));
}

void bb_menu_guard_init(void)
{
    bb_detour_pre(BB_SITE_REINIT, on_reinit);
    if (!bb_cfg.video_guard)
        return;
    if (RD32(SLOT_VIDEO_APPLY) != FN_VIDEO_APPLY) {
        bb_log("I11: слот [%08x] = %08x, ожидалось %08x — охрана не ставится", SLOT_VIDEO_APPLY, RD32(SLOT_VIDEO_APPLY), FN_VIDEO_APPLY);
        return;
    }
    uint32_t p = (uint32_t)(uintptr_t)g2_video_apply;
    if (bb_patch(SLOT_VIDEO_APPLY, &p, 4))
        bb_log("I11: слот видео-меню [%08x] -> охрана", SLOT_VIDEO_APPLY);
}
