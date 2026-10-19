// Звук: «вечная» зацикленная петля (L03A: треск факела 94 после поджига бочки, 2026-10-18).
//
// Движок (звуковой модуль 0x41d5c0..0x41f7f0, DirectSound поверх «голосов» PS1) держит два слоя:
//   голоса  0x4b3260 [24 × 0x14]: +0 u16 состояние (0x8000 занят, 1 старт ждёт, 2 стоп ждёт, 4 играет), +4 владелец
//           (актор), +8 описание звука (8 байт, 0x4b24c0), +0xc/+0xd громкость L/R, +0xe группа старта;
//   петли   0x553200 [≤10 × 0x10], число [0x4b1c84]: +0 сэмпл, +4 пара буферов (вступление; петля — +1), +8 фаза, +0xc владелец.
// Петлю останавливает только стоп через голос с тем же (сэмпл, владелец): 0x41f1ef..0x41f2b1 — Stop обоих буферов,
// удаление записи со сдвигом, [0x4b1c84]--, голос = 0. Стоп приходит командой (0x41deb0, cmd 2), автостопом при
// ActorFree (0x41f5d0, только описания с +4 & 0x10) и, как задумано, из обновления позиционной петли.
// Обновление (описание +4 & 0x20, состояние 4, 0x41f33c) раз в тик считает громкость 0x41f620 по дистанции до ЦЕЛИ камеры
// [0x4b38cc] (≥ ~4064 → L = R = 0) и при нуле (или владельце 0) делает 0x41f3d4: mov word [esi], 0x0002.
// Это не стоп: стоп везде 0x8002 (0x41dfc5, 0x41e019, 0x41f603). У 0x0002 нет 0x8000 — голос свободен, а Stop не вызван,
// запись петли осталась, буфер крутится с LOOPING на последней громкости (минимум движка −29,8 дБ, не тишина), и ни
// стоп владельца, ни автостоп сироту не находят (ищут только 0x8000) — убирает лишь загрузка уровня (0x41da00).
// Заодно сироты съедают 10 записей петель и пары буферов (5 на сэмпл): новые петли перестают стартовать.
// ×4 (actor_dist 400) делает это гарантированным: дальние факелы рождаются дальше 4096 от цели камеры.
//
// fix_sfx = 0 — как в оригинале (0x0002). 1 (по умолчанию) — «виртуальный голос»: петля вне слышимости останавливается
// штатно (Stop обоих буферов, запись удалена), а голос не отдаётся, а засыпает (0x8008); раз в тик стоковый цикл доходит
// до него (0x41f2e1), мы считаем громкость 0x41f620 и при подходе взводим старт (0x8001) — дальше всё делает стоковый
// старт 0x41ef5b. Стоп владельца и автостоп видят голос (0x8000) и снимают его штатно. 2 — только штатный стоп: 0x8002
// вместо 0x0002 (стоковый key-off в следующем тике), без возврата при подходе.
// Условия логики 0x55/0x57 («играет» = флаги & 5) у спящего такие же, как у сироты: «не играет».
// Повторные запросы. Скрипты и анимации просят зацикленный звук снова и снова (L05A2: звук 88 у одного владельца каждые
// ~0,2 с), а SndCmd 0x41deb0 дублей (владелец, звук) не ищет — каждый запрос занимает новый голос. В стоке лишний голос
// усыновляет запись петли и освобождается при key-off; у «спящего» он бы остался ещё одним спящим (потолок SLEEP_MAX
// забивался одним владельцем, слоты голосов кончались, новые разовые звуки терялись, а после потолка каждый повтор давал
// Play на минимуме и Stop в следующем тике). Поэтому: (1) на каждый (владелец, сэмпл) — не больше одного спящего: key-off
// дубля отдаёт его голос (on_keyoff); (2) запрос, пришедший при спящем двойнике вне слышимости, гасится до старта
// (pre_tick, до стокового обновления), а в слышимости — заменяет спящего и стартует как в стоке; (3) если свободных голосов
// меньше SLOT_RESERVE, спящие уступают места (разовым звукам нужны слоты, SndCmd при 24/24 молча отбрасывает запрос).
// Адреса — раскладка A; в B весь модуль сдвинут на +0x80 (BB_CODES, сверено выравниванием 2905 инструкций). Проверено на
// настоящем коде движка RU 1.6 и EU 1.0 в эмуляторе (re/scripts/emu: s2..s6, fuzz, lock_real, dfz5; DirectSound — заглушка);
// на устройстве — только по таблицам голосов (чтение памяти игры), на слух и под реальным DirectSound/Wine — нет.
#include <windows.h>
#include <stdint.h>
#include <string.h>
#include "hook.h"
#include "log.h"
#include "remaster.h"

