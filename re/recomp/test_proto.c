/*
 * Prototype test for the recompiler: runs generated code for
 *   Core_Random     (0x408f8c)
 *   Core_DrawSprite (0x442454, incl. the clipping path 0x4424f0)
 *   Core_Dispatch   (0x408c44, command 0 = report ranges)
 * and compares the results with independent reference implementations.
 *
 * Needs the packaged assets: argv[1] = assets directory.
 */
#include "recomp_rt.h"
#include "gen_funcs.h"

#include <stdio.h>
#include <stdlib.h>

#define DATA_VA     0x46e000u
#define STACK_TOP   0x00300000u
#define FRAME_VA    0x01000000u   /* 320x200 frame buffer */
#define SPRITES_VA  0x01100000u   /* LEVEL1.DAT copy */
#define G_FRAMEBUF  0x55c7f4u     /* g_frameBuffer */

static int failures;

void host_shell_call(uint32_t ilt) { fprintf(stderr, "unexpected shell call %08x\n", ilt); abort(); }

static size_t load(const char *dir, const char *name, uint32_t va)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    size_t n = fread(&mem[va], 1, RT_MEM_SIZE - va, f);
    fclose(f);
    return n;
}

/* Emulate "call fn" from outside: push a return address, run, check RET. */
static void call_core(rt_func fn)
{
    const uint32_t ret = 0xC0DEF00Du;
    uint32_t esp0 = STACK_TOP - 64;
    WR_REG(R_ESP, 4, esp0 - 4);
    MEM_WR(esp0 - 4, 4, ret);
    fn();
    if (cpu.ret_addr != ret) { printf("  FAIL: returned via %08x\n", cpu.ret_addr); failures++; }
    if (ESP != esp0) { printf("  FAIL: ESP %08x, expected %08x\n", ESP, esp0); failures++; }
}

/* ---- Core_Random ------------------------------------------------------------ */
static uint16_t ref_random(void)
{
    uint16_t seed = (uint16_t)MEM_RD(0x46ffa6, 2);
    uint16_t ax = (uint16_t)(seed * 13u);
    ax = (uint16_t)((ax >> 2) | (ax << 14));
    static const uint32_t adds[] = {0x470164, 0x470166, 0x4702bc, 0x4702be, 0x46ffa0, 0x470050, 0x4701a8};
    for (unsigned i = 0; i < sizeof adds / sizeof adds[0]; i++) ax = (uint16_t)(ax + MEM_RD(adds[i], 2));
    return ax;
}

static void test_random(void)
{
    printf("Core_Random: ");
    srand(1234);
    int bad = 0;
    for (int i = 0; i < 10000; i++) {
        static const uint32_t vars[] = {0x46ffa6, 0x470164, 0x470166, 0x4702bc, 0x4702be, 0x46ffa0, 0x470050, 0x4701a8};
        for (unsigned k = 0; k < 8; k++) MEM_WR(vars[k], 2, (uint64_t)(rand() & 0xffff));
        uint16_t want = ref_random();
        WR_REG(R_EAX, 4, 0x12345678);
        call_core(f_408f8c);
        uint16_t got = (uint16_t)RD_REG(R_EAX, 2);
        uint16_t seed = (uint16_t)MEM_RD(0x46ffa6, 2);
        if (got != want || seed != want || (EAX >> 16) != 0x1234) {
            if (bad++ < 5) printf("\n  mismatch: got %04x seed %04x want %04x", got, seed, want);
        }
    }
    printf(bad ? "\n  %d mismatches\n" : "10000 random states OK\n", bad);
    failures += bad;
}

/* ---- Core_DrawSprite ----------------------------------------------------------- */
static uint8_t ref_frame[320 * 200];

/* Returns the sprite's width/height and draws it clipped into ref_frame. */
static void ref_draw(const uint8_t *s, int x0, int y0, int *w, int *h, int draw)
{
    int x = 0, y = 0, maxw = 0;
    for (;;) {
        uint16_t a = (uint16_t)(s[0] | s[1] << 8); s += 2;
        if (a == 0xFFFF) break;
        if (a == 0xFFFE) { y++; x = 0; continue; }
        uint16_t n = (uint16_t)(s[0] | s[1] << 8); s += 2;
        x += a;
        for (int k = 0; k < n; k++, x++) {
            int px = x0 + x, py = y0 + y;
            if (draw && px >= 0 && px < 320 && py >= 0 && py < 200) ref_frame[py * 320 + px] = s[k];
        }
        s += n;
        if (x > maxw) maxw = x;
    }
    *w = maxw; *h = y + 1;
}

