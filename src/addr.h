// Адреса bugs.exe и ожидаемые байты.
// Исходники пишут адреса кода эталона 1.6 (раскладка A, md5 6672ce79f18cf5110e00059d5f031a35). Для сборок с другой
// раскладкой кода их переводит BB_CODE() по таблице BB_CODES. Данные (.rdata, .data, .bss) во всех известных сборках
// лежат на тех же адресах: сверено по всем обращениям кода к данным (docs/porting-editions.md).
// Строки BB_PROFILES/BB_SITES/BB_IATS/BB_CODES разбирает tools/verify_exe.py, адреса кода в исходниках сверяет
// tools/check_addr.py — формат не менять.
// Перед каждой записью код сверяет байты сайта; при несовпадении патч не ставится.
#pragma once
#include <stdint.h>

#define BB_IMAGE_BASE        0x400000u
#define BB_MARKER_ADDR       0x45ca00u      // qword в .rdata: отпечаток сборки (у раскладок A и B разный), не номер версии
#define BB_TEXT_VA           0x401000u
#define BB_TEXT_SIZE         0x5a400u       // raw 0x400..0x5a800

// Известные сборки: P(id, "название", CRC32 .text, маркер, раскладка кода: 0 — A, 1 — B).
// A — 1.6 от 26.09.1999: североамериканский диск (англ./франц./исп.) и русская CITY — тот же exe, у CITY снята проверка
// диска и переведена строка ресурса. B — 1.0 от 12.10.1999: европейский мультиязычный диск Infogrames и испанское
// издание (CRC разный, раскладка кода одна). Пропатченные сборки (hpRT и т. п.) дают другой CRC — мод выключается.
#define BB_PROFILES(P) \
    P(RU16, "bugs.exe 1.6 (Russian, CITY)",              0xecffb7c9u, 0x3fe5555555555555ull, 0) \
    P(NA16, "bugs.exe 1.6 (North America, En/Fr/Es)",    0xc1d00325u, 0x3fe5555555555555ull, 0) \
    P(EU10, "bugs.exe 1.0 (Europe, multi-language)",     0x873df3b9u, 0x3fe2d2d2d2d2d2d3ull, 1) \
    P(ES10, "bugs.exe 1.0 (Spanish, Perdido en el Tiempo)", 0x4df536d5u, 0x3fe2d2d2d2d2d2d3ull, 1)

// X(имя, VA раскладки A, "ожидаемые байты"). VA раскладки B — по BB_CODES; у call rel32 (e8) цель тоже переводится.
#define BB_SITES(X) \
    X(ENTRY0,     0x451eb0, "55 8b ec 6a ff") \
    X(FRAMETICK,  0x4065e0, "55 8b ec 83 ec 20") \
    X(PACER,      0x406ee0, "83 ec 08 53 55") \
    X(LEVELLOAD,  0x42e7a0, "81 ec 5c 03 00 00") \
    X(WMPAINT_C8, 0x40e323, "a1 70 1a 4b 00 3b c3 74 02 ff d0") \
    X(BEGINBUILD, 0x40ceb0, "83 ec 6c 8b 44 24 70") \
    X(ACTOR_SPAWN, 0x448d40, "66 8b 44 24 04") \
    X(ACTOR_FREE,  0x447a50, "a1 b8 39 4b 00") \
    X(REINIT,      0x40e790, "8b 0d 44 f3 45 00") \
    X(QUAD_SPLIT,  0x414c0a, "f6 c1 02") \
    X(H1A,         0x4481c7, "81 ff 00 ff ff ff") \
    X(H1B,         0x4481d1, "81 fa 00 01 00 00") \
    X(H1C,         0x4484b7, "66 3d 00 ff") \
    X(H1D,         0x4484bd, "66 3d 00 01") \
    X(SHADOW40,    0x44c981, "0f 8d f0 01 00 00") \
    X(FOGPARAMS,   0x40cc80, "a1 3c a8 9c 00") \
    X(AR_CMP,      0x447f3a, "66 8b 51 3e 3b c2") \
    X(FT_DEPTH,    0x410f6f, "8a 55 01 3a ca") \
    X(A2_MASK,     0x448074, "f6 45 02 01") \
    X(YG1,         0x410070, "66 a1 4e fb 4e 00") \
    X(YG2,         0x4100ba, "66 a1 4e fb 4e 00") \
    X(YG3,         0x411c7d, "66 8b 0d 4e fb 4e 00") \
    X(YG_PORTAL,   0x410c15, "db 05 1c 16 62 00") \
    X(PT_XMAX,     0x410b62, "db 05 14 16 62 00") \
    X(PT_XMIN,     0x410bbe, "db 05 10 16 62 00") \
    X(BACKFACE,    0x40fdc0, "55 8b ec 83 ec 10") \
    X(N1_PADUPD,   0x40660b, "e8 b0 2b 04 00") \
    X(N2_MAPPAD,   0x449223, "e8 e8 9d fb ff") \
    X(K2_CAM,      0x4323ce, "8b 1d 40 39 4b 00") \
    X(PARTPOOL,    0x430964, "0f bf 05 44 22 4b 00") \
    X(COND_DIVE,   0x42b3f0, "a1 4c 32 4b 00 8b 40 18 25 ff 00 00 00 c1 e8 02 83 e0 01 c3")