#define RD8(a)  (*(volatile uint8_t *)(uintptr_t)(a))
#define RD16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define RD32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define WR16(a, v) (*(volatile uint16_t *)(uintptr_t)(a) = (uint16_t)(v))
#define WR32(a, v) (*(volatile uint32_t *)(uintptr_t)(a) = (uint32_t)(v))

#define S_DS        0x4b1c74u   // IDirectSound* (0 — звука нет)
#define S_SLOTS     0x4b3260u
#define S_NSLOTS    24
#define S_SLOT_SZ   0x14
#define S_LOOPS     0x553200u
#define S_NLOOPS    0x4b1c84u
#define S_LOOPS_MAX 10
#define S_SAMPLES   0x54efe0u   // [сэмпл 1..0x7f] → объект 0x38: +0..+0x24 IDirectSoundBuffer*[10], +0x34 признак петли
#define S_PROG1     0x54afc4u   // ((банк << 4) + программа) · 8 → u16 номер сэмпла
#define S_PROG2     0x54f1e4u   // то же для описаний с +5 & 4, номер сэмпла + word [0x4b1c80]
#define S_PROG2_ADD 0x4b1c80u
#define S_GROUP     0x4b39c0u   // параметр обновления голосов (FrameTick 0x406617 кладёт его аргументом в 0x41edb0; 0 в обычном тике): старт идёт только при [голос + 0xe] == он (0x41ef63)

#define F_USED  0x8000
#define F_START 0x0001
#define F_STOP  0x0002
#define F_SLEEP 0x0008          // наш: голос занят, петли нет; стоковый цикл такое состояние пропускает
#define DSB_STOP 0x48           // IDirectSoundBuffer::Stop (vtbl[18])

#define M_OFF   0
#define M_VOICE 1               // виртуальный голос (по умолчанию)
#define M_STOP  2               // только штатный стоп
#define SLEEP_MAX  10           // спящих голосов не больше: 24 слота нужны и разовым звукам
#define SLOT_RESERVE 3          // свободных голосов держим про запас: меньше — спящие освобождаются (SndCmd при 24/24 отбрасывает звук)
#define WAKE_VOL   8            // подъём при громкости ≥ 8 (≈ на 250 ед. ближе границы 4064): без дребезга на границе

// Регистры из заглушки: pushal + pushfl, указатель — на флаги.
struct regs { uint32_t flags, edi, esi, ebp, esp, ebx, edx, ecx, eax; };

static struct {
    uint32_t keyoff, keyoff_loop;                   // 0x41f3d4 сработал / у голоса была живая петля
    uint32_t orphan, stopped, slept, woke, dropped; // сирот создано (fix=0) / штатных стопов / уснуло / проснулось / спящих снято
    uint32_t resets, purged;
    uint32_t merged, replaced, evicted;             // дублей спящего отдано/погашено / спящих заменено новым запросом / уступило место голосам
    uint32_t sec_keyoff, sec_orphan, sec_slept, sec_stopped, sec_woke, sec_merged;   // то же за секунду (P32)
} st;
static int installed;                               // сайты поставлены

// --- разбор описания звука: 0x41eeda..0x41ef32 ---
static uint32_t def_sample(uint32_t def)
{
    uint32_t k = ((uint32_t)RD8(def + 2) << 4) + RD8(def + 3);
    int bank2 = (RD8(def + 5) & 4) != 0;
    uint16_t s = RD16((bank2 ? S_PROG2 : S_PROG1) + k * 8);
    return bank2 ? (uint16_t)(s + RD16(S_PROG2_ADD)) : s;
}

