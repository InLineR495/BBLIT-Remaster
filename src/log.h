#pragma once
#include <windows.h>

void bb_log_open(HINSTANCE self);
void bb_log_close(void);
void bb_log(const char *fmt, ...);
extern volatile int bb_log_mute;        // 1 — bb_log молчит (секундные строки в тихом режиме)
