/*
 * Fuzz check of hand-written core functions (ASTRO_FUZZ, see
 * docs/recompiler.md, "Coverage").
 *
 * Some paths of the hand-written functions are never reached by the
 * recordings: lists that are always empty, full tables, null pointers, clamps.
 * Here each listed function is called many times on random but consistent
 * inputs: its tables and registers are filled in with edge values (counts that
 * match the live entries, a free slot where the code scans for one, non-zero
 * divisors), the way the game could leave them. In the differential build
 * every call goes through the checking wrapper, so each one is compared with
 * the generated version; in the coverage build the gcov counts show which
 * paths the fuzzing reached.
 *
 *   ASTRO_FUZZ=all | <name>[,<name>...]   functions (names as in replaced.h)
 *   ASTRO_FUZZ_ITER=<n>                   calls per function (default 2000)
 *   ASTRO_FUZZ_SEED=<n>                   random seed (default 1)
 *
 * Runs after the shell's start-up (the core initialised, no level loaded)
 * and exits.
 */
#include "astro.h"
#include "core.h"
#include "fuzz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCRATCH   0x03800000u      /* free emulated memory: synthetic records */
#define SCRATCH2  0x03400000u      /* and a second area (copy destinations)    */

/* ---- random values ---------------------------------------------------------- */
static uint64_t rng = 1;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t)(rng >> 16);
}
static uint32_t below(uint32_t n) { return n ? rnd() % n : 0; }
static bool chance(unsigned pct) { return below(100) < pct; }

/* Values near the boundaries the code tests, else small or arbitrary ones. */
static uint16_t edge16(void)
{
    static const uint16_t k[] = {0, 1, 2, 3, 4, 7, 8, 9, 10, 11, 12, 0x10, 0x1f, 0x20, 0x21, 0x7f, 0x80, 0x84,
                                 0xff, 0x100, 0xb3f, 0xb40, 0x7ffe, 0x7fff, 0x8000, 0x8001, 0xff74, 0xff7b,
                                 0xff7c, 0xff7d, 0xfff6, 0xfffc, 0xfffe, 0xffff};
    switch (below(3)) {
    case 0: return k[below(sizeof k / sizeof k[0])];
    case 1: return (uint16_t)(below(0x200) - 0x100);
    default: return (uint16_t)rnd();
    }
}
static uint32_t edge32(void)
{
    static const uint32_t k[] = {0, 1, 2, 3, 4, 0x20, 0x8000, 0xffff, 0x10000, 0x1b5ffff, 0x1b60000, 0x1b60001,
                                 0x7fffffff, 0x80000000u, 0xfffffffcu, 0xfffffffeu, 0xffffffffu};
    switch (below(3)) {
    case 0: return k[below(sizeof k / sizeof k[0])];
    case 1: return (uint32_t)(int32_t)(int16_t)edge16() << (below(2) ? 16 : 0);
    default: return rnd();
    }
}
static void rand_bytes(uint32_t va, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) wr8(va + i, (uint8_t)rnd());
}
static void rand_words(uint32_t va, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) wr16(va + 2 * i, edge16());
}

/* Random general registers (ESP aside) and arithmetic flags; DF stays clear,
 * as the core keeps it. */
static void rand_regs(void)
{
    static const unsigned reg[] = {R_EAX, R_ECX, R_EDX, R_EBX, R_EBP, R_ESI, R_EDI};
    for (unsigned i = 0; i < sizeof reg / sizeof reg[0]; i++) WR_REG(reg[i], 4, chance(30) ? edge32() : rnd());
    static const unsigned fl[] = {R_CF, 0x202, R_ZF, R_SF, R_OF};
    for (unsigned i = 0; i < sizeof fl / sizeof fl[0]; i++) set_flag(fl[i], chance(50));
    set_flag(0x20a, false);
}

/*
 * A list of `n` slots of `stride` bytes from `first` (the address of slot 0),
 * a slot in use when its first word (`width` 2) or dword (4) is non-zero:
 * each slot is randomised (`fill`, else random words) and then marked used
 * with probability `pct_live` (a non-zero edge value) or free. With
 * `need_free` at least one slot stays free. Returns the number in use.
 */