// --- таблица петель ---
static int loop_find(uint32_t smp, uint32_t owner)       // 0x41f217..0x41f221
{
    int n = (int)RD32(S_NLOOPS);
    if (n > S_LOOPS_MAX)
        n = S_LOOPS_MAX;
    for (int i = 0; i < n; i++)
        if (RD32(S_LOOPS + 16u * i) == smp && RD32(S_LOOPS + 16u * i + 12) == owner)
            return i;
    return -1;
}

static void ds_stop(uint32_t buf)
{
    if (!buf)
        return;
    void **vt = *(void ***)(uintptr_t)buf;
    ((HRESULT (WINAPI *)(void *))vt[DSB_STOP / 4])((void *)(uintptr_t)buf);
}

// Как штатный стоп 0x41f235..0x41f2ab: Stop обоих буферов пары, запись удаляется сдвигом, счётчик--.
static void loop_stop(int k)
{
    uint32_t e = S_LOOPS + 16u * k, smp = RD32(e), pair = RD32(e + 4);
    uint32_t obj = smp < 0x80 ? RD32(S_SAMPLES + 4 * smp) : 0;
    int n = (int)RD32(S_NLOOPS);
    if (obj && pair <= 8) {
        ds_stop(RD32(obj + 4 * pair));
        ds_stop(RD32(obj + 4 * (pair + 1)));
    }
    for (int j = k; j + 1 < n; j++)
        memmove((void *)(uintptr_t)(S_LOOPS + 16u * j), (void *)(uintptr_t)(S_LOOPS + 16u * (j + 1)), 16);
    WR32(S_NLOOPS, n - 1);
}

static int slot_alive(uint32_t smp, uint32_t owner)       // есть ли голос (0x8000), которому принадлежит петля
{
    for (int k = 0; k < S_NSLOTS; k++) {
        uint32_t s = S_SLOTS + S_SLOT_SZ * k;
        if ((RD16(s) & F_USED) && RD32(s + 4) == owner && RD32(s + 8) && def_sample(RD32(s + 8)) == smp)
            return 1;
    }
    return 0;
}

// Сироты: записи без голоса. kill — Stop и удаление (один раз при включении fix_sfx на лету).
static int orphans(int kill)
{
    int found = 0;
    if (!RD32(S_DS))
        return 0;
    for (int i = 0; i < (int)RD32(S_NLOOPS) && i < S_LOOPS_MAX;) {
        uint32_t e = S_LOOPS + 16u * i;
        if (slot_alive(RD32(e), RD32(e + 12))) {
            i++;
            continue;
        }
        found++;
        if (kill) {
            loop_stop(i);
            st.purged++;
        } else {
            i++;
        }
    }
    return found;
}

static int is_sleeper(uint32_t v)
{
    return (RD16(v) & (F_USED | 0xff)) == (F_USED | F_SLEEP);
}

static int sleepers(void)
{
    int n = 0;
    for (int k = 0; k < S_NSLOTS; k++)
        n += is_sleeper(S_SLOTS + S_SLOT_SZ * k);
    return n;
}

static int free_slots(void)
{
    int n = 0;
    for (int k = 0; k < S_NSLOTS; k++)
        n += !(RD16(S_SLOTS + S_SLOT_SZ * k) & F_USED);
    return n;
}

// Другой спящий голос той же петли — того же (владелец, сэмпл), что и у голоса except; 0 — нет.
static uint32_t sleeper_twin(uint32_t owner, uint32_t smp, uint32_t except)
{
    for (int k = 0; k < S_NSLOTS; k++) {
        uint32_t s = S_SLOTS + S_SLOT_SZ * k;
        if (s != except && is_sleeper(s) && RD32(s + 4) == owner && RD32(s + 8) && def_sample(RD32(s + 8)) == smp)
            return s;
    }
    return 0;
}

static int owner_alive(uint32_t owner)                    // ActorFree 0x447a50 ставит у актора +4 = 1
{
    return owner && !(RD32(owner + 4) & 1);
}

static int group_ok(uint32_t v)
{
    return RD8(v + 0xe) == RD16(S_GROUP);                  // 0x41ef63: movzx dx,[esi+0xe]; cmp dx,[ebp+8]
}

