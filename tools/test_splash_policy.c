#include <assert.h>
#include <stdio.h>
#include "../src/splash_policy.h"

int main(void)
{
    float rgb[3];
    const unsigned char white[4][4] = {{255,255,255,0},{255,255,255,255},{255,255,255,0},{255,255,255,255}};
    const unsigned char black[4][4] = {{0},{0},{0},{0}};
    const unsigned char blue[4][4] = {{0,64,255,0},{0,64,255,0},{0,64,255,0},{0,64,255,0}};
    const unsigned char mixed[4][4] = {{255,255,255,0},{0,0,0,0},{255,255,255,0},{0,0,0,0}};
    assert(bb_splash_color(white, rgb) && rgb[0] == 1 && rgb[1] == 1 && rgb[2] == 1);
    assert(bb_splash_color(black, rgb) && rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0);
    assert(bb_splash_color(blue, rgb) && rgb[0] == 0 && rgb[2] == 1);
    assert(!bb_splash_color(mixed, rgb));

    // Raster border: 3 dark pixels at the edge, then background -> 3.
    const float bg[3] = { 248.0f / 255, 248.0f / 255, 248.0f / 255 };
    unsigned char px[8][4] = {{0,0,0,255},{0,0,0,255},{2,2,2,255}};
    for (int i = 3; i < 8; i++) px[i][0] = px[i][1] = px[i][2] = 248;
    assert(bb_splash_border((const unsigned char (*)[4])px, 8, bg) == 3);
    // Background right at the edge -> 0.
    px[0][0] = px[0][1] = px[0][2] = 250;
    assert(bb_splash_border((const unsigned char (*)[4])px, 8, bg) == 0);
    // Artwork reaches the edge (no background in the strip) -> -1.
    for (int i = 0; i < 8; i++) px[i][0] = px[i][1] = px[i][2] = 90;
    assert(bb_splash_border((const unsigned char (*)[4])px, 8, bg) == -1);
    puts("Splash background policy: all regression cases passed");
    return 0;
}
