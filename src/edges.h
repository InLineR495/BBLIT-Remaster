#pragma once

void bb_edges_init(void);
void bb_edges_apply(int hor_active, double fcull);   // из C1 на каждом построении
int bb_part_pool_used(int *cap);                     // P13b: занято частей и ёмкость пула
