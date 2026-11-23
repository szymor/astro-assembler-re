/*
 * Self-modifying code test: the affine texture mapper at 0x442ed6..0x442f90.
 * Before its loops it patches the shld count, the texture base and the step
 * values into its own instructions. The generated code must read those fields
 * from memory. Compared against a hand transcription of the routine.
 *
 * argv[1] = assets directory (needs data/ASTRO_DATA.BIN, data/ASTRO_TEXT.BIN)
 */
#include "recomp_rt.h"
#include "gen_funcs.h"

#include <stdio.h>
#include <stdlib.h>

#define FRAME_VA 0x01000000u
#define TEX_VA   0x01200000u
#define STACK_TOP 0x00300000u

void host_shell_call(uint32_t ilt) { fprintf(stderr, "unexpected shell call %08x\n", ilt); abort(); }

static void load(const char *dir, const char *name, uint32_t va)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    if (fread(&mem[va], 1, RT_MEM_SIZE - va, f) == 0) { fprintf(stderr, "empty %s\n", path); exit(2); }
    fclose(f);
}

static uint32_t rd32(uint32_t a) { return (uint32_t)MEM_RD(a, 4); }

/* Hand transcription of 0x442ed6..0x442f90 (registers ebp/ebx are inputs). */
static void reference(uint8_t *frame_out, uint32_t in_ebp, uint32_t in_ebx)
{
    uint32_t edi = rd32(0x4b3bb8) * 320u + rd32(0x4b3bb4) + rd32(0x55c7f4);
    uint32_t w = rd32(0x4b3be4);
    unsigned n = 0; while (!((w >> n) & 1)) n++;          /* bsf */
    uint32_t tex = rd32(0x4b3be8);
    uint32_t add_edx = in_ebp, sub_ecx = in_ebx;
    uint32_t row_edi = 320u - rd32(0x4b3bd4), row_edx = rd32(0x4b3bfc), row_ecx = rd32(0x4b3bf8);
    uint32_t edx = rd32(0x4b3bf0), ecx = rd32(0x4b3bf4), esi = 0;
    uint32_t rows = rd32(0x4b3bd8);
    do {
        uint32_t sv_ecx = ecx, sv_edx = edx;
        uint32_t cnt = rd32(0x4b3bd4);
        do {
            esi = (esi & 0xffff0000u) | (ecx & 0xffffu);
            if (n) esi = (esi << n) | (edx >> (32 - n));   /* shld esi,edx,n */
            uint8_t al = (uint8_t)MEM_RD(esi + tex, 1);
            if (al) {
                if (edi - FRAME_VA >= 320u * 200u) { printf("  reference writes outside the frame (%08x)\n", edi); return; }
                frame_out[edi - FRAME_VA] = al;
            }
            edx += add_edx;
            edi++;
            uint32_t borrow = ecx < sub_ecx;               /* sub ecx,imm ; sbb ecx,0 */
            ecx -= sub_ecx;
            ecx -= borrow;
        } while (--cnt);
        edx = sv_edx; ecx = sv_ecx;
        edi += row_edi;
        edx += row_edx;
        uint32_t carry = (uint32_t)(ecx + row_ecx) < ecx;  /* add ecx,imm ; adc ecx,0 */
        ecx += row_ecx;
        ecx += carry;
    } while (--rows);
}

static int run_case(int x, int y, int width, int rows, uint32_t texw, uint32_t u0, uint32_t v0,
                    uint32_t du, uint32_t dv, uint32_t rdu, uint32_t rdv)
{
    MEM_WR(0x4b3bb4, 4, (uint32_t)x);   MEM_WR(0x4b3bb8, 4, (uint32_t)y);
    MEM_WR(0x4b3be4, 4, texw);          MEM_WR(0x4b3be8, 4, TEX_VA);
    MEM_WR(0x4b3bd4, 4, (uint32_t)width); MEM_WR(0x4b3bd8, 4, (uint32_t)rows);
    MEM_WR(0x4b3bf0, 4, u0);            MEM_WR(0x4b3bf4, 4, v0);
    MEM_WR(0x4b3bf8, 4, rdv);           MEM_WR(0x4b3bfc, 4, rdu);
    MEM_WR(0x55c7f4, 4, FRAME_VA);

    static uint8_t ref[320 * 200];
    memset(&mem[FRAME_VA], 0, 320 * 200);
    memcpy(ref, &mem[FRAME_VA], sizeof ref);
    reference(ref, du, dv);

    /* scribble over the patched fields so stale values cannot pass the test */
    static const uint32_t fields[] = {0x442f5b, 0x442f65, 0x442f6c, 0x442f7a, 0x442f80, 0x442f86};
    for (unsigned i = 0; i < 6; i++) MEM_WR(fields[i], 4, 0xdeadbeef);
    MEM_WR(0x442f58, 1, 0x1f);

    WR_REG(R_EBP, 4, du); WR_REG(R_EBX, 4, dv);
    uint32_t esp0 = STACK_TOP - 64;
    WR_REG(R_ESP, 4, esp0 - 4); MEM_WR(esp0 - 4, 4, 0xC0DEF00Du);
    f_442ed6();
    int ok = cpu.ret_addr == 0xC0DEF00Du && ESP == esp0;
    int diff = 0, drawn = 0;
    for (int i = 0; i < 320 * 200; i++) { diff += mem[FRAME_VA + i] != ref[i]; drawn += ref[i] != 0; }
    printf("texture mapper %3dx%-3d at (%3d,%3d) tex %3u wide: %s (%d pixels drawn, %d differ)\n",
           width, rows, x, y, texw, ok && !diff ? "OK" : "FAIL", drawn, diff);
    return ok && !diff;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "assets";
    setvbuf(stdout, NULL, _IONBF, 0);
    mem = calloc(RT_MEM_SIZE, 1);
    if (!mem) return 2;
    load(dir, "data/ASTRO_DATA.BIN", 0x46e000);
    load(dir, "data/ASTRO_TEXT.BIN", 0x401000);
    for (uint32_t i = 0; i < 0x40000; i++) mem[TEX_VA + i] = (uint8_t)((i * 7 + (i >> 6) * 13) % 251 + 1) * ((i % 9) != 0);

    int ok = 1;
    /* u in edx: the top N bits are the texel column (wraps freely).
       v in ecx, "swapped" fixed point: low 16 bits = integer row, high 16 bits
       = fraction; sub/sbb and add/adc carry between them. The routine relies on
       v staying below 2^(16-N), otherwise stale high bits pile up in esi. */
    ok &= run_case(40, 30, 64, 48, 64, 0, 40, 0x04000000, 0x80000000, 0, 0x00000001);
    ok &= run_case(100, 60, 100, 80, 64, 0x10000000, 500, 0x03000000, 0x40000000, 0x00800000, 0xc0000000);
    ok &= run_case(0, 0, 320, 10, 128, 0, 300, 0x01000000, 0x40000000, 0x02000000, 0x60000000);
    ok &= run_case(150, 120, 50, 50, 32, 0xf0000000, 1000, 0xff000000, 0xe0000001, 0x05000000, 0x10000002);
    printf(ok ? "\nall SMC tests passed\n" : "\nSMC TEST FAILED\n");
    return !ok;
}
