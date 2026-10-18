#pragma once
#include <stdint.h>

// Geometry and engine state, independent of translated strings or font widths.
static inline int bb_hud_gameplay(uint32_t level, int paused)
{
    return !paused && level > 2 && level < 100;
}

static inline int bb_hud_edge(float x0, float x1, float y0, float y1, int has_icon)
{
    // A text-only group may be a subtitle, dialog choice or menu label.
    if (!has_icon || !(y0 > 0.45f || y1 < -0.45f))
        return 0;
    return x1 < -0.25f ? 1 : x0 > 0.25f ? 2 : 0;
}