// --- 0x41f3d4: вместо mov word [esi], 2 (esi — голос, edi — номер сэмпла) ---
static void __cdecl on_keyoff(struct regs *r)
{
    uint32_t v = r->esi, owner = RD32(v + 4), def = RD32(v + 8);
    int k = loop_find(r->edi & 0xffff, owner), mode = bb_cfg.fix_sfx;
    uint32_t twin = 0;
    const char *act;
    st.keyoff++;
    st.sec_keyoff++;
    st.keyoff_loop += k >= 0;
    if (mode == M_OFF) {                                   // оригинал: голос свободен, петля осталась играть
        WR16(v, F_STOP);
        if (k >= 0) {
            st.orphan++;
            st.sec_orphan++;
        }
        act = k >= 0 ? "сирота (как в оригинале)" : "голос свободен";
    } else if (mode == M_VOICE && owner_alive(owner) && group_ok(v) &&
               ((twin = sleeper_twin(owner, r->edi & 0xffff, v)) || sleepers() < SLEEP_MAX)) {
        if (k >= 0)
            loop_stop(k);
        if (twin) {                                        // у петли уже есть спящий голос: этот дубль не нужен
            WR16(v, 0);
            st.merged++;
            st.sec_merged++;
            act = "стоп, голос отдан (спящий двойник уже есть)";
        } else {
            WR16(v, F_USED | F_SLEEP);
            st.slept++;
            st.sec_slept++;
            act = "стоп и сон";
        }
    } else {
        WR16(v, F_USED | F_STOP);                          // штатный стоп в следующем тике
        st.stopped++;
        st.sec_stopped++;
        act = "штатный стоп";
    }
    if (st.keyoff <= 40)
        bb_log("SFX: вне слышимости: звук %u, владелец %08lx (тип %d), петля %s — %s", def ? RD16(def) : 0, (unsigned long)owner,
               owner ? (int)(int16_t)RD16(owner + 0x2c) : -1, k >= 0 ? "была" : "нет", act);
}

static int wake_vol(uint32_t owner, uint32_t def, uint16_t *lr)       // громкость в слышимости: L или R ≥ WAKE_VOL
{
    *lr = ((uint16_t (__cdecl *)(uint32_t, uint32_t))(uintptr_t)BB_CODE(0x41f620u))(owner, def);
    return (*lr & 0xff) >= WAKE_VOL || (*lr >> 8) >= WAKE_VOL;
}

// --- 0x41f2e1: je 0x41f3d9 («не играет, не старт, не стоп») — сюда попадает спящий голос ---
static void __cdecl on_idle(struct regs *r)
{
    uint32_t v = r->esi;
    if ((RD16(v) & (F_USED | 0xff)) != (F_USED | F_SLEEP))
        return;
    uint32_t owner = RD32(v + 4), def = RD32(v + 8);
    if (bb_cfg.fix_sfx != M_VOICE || !owner_alive(owner) || !def || !group_ok(v)) {
        WR16(v, 0);                                         // режим выключили или владельца нет — просто отдать
        st.dropped++;
        return;
    }
    uint16_t lr;
    if (wake_vol(owner, def, &lr)) {
        WR16(v, F_USED | F_START);                          // стоковый старт 0x41ef5b: громкость, запись в 0x553200, Play
        st.woke++;
        st.sec_woke++;
        if (st.woke <= 40)
            bb_log("SFX: подход: звук %u, владелец %08lx (тип %d), громкость %u/%u — старт", RD16(def), (unsigned long)owner,
                   (int)(int16_t)RD16(owner + 0x2c), lr & 0xff, lr >> 8);
    }
}

// --- 0x41da00: звук пересоздаётся (загрузка уровня, старт игры) — голоса прошлой сессии DirectSound не нужны ---
static void on_init(uint32_t *esp)
{
    if (bb_cfg.fix_sfx == M_OFF)
        return;
    for (int k = 0; k < S_NSLOTS; k++)
        WR16(S_SLOTS + S_SLOT_SZ * k, 0);
    st.resets++;
}