static unsigned slots(uint32_t first, uint32_t stride, unsigned n, int width, unsigned pct_live, bool need_free,
                      void (*fill)(uint32_t e))
{
    unsigned live = 0;
    for (unsigned i = 0; i < n; i++) {
        const uint32_t e = first + i * stride;
        if (fill) fill(e); else rand_words(e, stride / 2);
        bool use = chance(pct_live);
        if (need_free && i == n - 1 && live == n - 1) use = false;
        if (width == 4) wr32(e, use ? (edge32() | (chance(50) ? 1u : 0x10000u)) : 0);
        else {
            uint16_t v = edge16();
            if (!v) v = 1;
            wr16(e, use ? v : 0);
        }
        live += use;
    }
    if (need_free && live == n) {                    /* n == 1 and it was used */
        if (width == 4) wr32(first, 0); else wr16(first, 0);
        live--;
    }
    return live;
}

/* ---- per-function inputs ---------------------------------------------------- */
#define LEVEL_TIME 0x46ffa0u

/* The per-frame list steps: the counter = the live entries (sometimes fewer). */
static void list_step(uint32_t counter, uint32_t first, uint32_t stride, int width)
{
    const unsigned live = slots(first, stride, 1 + below(16), width, 60, false, NULL);
    unsigned count = live;
    if (count && chance(15)) count = 1 + below(count);
    wr16(counter, (uint16_t)count);
    wr32(LEVEL_TIME, rnd());
}
static void in_clear_finished_a(void) { list_step(0x4b0c60, 0x51e700, 0xc, 2); }
static void in_clear_finished_b(void) { list_step(0x4b0dc8, 0x51f354, 0xe, 2); }
static void in_step_entries_b(void) { list_step(0x4b0aa0, 0x51e3d0, 0xc, 2); }
static void in_step_entries_c(void) { list_step(0x4b0ca8, 0x51ef34, 0x24, 2); }
static void in_step_entries_24(void)
{
    list_step(0x4b0a10, 0x51e070, 0x18, 4);
    for (uint32_t e = 0x51e070; e < 0x51e070 + 16 * 0x18; e += 0x18)    /* position + speed = 0 */
        if (chance(15)) wr32(e + 8, 0u - rd32(e));
    wr8(0x4b0a16, (uint8_t)(1 + below(chance(50) ? 4 : 255)));           /* a divisor */
}
static void in_step_entries_14(void)
{
    list_step(0x4b0c94, 0x51ed90, 0xe, 2);
    wr8(0x4b0c9a, (uint8_t)(1 + below(chance(50) ? 4 : 255)));
    wr8(0x4b0c9b, (uint8_t)(1 + below(chance(50) ? 4 : 255)));
}
/* 41bd40: the word at +0x16 picks a pointer from the table at 0x4b16b8 to a
 * record with a limit (+0x18) and a step (+0x1c). */
static void deadline_entry(uint32_t e)
{
    rand_words(e, 0x1e / 2);
    wr16(e + 0x16, (uint16_t)(4 * below(8)));
}
static void in_step_deadlines(void)
{
    for (uint32_t k = 0; k < 8; k++) {
        const uint32_t rec = SCRATCH + 0x40 * k;
        wr32(0x4b16b8 + 4 * k, rec);
        wr32(rec + 0x18, edge32());
        wr32(rec + 0x1c, edge32());
    }
    const unsigned live = slots(0x51fd8c, 0x1e, 1 + below(16), 2, 60, false, deadline_entry);
    wr16(0x4b16b6, (uint16_t)live);
}
static void in_copy_screen(void)
{
    static const uint32_t widths[] = {320, 640, 1280, 2560};
    const uint32_t width = chance(70) ? widths[below(4)] : 1 + below(2600);
    wr32(0x55c804, chance(80) ? edge32() | 1 : 0);
    wr32(0x55b610, SCRATCH);
    wr32(0x55c7fc, SCRATCH2);
    wr32(0x55b5dc, width);
    wr32(0x55c800, width + (chance(50) ? 0 : 4 * below(64)));
    rand_bytes(SCRATCH, 64);                          /* the rest is whatever is there */
}
static void in_set_record_fields(void) { wr32(0x4a95d0, below(5)); }
static void in_bind_keys(void) { rand_bytes(0x46f450, 24); }
/* 444a7c: a high-score table of 8 (name, score) pairs, scores descending. */
static void in_insert_rank(void)
{
    uint32_t s = edge32() | 0x100000u;
    for (uint32_t i = 0; i < 8; i++) {
        wr32(0x46f498 + 8 * i, rnd());
        wr32(0x46f498 + 8 * i + 4, s);
        s -= below(2) ? 0 : below(s / 4 + 1);
    }
    const uint32_t pick = rd32(0x46f498 + 8 * below(8) + 4);
    WR_REG(R_EAX, 4, chance(50) ? pick + below(3) - 1 : edge32());
}
/* The free-slot searches: the used count (one word or the sum of two) is
 * independent of the slots; when it is not the capacity a free slot exists. */