// I(имя, VA слота IAT в раскладке A, в раскладке B, "DLL", "функция"). В B импорт длиннее (OemKeyScan) — слоты сдвинуты.
#define BB_IATS(I) \
    I(DDRAW_CREATE,    0x9cc28c, 0x9cc290, "DDRAW.dll",    "DirectDrawCreate") \
    I(DDRAW_ENUM_A,    0x9cc290, 0x9cc294, "DDRAW.dll",    "DirectDrawEnumerateA") \
    I(FREELIBRARY,     0x9cc2c8, 0x9cc2cc, "KERNEL32.dll", "FreeLibrary") \
    I(GETPROCADDRESS,  0x9cc2d8, 0x9cc2dc, "KERNEL32.dll", "GetProcAddress") \
    I(LOADLIBRARYA,    0x9cc2dc, 0x9cc2e0, "KERNEL32.dll", "LoadLibraryA") \
    I(CREATEWINDOWEXA, 0x9cc41c, 0x9cc428, "USER32.dll",   "CreateWindowExA") \
    I(SETWINDOWPOS,    0x9cc3fc, 0x9cc3f8, "USER32.dll",   "SetWindowPos")

// C(VA раскладки A, VA раскладки B) — все адреса кода, которые используют исходники (сайты, функции, цели переходов
// заглушек, адреса возврата). Сопоставлены выравниванием дизассемблеров 1.6 и 1.0: функции те же, сдвинуты вставками.
#define BB_CODES(C) \
    C(0x403010, 0x403010) C(0x405c26, 0x405c26) C(0x405d1f, 0x405d1f) C(0x405ea9, 0x405ea9) C(0x4065e0, 0x406600) C(0x40660b, 0x40662b) \
    C(0x406851, 0x406871) C(0x4069c0, 0x4069e0) C(0x406be1, 0x406c01) C(0x406d4c, 0x406d6c) \
    C(0x406ee0, 0x406f00) C(0x409a10, 0x409a40) C(0x40a6c0, 0x40a720) C(0x40b130, 0x40b190) \
    C(0x40b980, 0x40b9e0) C(0x40be10, 0x40be70) C(0x40c5e0, 0x40c640) C(0x40c850, 0x40c8b0) \
    C(0x40cc80, 0x40cce0) C(0x40ceb0, 0x40cf10) C(0x40e323, 0x40e37f) C(0x40e790, 0x40e820) \
    C(0x40fa90, 0x40fb20) C(0x40fdc0, 0x40fe50) C(0x410070, 0x410100) C(0x4100ba, 0x41014a) \
    C(0x410b62, 0x410bf2) C(0x410bbe, 0x410c4e) C(0x410c15, 0x410ca5) C(0x410f6f, 0x410fff) \
    C(0x410f74, 0x411004) C(0x410f90, 0x411020) C(0x410fb4, 0x411044) C(0x410fc0, 0x411050) \
    C(0x411350, 0x4113e0) C(0x411c7d, 0x411d0d) C(0x4124b0, 0x412540) C(0x414590, 0x414620) \
    C(0x414b40, 0x414bd0) C(0x414c0a, 0x414c9a) C(0x42b3f0, 0x42b470) \
    C(0x42e050, 0x42e0f0) C(0x42e7a0, 0x42e910) C(0x430964, 0x430ad4) \
    C(0x4323ce, 0x43253e) C(0x432436, 0x4325a6) C(0x447a50, 0x447bc0) C(0x447f3a, 0x4480aa) \
    C(0x448074, 0x4481e4) C(0x4481c7, 0x448337) C(0x4481d1, 0x448341) C(0x4484b7, 0x448627) \
    C(0x4484bd, 0x44862d) C(0x448d40, 0x448eb0) C(0x4491c0, 0x449330) C(0x449223, 0x449393) \
    C(0x44b456, 0x44b5c6) C(0x44b482, 0x44b5f2) C(0x44be06, 0x44bf76) C(0x44c981, 0x44caf1) \
    C(0x44ca31, 0x44cba1) C(0x451eb0, 0x452050) C(0x451eb5, 0x452055)

