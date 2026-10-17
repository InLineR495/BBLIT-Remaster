#pragma once
#include <stdint.h>

#include <windows.h>
extern void (*bb_pacer_observer)(void);   // проба P1: счётчик вызовов пейсера
extern LARGE_INTEGER bb_pacer_exit_ts;
void bb_pacer_init(void);
void bb_pacer_take(uint32_t *n, uint32_t *sleep_us, uint32_t *spin_us);   // среднее за период
void bb_pacer_take_m4(uint32_t *frames, uint32_t *ticks);                 // M4: выведено кадров / тиков
void bb_pacer_swapped(int64_t qpc);                                      // M4f: после нашего свопа
double bb_pacer_vb_ms(void);                                             // M4f: измеренный период vblank, мс
uint32_t bb_pacer_vb_missed(void);                                       // M4g: пропусков vblank с прошлого вызова
