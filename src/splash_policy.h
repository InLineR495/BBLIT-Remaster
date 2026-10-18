#pragma once

// Only extend a uniform image background. Ignore framebuffer alpha.
static inline int bb_splash_color(const unsigned char corners[4][4], float rgb[3])
{
    for (int c = 0; c < 3; c++) {
        unsigned lo = 255, hi = 0, sum = 0;
        for (int i = 0; i < 4; i++) {
            unsigned v = corners[i][c];
            if (v < lo) lo = v;
            if (v > hi) hi = v;
            sum += v;
        }
        if (hi - lo > 8)
            return 0;
        rgb[c] = sum / (4.0f * 255.0f);
    }
    return 1;
}
