#pragma once
#include <stdint.h>

void bb_cam_init(void);             // детуры спавна/освобождения акторов (P12)
void bb_cam_on_build(void);         // из C1: P10 и список частей игрока
void bb_p7_part(uint32_t part);     // из C4 (часть актора) до проекции
void bb_p9_log_pad(void);           // один раз, в главном потоке
void bb_cam_log_second(void);
void bb_cam_level_reset(void);