static void free_slot(uint32_t count1, uint32_t count2, uint16_t cap, uint32_t first, uint32_t stride, int width)
{
    const bool full = chance(30);
    slots(first, stride, 1 + below(full ? 8 : 12), width, 70, !full, NULL);
    uint16_t used = full ? cap : (uint16_t)below(cap);
    if (count2) {
        const uint16_t a = (uint16_t)below(used + 1u);
        wr16(count1, a);
        wr16(count2, (uint16_t)(used - a));
    } else {
        wr16(count1, used);
    }
}
static void in_slot200(void) { free_slot(0x4accce, 0x4accd0, 200, 0x520afc, 0x14, 4); }
static void in_slot30(void) { free_slot(0x4acab0, 0x4acab2, 30, 0x524340, 0xd8, 2); }
static void in_slot100(void) { free_slot(0x4b04c8, 0, 100, 0x521a9c, 0x20, 4); }
static void in_slot50(void) { free_slot(0x4abba8, 0, 50, 0x52271c, 0x5e, 4); }
static void in_slot25(void) { free_slot(0x4ab678, 0x4ab67a, 25, 0x523ef4, 0x2c, 2); }
static void in_slot10(void) { free_slot(0x4acb64, 0x4acb66, 10, 0x525c90, 0x1a0, 2); }
static void in_slot28(void) { free_slot(0x4acccc, 0, 0x32, 0x526cd0, 0x28, 2); }
static void in_slot30b(void) { free_slot(0x4b37a8, 0, 0x64, 0x519d88, 0x30, 2); }
static void in_slot14(void) { free_slot(0x4b3b7c, 0, 0xc8, 0x51b048, 0x14, 2); }
static void in_add_entry18a(void) { free_slot(0x497c8e, 0, 0x32, 0x52c3a8, 0x12, 2); }
static void in_add_entry18b(void) { free_slot(0x4984c6, 0, 0x32, 0x52c9e8, 0x12, 2); }
static void in_add_entry14a(void) { free_slot(0x497c90, 0, 0x32, 0x52c72c, 0xe, 2); }
/* 415e97: 10 entries of 48 bytes, the count = the used ones; EDI is looked
 * for in their dword at +0xe. */
static void in_find_entry(void)
{
    const unsigned live = slots(0x519a60, 0x30, 10, 2, chance(20) ? 100 : 50, false, NULL);
    wr16(0x4ba740, (uint16_t)live);
    if (chance(50)) WR_REG(R_EDI, 4, rd32(0x519a60 + 0x30 * below(10) + 0xe));
}
static void in_grow_slot(void) { wr8(0x47004c, (uint8_t)(chance(30) ? 0x20 : 0x19 + below(8))); }
static void in_count_up(void)
{
    wr8(0x47004e, (uint8_t)edge16());
    wr8(0x47004c, (uint8_t)edge16());
}
static void in_next_record(void)
{
    wr8(0x47005a, (uint8_t)below(4));
    wr8(0x47005b, (uint8_t)below(3));
    rand_bytes(0x519a00, 0x36);
}
static void in_timed_entry(void)
{
    wr32(0x46ffe8, SCRATCH);
    wr32(SCRATCH, edge32());
    slots(0x51f440, 0xa, 1 + below(12), 2, 60, true, NULL);
}
static void in_phase_entry(void)
{
    slots(0x51fbac, 0x18, 1 + below(12), 2, 60, true, NULL);
    WR_REG(R_EDI, 4, SCRATCH);
    rand_words(SCRATCH, 4);
    wr16(0x46ff42, edge16());
}
/* The random fills: EBP is an object with a count (1..200) at +0xa. */
static void in_random_fill(void)
{
    WR_REG(R_EBP, 4, SCRATCH);
    rand_words(SCRATCH, 8);
    wr16(SCRATCH + 0xa, (uint16_t)(chance(30) ? 0xc8 : 1 + below(0xc8)));
}
static void in_palette_fade(void)
{
    wr32(0x4be944, chance(50) ? 0x4bf48eu : SCRATCH);
    rand_bytes(SCRATCH, 0x2d0);
    rand_bytes(0x4bf48e, 0x2d0);
    WR_REG(R_EDI, 4, SCRATCH + 0x400);
    rand_bytes(SCRATCH + 0x400, 0x2d0);
    WR_REG(R_ECX, 2, (uint16_t)(2 + below(254)));               /* IDIV by -CX: not 0 or 1 */
}
/* The path followers: a path offset (0 = none) into the arena, a cursor and a
 * speed; the path's words end with 0x8000. */
