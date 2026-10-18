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

// Thin dark raster border at an image edge: px holds m RGBA pixels from the
// viewport edge inwards. Returns how many of them differ from the background
// before the first background pixel, or -1 if none matches (the artwork
// reaches this edge, so nothing may be painted over it).
static inline int bb_splash_border(const unsigned char (*px)[4], int m, const float rgb[3])
{
    for (int i = 0; i < m; i++) {
        int same = 1;
        for (int c = 0; c < 3; c++) {
            int d = (int)px[i][c] - (int)(rgb[c] * 255.0f + 0.5f);
            if (d > 24 || d < -24)
                same = 0;
        }
        if (same)
            return i;
    }
    return -1;
}
