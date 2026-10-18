#pragma once
#include <stdint.h>

#define BB_POOL_CAP 35000u

enum {
    BB_TAG_NONE, BB_TAG_BLOCK, BB_TAG_PART, BB_TAG_ACTOR, BB_TAG_SHADOW, BB_TAG_HUD3D,
    BB_TAG_BB, BB_TAG_TEXT, BB_TAG_OVL, BB_TAG_BARS, BB_TAG_HUDBAR, BB_TAG_COUNT
};

extern uint8_t bb_pool_tag[];
extern uint32_t bb_pool_obj[];      // M4: ключ вершины пула — объект (0 — без интерполяции)
extern uint16_t bb_pool_local[];    // M4: ключ вершины пула — номер внутри объекта
extern uint8_t bb_pool_fl[];        // M5b: BB_VF_* вершины пула
extern uint32_t bb_mark_written[BB_TAG_COUNT], bb_mark_anomaly;

void bb_mark_init(void);           // детур C1 (сброс меток на построение)
void bb_mark_install_slots(void);  // лениво из FrameTick: слоты пишет Reinit
const char *bb_tag_name(int t);
int bb_part_type(uint32_t part, uint32_t *actor);  // отладка: тип актора части (−1 — неизвестно)
int bb_dome_dx(uint32_t actor, int32_t d[3]);      // позиция актора − глаз; возврат — построений подряд на глазу
