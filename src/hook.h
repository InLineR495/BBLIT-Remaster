#pragma once
#include <windows.h>
#include <stdint.h>
#include "addr.h"

uint32_t bb_crc32(const void *data, size_t len);

// Сайты кода из BB_SITES: VA, сверка ожидаемых байт, запись.
uint32_t bb_site_va(int site);
int bb_site_check(int site);
int bb_patch(uint32_t va, const void *bytes, size_t len);
int bb_patch_jmp(uint32_t from, const void *to);

// Слоты IAT из BB_IATS: адрес слота сверяется с таблицей импорта exe по имени.
void **bb_iat_slot(int iat);
int bb_iat_hook(int iat, void *fn, void **orig);

// Пре-детур сайта из BB_SITES: pre(esp) вызывается до исходного кода, esp[0] — адрес возврата.
int bb_detour_pre(int site, void (*pre)(uint32_t *esp));

// Обёртка «вокруг» слота-указателя (арность не нужна): если [slot] == expect, ставит заглушку k (0..15).
// pre(k, esp): esp[0] — адрес возврата, esp[1..] — аргументы; post(k, esp): то же после возврата, eax не менять.
int bb_around_slot(int k, uint32_t slot, uint32_t expect, void (*pre)(int, uint32_t *), void (*post)(int, uint32_t *));
// То же для кода функции (сайт из BB_SITES, 5..8 украденных байт). В обоих случаях esp[-1] — сохранённый eax
// (в post — результат функции).
int bb_around_site(int k, int site, void (*pre)(int, uint32_t *), void (*post)(int, uint32_t *));
