#pragma once
#include <windows.h>
#include <stdint.h>

struct bb_rec_stats {
    uint32_t blocks, tris, quads, leftover, other_mode_verts, unclosed;
    uint32_t pool_verts, other_verts, imm_verts, ignored_begin, ignored_end;
    uint32_t tex_uploads, tex_deletes, swaps, canary_bind, canary_upload;
    uint32_t swap_us_sum, swap_us_max, swap_slow;   // P11: время wglSwapBuffers, > 20 мс
    uint32_t swaps_skipped;         // M4: своп движка пропущен (выводит наш цикл)
    uint32_t tag_refs[16];          // P6: ссылки на вершины пула по метке писателя (0 — без метки)
};

// --- снимок кадра (M3) ---
#define BB_TAG_IMM          15      // вершина не из пула (glVertex3f или чужой указатель) — экранная
#define BB_CAP_TEXTURE      0x01
#define BB_CAP_BLEND        0x02
#define BB_CAP_ALPHA        0x04
#define BB_CAP_DEPTH        0x08
#define BB_CAP_CULL         0x10
#define BB_CAP_FOG          0x20
#define BB_SNAP_MAX_VERTS   98304
#define BB_SNAP_MAX_PRIMS   32768
#define BB_SNAP_MAX_STATES  4096

struct bb_vert {
    float x, y, z;                  // как отдал движок: для пула — экранные X, Y и Z вида
    float u, v;
    uint32_t rgba;                  // r | g<<8 | b<<16 | a<<24 (цвет уже с туманом и фейдом)
    uint32_t obj;                   // M4: ключ — объект (0 — не интерполировать)
    uint16_t local;                 // M4: ключ — номер вершины в объекте
    uint8_t tag;                    // BB_TAG_* источника (mark.h) или BB_TAG_IMM
    uint8_t fl;                     // BB_VF_*
};
#define BB_VF_NOFOG 1               // M5b: без тумана и дымки (условие 0x44cb90 для базы 0x3fff: тип 9 или 0x20000000)
#define BB_VF_SKY 2                 // M5b: задник (тип 9) — рисуется первым, его копия — цвет тумана

struct bb_state {                   // состояние движка на момент glBegin блока
    unsigned caps, tex, bsrc, bdst, afunc, dfunc, shade;
    float aref;
    double ortho[6];                // l r b t n f проекции
};

struct bb_prim {
    uint32_t first;                 // индекс первой вершины
    uint16_t state;
    uint8_t nverts;                 // 3 (GL_TRIANGLES) или 4 (GL_QUADS)
};

struct bb_snapshot {
    uint32_t nverts, nprims, nstates, overflow;
    double t;                       // QPC (с) публикации — время тика для интерполяции
    uint32_t build_id, level_gen;
    double fcull;                   // F_cull построения (Hor+), 1 — без сжатия
    double cam[9], eye[3];          // камера построения: вид v = C·(w − eye), C = [0x4efae0]/4096 (S·R, Q12)
    double fog_d, fog_m;            // M5b: D комнат (сток; 0 — наш туман выключен) и множитель дальности
    double ply[3];                  // M5b: позиция игрока [[0x4b324c]+0xe4] (мир; верх — −Y), для высоты дымки
    struct bb_vert verts[BB_SNAP_MAX_VERTS];
    struct bb_prim prims[BB_SNAP_MAX_PRIMS];
    struct bb_state states[BB_SNAP_MAX_STATES];
};

extern int bb_gl_sink;                                 // 1 — геометрия движка не идёт в драйвер
extern LARGE_INTEGER bb_swap_t0, bb_swap_t1;           // последний wglSwapBuffers (разбивка тика)
void *bb_gl_intercept(const char *name, void *real);   // из хука GetProcAddress для opengl32
void bb_rec_begin(void);
void bb_rec_end(void);
const struct bb_snapshot *bb_rec_snapshot(void);       // записанный последним (ещё не опубликованный)
void bb_rec_publish(uint32_t build_id, uint32_t level_gen, double fcull);   // сделать его «последним тиком»
const struct bb_snapshot *bb_snap_latest(void);        // M4: последний и предыдущий опубликованные
const struct bb_snapshot *bb_snap_prev(void);
extern int bb_swap_suppress;                           // M4: своп движка пропустить (выводим сами)
void bb_swap_suppress_arm(void);                       // из C8: этот тик выводит наш цикл
int bb_swap_suppress_pending(void);
void bb_swap_suppress_done(void);
BOOL bb_gl_real_swap(HDC dc);
void bb_rec_take(struct bb_rec_stats *out);            // забрать и обнулить накопленное
uint32_t bb_rec_prim_count(void);                      // tri+quad, записанные за всё время
void bb_rec_log_histogram(const char *why);
