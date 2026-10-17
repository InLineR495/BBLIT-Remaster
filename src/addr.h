// Адреса bugs.exe v1.6 (эталон md5 6672ce79f18cf5110e00059d5f031a35) и ожидаемые байты.
// Строки BB_SITE/BB_IAT разбирает tools/verify_exe.py — формат не менять.
// Перед каждой записью код сверяет байты сайта; при несовпадении патч не ставится.
#pragma once

#define BB_IMAGE_BASE        0x400000u
#define BB_VERSION_ADDR      0x45ca00u
#define BB_VERSION_1_6       0x3fe5555555555555ull
#define BB_TEXT_VA           0x401000u
#define BB_TEXT_SIZE         0x5a400u       // raw 0x400..0x5a800
#define BB_TEXT_CRC32        0xecffb7c9u    // hpRT и прочие сборки дают другой CRC

// X(имя, VA, "ожидаемые байты")
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
    X(K2_CAM,      0x4323ce, "8b 1d 40 39 4b 00")

// I(имя, VA слота IAT, "DLL", "функция")
#define BB_IATS(I) \
    I(DDRAW_CREATE,    0x9cc28c, "DDRAW.dll",    "DirectDrawCreate") \
    I(DDRAW_ENUM_A,    0x9cc290, "DDRAW.dll",    "DirectDrawEnumerateA") \
    I(FREELIBRARY,     0x9cc2c8, "KERNEL32.dll", "FreeLibrary") \
    I(GETPROCADDRESS,  0x9cc2d8, "KERNEL32.dll", "GetProcAddress") \
    I(LOADLIBRARYA,    0x9cc2dc, "KERNEL32.dll", "LoadLibraryA") \
    I(CREATEWINDOWEXA, 0x9cc41c, "USER32.dll",   "CreateWindowExA") \
    I(SETWINDOWPOS,    0x9cc3fc, "USER32.dll",   "SetWindowPos")

// Слоты диспетчера рендера (пишет Reinit, в файле нули) и функции, на которые они указывают.
#define BB_SLOT_DRAW         0x4b1a70u      // C8: WM_PAINT 0x40e323 call [slot]
#define BB_FN_DRAW           0x4124b0u      // Draw3DFrame: сортировка + отрисовка, без аргументов
#define BB_SLOT_EMIT_TRI     0x4b1a78u      // G4-E1: emit(arena_ptr, 5 дескрипторов) -> байт, cdecl
#define BB_FN_EMIT_TRI       0x414590u
#define BB_SLOT_EMIT_QUAD    0x4b1a74u      // G4-E2
#define BB_FN_EMIT_QUAD      0x414b40u

#define BB_ENUM_SITE(name, va, bytes) BB_SITE_##name,
enum { BB_SITES(BB_ENUM_SITE) BB_SITE_COUNT };
#undef BB_ENUM_SITE

#define BB_ENUM_IAT(name, va, dll, fn) BB_IAT_##name,
enum { BB_IATS(BB_ENUM_IAT) BB_IAT_COUNT };
#undef BB_ENUM_IAT