static void test_draw_sprite(uint32_t sprite_off, int x, int y)
{
    const uint8_t *spr = &mem[SPRITES_VA + sprite_off];
    int w, h;
    ref_draw(spr, 0, 0, &w, &h, 0);
    memset(ref_frame, 0, sizeof ref_frame);
    ref_draw(spr, x, y, &w, &h, 1);
    memset(&mem[FRAME_VA], 0, 320 * 200);
    MEM_WR(G_FRAMEBUF, 4, FRAME_VA);

    WR_REG(R_EDX, 4, (uint32_t)(uint16_t)x);   /* dx = x */
    WR_REG(R_EBX, 4, (uint32_t)(uint16_t)y);   /* bx = y */
    WR_REG(R_EBP, 4, (uint32_t)w);             /* bp = width */
    WR_REG(R_ECX, 4, (uint32_t)h);             /* cx = height */
    WR_REG(R_ESI, 4, SPRITES_VA + sprite_off);
    WR_REG(R_DF, 1, 0);
    call_core(f_442454);

    int diff = 0, first = -1;
    for (int i = 0; i < 320 * 200; i++)
        if (mem[FRAME_VA + i] != ref_frame[i]) { if (first < 0) first = i; diff++; }
    printf("Core_DrawSprite %3dx%-3d at (%4d,%4d): %s", w, h, x, y, diff ? "" : "OK\n");
    if (diff) {
        printf("%d pixels differ, first at (%d,%d): got %02x want %02x\n", diff, first % 320, first / 320,
               mem[FRAME_VA + first], ref_frame[first]);
        failures++;
    }
}

/* ---- Core_Dispatch, command 0 ------------------------------------------------- */
static void test_dispatch(void)
{
    printf("Core_Dispatch cmd 0: ");
    MEM_WR(0x556ae4, 4, 0);                       /* g_coreCommand = 0 */
    for (uint32_t a = 0x556ad4; a < 0x556ae4; a += 4) MEM_WR(a, 4, 0);
    WR_REG(R_EBX, 4, 0xB0B0B0B0); WR_REG(R_ESI, 4, 0x51515151);
    WR_REG(R_ZF, 1, 1); WR_REG(R_CF, 1, 1);
    call_core(f_408c44);
    uint32_t v[4] = {(uint32_t)MEM_RD(0x556ad4, 4), (uint32_t)MEM_RD(0x556ad8, 4),
                     (uint32_t)MEM_RD(0x556adc, 4), (uint32_t)MEM_RD(0x556ae0, 4)};
    int ok = v[0] == 0x408c44 && v[1] == 0x3f163 && v[2] == 0x46ff3c && v[3] == 0xbd274
             && EBX == 0xB0B0B0B0 && ESI == 0x51515151 && RD_REG(R_ZF, 1) == 1 && RD_REG(R_CF, 1) == 1;
    printf("%s ranges %08x+%x %08x+%x, registers and flags %s\n", ok ? "OK," : "FAIL,", v[0], v[1], v[2], v[3],
           ok ? "preserved" : "NOT preserved");
    if (!ok) failures++;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "assets";
    setvbuf(stdout, NULL, _IONBF, 0);
    mem = calloc(RT_MEM_SIZE, 1);
    if (!mem) return 2;
    load(dir, "data/ASTRO_DATA.BIN", DATA_VA);
    load(dir, "data/LEVEL1.DAT", SPRITES_VA);

    /* the dispatcher reads its command table from the code section */
    load(dir, "data/ASTRO_TEXT.BIN", 0x401000);

    test_random();
    test_draw_sprite(0x0, 100, 80);          /* fully visible: fast path       */
    test_draw_sprite(0x0, -5, 60);           /* clipped left                   */
    test_draw_sprite(0x0, 310, 190);         /* clipped right and bottom       */
    test_draw_sprite(0x0, 50, -7);           /* clipped top                    */
    test_draw_sprite(0x35dbad, 120, 40);     /* large asteroid, fully visible  */
    test_draw_sprite(0x35dbad, 250, -30);    /* large asteroid, clipped        */
    test_draw_sprite(0x35dbad, -400, 50);    /* completely off screen          */
    test_dispatch();

    printf(failures ? "\n%d FAILURES\n" : "\nall tests passed\n", failures);
    return failures != 0;
}