// Адрес кода раскладки A -> адрес в текущем хосте (0 — нет в BB_CODES; tools/check_addr.py не даёт до этого дойти).
uint32_t bb_code(uint32_t va);
#define BB_CODE(va) bb_code(va)
// Адрес кода раскладки A в таблице-инициализаторе: переводится bb_code() там, где используется.
#define BB_A(va) (va)

// Проверка диска при запуске (main): нет диска с меткой BBLIT — окно «Please insert the Bugs Bunny Lost In Time
// CD-ROM» и повтор. В 1.6 её сняла CITY (mov esi,1; jmp), в 1.0 она есть — мод снимает её в памяти (lateinit.c).
#define BB_CD_CHECK_VA       BB_A(0x405c26u)    // test esi,esi; jne BB_CD_CHECK_GO (диск найден)
#define BB_CD_CHECK_GO       BB_A(0x405d1fu)
#define BB_CD_CHECK_BYTES    "85 f6 0f 85 f1 00 00 00"
#define BB_CD_CHECK_REMOVED  "be 01 00 00 00 e9"     // уже снята в самом exe

// Слоты диспетчера рендера (пишет Reinit, в файле нули) и функции, на которые они указывают.
#define BB_SLOT_DRAW         0x4b1a70u      // C8: WM_PAINT 0x40e323 call [slot]
#define BB_FN_DRAW           BB_CODE(0x4124b0u)   // Draw3DFrame: сортировка + отрисовка, без аргументов
#define BB_SLOT_EMIT_TRI     0x4b1a78u      // G4-E1: emit(arena_ptr, 5 дескрипторов) -> байт, cdecl
#define BB_FN_EMIT_TRI       BB_CODE(0x414590u)
#define BB_SLOT_EMIT_QUAD    0x4b1a74u      // G4-E2
#define BB_FN_EMIT_QUAD      BB_CODE(0x414b40u)

// Слот таблицы условий логики [0x4ac550 + 4·код] (.data, VA одинаков в A и B): код 0x34 «игрок в действии нырка» —
// функция BB_SITE_COND_DIVE (A 0x42b3f0, B 0x42b470). fog.c ставит в слот свою обёртку (нора L03A, 2026-10-18).
#define BB_SLOT_COND_DIVE    0x4ac620u

#define BB_ENUM_SITE(name, va, bytes) BB_SITE_##name,
enum { BB_SITES(BB_ENUM_SITE) BB_SITE_COUNT };
#undef BB_ENUM_SITE

#define BB_ENUM_IAT(name, va_a, va_b, dll, fn) BB_IAT_##name,
enum { BB_IATS(BB_ENUM_IAT) BB_IAT_COUNT };
#undef BB_ENUM_IAT

#define BB_ENUM_PROFILE(id, name, crc, marker, layout) BB_PROFILE_##id,
enum { BB_PROFILES(BB_ENUM_PROFILE) BB_PROFILE_COUNT };
#undef BB_ENUM_PROFILE

// Выбор сборки по CRC32 .text и маркеру: индекс BB_PROFILE_* или -1. Ставит раскладку для bb_code/сайтов/IAT.
int bb_host_select(uint32_t text_crc, uint64_t marker);
const char *bb_host_name(void);
