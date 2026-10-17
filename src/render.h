#pragma once
#include <windows.h>
#include "glrec.h"

int bb_render_ready(void);                             // рендер включён и не отказал
int bb_render_frame(const struct bb_snapshot *s);      // M3: из C8 после оригинала; 1 — кадр нарисован нами
int bb_render_interp(const struct bb_snapshot *a, const struct bb_snapshot *b, double alpha);   // M4: + своп
#define ALPHA_MAX 1.3                                  // M4f: кадр, показанный после следующего тика, — экстраполяция
extern LARGE_INTEGER bb_swap_ret;                      // возврат нашего последнего свопа (при vsync — vblank)
double bb_hor_fcull(void);                             // F_cull текущего построения
void bb_render_engine_context_deleted(HGLRC ctx);      // C15
void bb_hor_build_begin(uint32_t ret);                 // C1 до GsSetRefView2L: сжатие sx (Hor+)
void bb_hor_build_end(void);                           // C1 после: восстановить sx
int bb_render_fog_ok(void);                            // M5b: наш туман возможен (шейдеры не отказали)
void bb_fog_build_begin(void);                         // fog.c: C1 — защёлкнуть туман/дальность, сбросить кэш
void bb_fog_params(double *d, double *m);              // fog.c: D комнат (0 — наш туман выключен) и множитель
void bb_fog_log_second(void);
void bb_fog_init(void);