// Повторные запросы петли, пока у неё есть спящий голос (звук 88 у одного владельца каждые ~0,2 с): новый голос ждёт
// старта (0x8001). Вне слышимости — гасится до стокового старта (иначе Play на минимуме и Stop в следующем тике, а
// спящий остался бы ещё и с дублем); в слышимости — заменяет спящего, дальше стоковый старт. Затем, если свободных
// голосов меньше SLOT_RESERVE, спящие освобождаются (они вернутся, только если скрипт повторит запрос).
static void pre_tick(void)
{
    for (int k = 0; k < S_NSLOTS; k++) {
        uint32_t v = S_SLOTS + S_SLOT_SZ * k, owner = RD32(v + 4), def = RD32(v + 8), twin;
        uint16_t lr;
        if ((RD16(v) & (F_USED | 0xff)) != (F_USED | F_START) || !def || !(RD8(def + 4) & 0x20) || !owner_alive(owner) ||
            !group_ok(v))
            continue;
        twin = sleeper_twin(owner, def_sample(def), v);
        if (!twin)
            continue;
        if (wake_vol(owner, def, &lr)) {
            WR16(twin, 0);                                  // стартует новый запрос, спящий не нужен
            st.replaced++;
        } else {
            WR16(v, 0);                                     // дубль запроса вдали: спящий уже ждёт
            st.merged++;
            st.sec_merged++;
        }
    }
    for (int k = 0; k < S_NSLOTS && free_slots() < SLOT_RESERVE; k++) {
        uint32_t v = S_SLOTS + S_SLOT_SZ * k;
        if (is_sleeper(v)) {
            WR16(v, 0);
            st.evicted++;
        }
    }
}

// --- 0x41edb0: раз в тик (30 Гц), до стокового обновления голосов: спящие и переключение режима на лету ---
static void on_tick(uint32_t *esp)
{
    static int last_mode = -1;
    int mode = bb_cfg.fix_sfx;
    if (mode == M_VOICE)
        pre_tick();
    if (mode == last_mode)
        return;
    if (last_mode == M_OFF && orphans(1))                      // fix_sfx включили на лету: сирот прошлого режима снять разом
        bb_log("SFX: fix_sfx=%d, сирот снято %lu", mode, (unsigned long)st.purged);
    bb_log("SFX: fix_sfx=%d (%s)", mode, mode == M_VOICE ? "виртуальный голос: стоп и возврат при подходе" :
           mode == M_STOP ? "штатный стоп" : "как в оригинале");
    last_mode = mode;
}

// Строка P32 раз в секунду (probe.c; в тихом режиме log_sec=0 не пишется, счётчики сбрасываются)
void bb_sfx_log_second(void)
{
    if (installed) {
        int orph = orphans(0), sl = sleepers();
        if (st.sec_keyoff || orph || sl || st.sec_woke || st.sec_merged)
            bb_log("P32 звук fix_sfx=%d: петель %lu, сирот %d, спящих %d, голосов свободно %d | за с: вне слышимости %lu (сирот создано "
                   "%lu, уснуло %lu, штатный стоп %lu), проснулось %lu, дублей отдано %lu | всего: сирот снято %lu, спящих сброшено "
                   "%lu, сбросов звука %lu, заменено новым запросом %lu, уступило голоса %lu",
                   bb_cfg.fix_sfx, (unsigned long)RD32(S_NLOOPS), orph, sl, free_slots(), (unsigned long)st.sec_keyoff,
                   (unsigned long)st.sec_orphan, (unsigned long)st.sec_slept, (unsigned long)st.sec_stopped,
                   (unsigned long)st.sec_woke, (unsigned long)st.sec_merged, (unsigned long)st.purged, (unsigned long)st.dropped,
                   (unsigned long)st.resets, (unsigned long)st.replaced, (unsigned long)st.evicted);
    }
    st.sec_keyoff = st.sec_orphan = st.sec_slept = st.sec_stopped = st.sec_woke = st.sec_merged = 0;
}

