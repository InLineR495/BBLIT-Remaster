#pragma once
#include <windows.h>

extern HWND bb_game_hwnd;
void bb_window_init(void);
void bb_window_viewport(int *x, int *y, int *w, int *h);   // C12: вьюпорт движка — вписан в окно по центру
