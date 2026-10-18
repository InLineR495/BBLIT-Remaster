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
    puts("Splash background policy: all regression cases passed");
    return 0;
}