static void path(uint32_t off_at, uint32_t cursor_at, uint32_t speed_at)
{
    const uint32_t arena = rd32(G_ARENA_BASE);
    wr32(off_at, chance(20) ? 0 : SCRATCH - arena);
    const uint32_t cur = 4 * below(16);
    wr32(cursor_at, cur);
    for (uint32_t i = 0; i < 64; i += 2) wr16(SCRATCH + i, chance(15) ? 0x8000 : edge16());
    wr32(speed_at, chance(30) ? edge32() : below(0x100000));
}
static void in_follow_path(void)
{
    path(0x498494, 0x498498, 0x49849c);
    wr32(0x4984a8, edge32());
}
static void in_follow_path2(void)
{
    path(0x497c34, 0x497c38, 0x497c3c);
    wr32(0x497aec, chance(40) ? 1 : edge32());
    wr32(0x497ae8, SCRATCH + 0x100);
    rand_bytes(SCRATCH + 0x100, 16);
}

/* 437c3a: EDI is an object; a used one (word 0) adds +0x2c to the dword the
 * pointer at 0x46ffe8 names and calls its handler (+0x30), here a one-store
 * setter (Core_Set_0047009e). */
static void in_object_exit(void)
{
    WR_REG(R_EDI, 4, SCRATCH);
    rand_words(SCRATCH, 0x18);
    if (chance(40)) wr16(SCRATCH, 0);
    wr32(SCRATCH + 0x30, 0x42414a);
    wr32(0x46ffe8, SCRATCH + 0x100);
    wr32(SCRATCH + 0x100, edge32());
    wr16(0x496334, edge16());
}
/* The point records of 42aee4 / 42e495: an offset into the arena (0 = none),
 * a cursor and a factor; the values end with 0x80000000. */
static void point_record(uint32_t b, uint32_t k)
{
    const uint32_t data = SCRATCH + 0x200 * k, arena = rd32(G_ARENA_BASE);
    wr32(b, chance(25) ? 0 : data - arena);
    wr32(b + 4, 8 * below(8));
    wr32(b + 8, edge32());
    for (uint32_t i = 0; i < 0x80; i += 4) wr32(data + i, chance(15) ? 0x80000000u : edge32());
}
static void in_step_points(void)
{
    for (uint32_t k = 0; k < 4; k++) point_record(0x497db2u + k * 0xb0, k);
}
static void in_step_points2(void)
{
    static const uint32_t rec[5] = {0x4985fc, 0x498686, 0x4986fe, 0x498776, 0x4987ee};
    for (uint32_t k = 0; k < 5; k++) point_record(rec[k], k);
}

/* ---- the run ---------------------------------------------------------------- */
typedef struct {
    const char *name;
    void (*fn)(void);
    void (*setup)(void);
    unsigned weight;              /* percent of ASTRO_FUZZ_ITER (big copies: fewer) */
} target;