void bb_sfx_dump(void)
{
    if (!RD32(S_DS)) {
        bb_log("SFX dump: звука нет ([0x4b1c74] = 0)");
        return;
    }
    bb_log("SFX dump fix_sfx=%d: петель %lu, сирот %d, спящих %d", bb_cfg.fix_sfx, (unsigned long)RD32(S_NLOOPS),
           orphans(0), sleepers());
    for (int k = 0; k < S_NSLOTS; k++) {
        uint32_t s = S_SLOTS + S_SLOT_SZ * k, def = RD32(s + 8);
        if (!(RD16(s) & F_USED) || !def)
            continue;
        bb_log("SFX голос %2d: %04x звук %u флаги %04x сэмпл %lu владелец %08lx L/R %u/%u", k, RD16(s), RD16(def),
               RD16(def + 4), (unsigned long)def_sample(def), (unsigned long)RD32(s + 4), RD8(s + 0xc), RD8(s + 0xd));
    }
    for (int i = 0; i < (int)RD32(S_NLOOPS) && i < S_LOOPS_MAX; i++) {
        uint32_t e = S_LOOPS + 16u * i;
        bb_log("SFX петля %d: сэмпл %lu пара %lu фаза %lu владелец %08lx%s", i, (unsigned long)RD32(e),
               (unsigned long)RD32(e + 4), (unsigned long)RD32(e + 8), (unsigned long)RD32(e + 12),
               slot_alive(RD32(e), RD32(e + 12)) ? "" : " — СИРОТА");
    }
}

// Заглушка: pushal; pushfl; push esp; call fn; add esp,4; popfl; popal; jmp ret. Регистры правит fn через struct regs.
static uint8_t *make_stub(void (__cdecl *fn)(struct regs *), uint32_t ret)
{
    static uint8_t *mem;
    static size_t used;
    if (!mem)
        mem = VirtualAlloc(NULL, 128, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!mem || used + 32 > 128)
        return NULL;
    uint8_t *s = mem + used, *p = s;
    used += 32;
    *p++ = 0x60; *p++ = 0x9c; *p++ = 0x54;
    *p++ = 0xe8; *(int32_t *)p = (int32_t)((uintptr_t)fn - (uintptr_t)(p + 4)); p += 4;
    *p++ = 0x83; *p++ = 0xc4; *p++ = 0x04;
    *p++ = 0x9d; *p++ = 0x61;
    *p++ = 0xe9; *(int32_t *)p = (int32_t)(ret - (uintptr_t)(p + 4)); p += 4;
    FlushInstructionCache(GetCurrentProcess(), s, 32);
    return s;
}

void bb_sfx_init(void)
{
    uint32_t back = BB_CODE(0x41f3d9u);
    // все четыре сайта сверяются до первой записи: заглушка ожидания без заглушки засыпания бесполезна, и наоборот
    if (!bb_site_check(BB_SITE_SFX_KEYOFF) || !bb_site_check(BB_SITE_SFX_IDLE) || !bb_site_check(BB_SITE_SFX_INIT) ||
        !bb_site_check(BB_SITE_SFX_TICK) || !back) {
        bb_log("SFX: сайты 0x41f3d4/0x41f2e1/0x41da00/0x41edb0 не совпали — звук как в оригинале");
        return;
    }
    uint8_t *key = make_stub(on_keyoff, back), *idle = make_stub(on_idle, back);
    if (!key || !idle) {
        bb_log("SFX: нет места под заглушки");
        return;
    }
    uint32_t va = bb_site_va(BB_SITE_SFX_IDLE);
    int32_t rel = (int32_t)((uintptr_t)idle - (va + 6));
    // сначала пробуждение (je 0x41f3d9 → заглушка; без спящих она ничего не меняет), потом засыпание
    int ok = bb_patch(va + 2, &rel, 4) && bb_patch_jmp(bb_site_va(BB_SITE_SFX_KEYOFF), key) &&
             bb_detour_pre(BB_SITE_SFX_INIT, on_init) && bb_detour_pre(BB_SITE_SFX_TICK, on_tick);
    installed = ok;
    bb_log("SFX: заглушки keyoff %p, idle %p (%s), fix_sfx=%d", (void *)key, (void *)idle, ok ? "ок" : "ОШИБКА", bb_cfg.fix_sfx);
}