static const target k_targets[] = {
    {"Core_ClearFinishedA", f_41a93d, in_clear_finished_a, 100},
    {"Core_ClearFinishedB", f_41b026, in_clear_finished_b, 100},
    {"Core_StepEntries24", f_419819, in_step_entries_24, 100},
    {"Core_StepEntries14", f_41aa6d, in_step_entries_14, 100},
    {"Core_StepEntriesB", f_419971, in_step_entries_b, 100},
    {"Core_StepEntriesC", f_41abba, in_step_entries_c, 100},
    {"Core_StepDeadlines", f_41bd40, in_step_deadlines, 100},
    {"Core_CopyScreen", f_446c99, in_copy_screen, 5},
    {"Core_SetRecordFields", f_430874, in_set_record_fields, 10},
    {"Core_BindKeysP1b", f_416ba8, in_bind_keys, 10},
    {"Core_BindKeysP1c", f_416baf, in_bind_keys, 10},
    {"Core_BindKeysP1d", f_416bb6, in_bind_keys, 10},
    {"Core_BindKeysP2a", f_416bff, in_bind_keys, 10},
    {"Core_BindKeysP2c", f_416c0d, in_bind_keys, 10},
    {"Core_BindKeysP2d", f_416c14, in_bind_keys, 10},
    {"Core_InsertRank", f_444a7c, in_insert_rank, 100},
    {"Core_FindFreeSlot200", f_41c033, in_slot200, 50},
    {"Core_FindFreeSlot30", f_41cb30, in_slot30, 50},
    {"Core_FindFreeSlot100", f_41c27d, in_slot100, 50},
    {"Core_FindFreeSlot50", f_41c49a, in_slot50, 50},
    {"Core_FindFreeSlot25", f_41c9b2, in_slot25, 50},
    {"Core_FindFreeSlot10", f_41cdb2, in_slot10, 50},
    {"Core_FindFreeSlot28", f_41cf17, in_slot28, 50},
    {"Core_FindFreeSlot30b", f_415fe8, in_slot30b, 50},
    {"Core_FindFreeSlot14", f_416283, in_slot14, 50},
    {"Core_AddListEntry18a", f_429b26, in_add_entry18a, 50},
    {"Core_AddListEntry18b", f_42d48f, in_add_entry18b, 50},
    {"Core_AddListEntry14a", f_429bc4, in_add_entry14a, 50},
    {"Core_FindEntryOrFree", f_415e97, in_find_entry, 100},
    {"Core_GrowSlot1", f_424120, in_grow_slot, 20},
    {"Core_CountUp1", f_4240e1, in_count_up, 50},
    {"Core_NextRecord18", f_424030, in_next_record, 20},
    {"Core_StartTimedEntry", f_423f3f, in_timed_entry, 50},
    {"Core_AddPhaseEntry", f_41b944, in_phase_entry, 50},
    {"Core_FillRandomPairs", f_41cd6f, in_random_fill, 50},
    {"Core_FillRandomFlags", f_41cae3, in_random_fill, 50},
    {"Palette_StartFade", f_4135ba, in_palette_fade, 20},
    {"Core_FollowPath", f_42d318, in_follow_path, 100},
    {"Core_FollowPath2", f_4299b8, in_follow_path2, 100},
    {"Core_RunObjectExit", f_437c3a, in_object_exit, 20},
    {"Core_StepPoints", f_42aee4, in_step_points, 100},
    {"Core_StepPoints2", f_42e495, in_step_points2, 100},
};

static bool listed(const char *list, const char *name)
{
    if (strcmp(list, "all") == 0) return true;
    const size_t n = strlen(name);
    for (const char *p = list; (p = strstr(p, name)) != NULL; p += n)
        if ((p == list || p[-1] == ',') && (p[n] == ',' || p[n] == 0)) return true;
    return false;
}

/* One call as from the core: a return address on the emulated stack. */
static void call(const target *t)
{
    const uint32_t ret = 0xF022F022u, esp0 = VA_STACK_TOP - 0x1000;
    WR_REG(R_ESP, 4, esp0 - 4);
    wr32(esp0 - 4, ret);
    cpu.ret_addr = 0;
    t->fn();
    if (cpu.ret_addr != ret || ESP != esp0) {
        fprintf(stderr, "fuzz: %s returned badly (ret %08x, esp %08x)\n", t->name, cpu.ret_addr, ESP);
        exit(1);
    }
}

int fuzz_run(const char *list)
{
    const char *s = getenv("ASTRO_FUZZ_SEED"), *it = getenv("ASTRO_FUZZ_ITER");
    rng = 0x9e3779b97f4a7c15ull ^ (s ? strtoull(s, NULL, 0) : 1);
    if (!rng) rng = 1;
    const unsigned long iter = it ? strtoul(it, NULL, 0) : 2000;
    unsigned done = 0;
    for (size_t i = 0; i < sizeof k_targets / sizeof k_targets[0]; i++) {
        const target *t = &k_targets[i];
        if (!listed(list, t->name)) continue;
        const unsigned long n = iter * t->weight / 100 ? iter * t->weight / 100 : 1;
        for (unsigned long k = 0; k < n; k++) {
            rand_regs();
            t->setup();
            call(t);
        }
        printf("fuzz: %s: %lu calls\n", t->name, n);
        done++;
    }
    if (!done) {
        fprintf(stderr, "fuzz: no function matches \"%s\"\n", list);
        return 1;
    }
    printf("fuzz: %u functions done\n", done);
    return 0;
}
