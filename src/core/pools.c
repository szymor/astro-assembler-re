/*
 * Small leaf routines on fixed-size object tables: "find the first free
 * slot" scans and the wrap-around of a position. They are written as
 * translations of the original register use: the callers continue with the
 * slot address in EBP (or ESI) and test the flags (the carry flag says
 * "table full" in two of them).
 */
#include "core.h"

/*
 * Core_AddEntry8 (0x4137f4): a table of 25 entries of 8 bytes (0x519c40 on,
 * first dword of an entry zero = free) with its count at 0x4ba908. Unless the
 * count is 25, the first free entry receives EBX and ECX and the count goes up.
 * EBP is preserved; the flags are those of the count's INC (or of the compare
 * with 25 when the table is full).
 */
HAND_FN(4137f4)
{
    const uint32_t count_va = 0x4ba908u;
    cmp16(rd16(count_va), 0x19);
    if (!flag(R_ZF)) {
        uint32_t e = 0x519c38u;
        do e += 8; while (rd32(e) != 0);
        cmp32(0, 0);                                   /* the last flag-setting scan step */
        wr32(e, EBX);
        wr32(e + 4, ECX);
        wr16(count_va, inc16(rd16(count_va)));
    }
    x86_ret();
}

/*
 * The shared shape of the "find a free slot" routines: `used` is the number of
 * used entries (read into BP); if it equals `capacity` the table is full (CF
 * set, EBP's low half = used). Otherwise EBP = the address of the first entry
 * whose first dword/word (`width` 4 or 2) is zero, scanning from base+stride
 * on, and CF = 0. The flags are those of the last compare, CF aside.
 */
static void find_free_slot(uint16_t used, uint16_t capacity, uint32_t base, uint32_t stride, int width)
{
    cmp16(used, capacity);
    if (flag(R_ZF)) {
        WR_REG(R_EBP, 2, used);
        set_flag(R_CF, true);
    } else {
        uint32_t e = base;
        do e += stride; while ((width == 4 ? rd32(e) : rd16(e)) != 0);
        if (width == 4) cmp32(0, 0); else cmp16(0, 0);
        WR_REG(R_EBP, 4, e);
        set_flag(R_CF, false);
    }
    x86_ret();
}

/* 0x41c033: 200 entries of 0x14 bytes at 0x520afc (first dword), used = the
 * sum of the words at 0x4accce and 0x4accd0. */
HAND_FN(41c033) { find_free_slot((uint16_t)(rd16(0x4accceu) + rd16(0x4accd0u)), 200, 0x520ae8u, 0x14, 4); }
/* 0x41cb30: 30 entries of 0xd8 bytes at 0x524340 (first word), used = the
 * sum of the words at 0x4acab0 and 0x4acab2. */
HAND_FN(41cb30) { find_free_slot((uint16_t)(rd16(0x4acab0u) + rd16(0x4acab2u)), 30, 0x524268u, 0xd8, 2); }
/* 0x41c27d: 100 entries of 0x20 bytes at 0x521a9c (first dword), used = the
 * word at 0x4b04c8. */
HAND_FN(41c27d) { find_free_slot(rd16(0x4b04c8u), 100, 0x521a7cu, 0x20, 4); }
/* 0x41c49a: 50 entries of 0x5e bytes at 0x522728 (first dword), used = the
 * word at 0x4abba8. */
HAND_FN(41c49a) { find_free_slot(rd16(0x4abba8u), 50, 0x5226beu, 0x5e, 4); }
/* 0x41c9b2: 25 entries of 0x2c bytes at 0x523ef4 (first word), used = the
 * sum of the words at 0x4ab678 and 0x4ab67a. */
HAND_FN(41c9b2) { find_free_slot((uint16_t)(rd16(0x4ab678u) + rd16(0x4ab67au)), 25, 0x523ec8u, 0x2c, 2); }
/* 0x41cdb2: 10 entries of 0x1a0 bytes at 0x525c90 (first word), used = the
 * sum of the words at 0x4acb64 and 0x4acb66. */
HAND_FN(41cdb2) { find_free_slot((uint16_t)(rd16(0x4acb64u) + rd16(0x4acb66u)), 10, 0x525af0u, 0x1a0, 2); }

/*
 * Core_WrapPosition (0x42be97): ESI += EAX, then wrapped into [0, 0x1680000)
 * (0x168 = 360 in 16.16 fixed point, presumably an angle in degrees). The
 * flags are those of the last compare with 0x1680000.
 */
HAND_FN(42be97)
{
    uint32_t esi = add32(ESI, EAX);
    while ((int32_t)esi < 0) esi += 0x1680000u;
    for (;;) {
        cmp32(esi, 0x1680000u);
        if (cond_l()) break;
        esi -= 0x1680000u;
    }
    WR_REG(R_ESI, 4, esi);
    x86_ret();
}

/*
 * Core_WrapIndex (0x41cf6b): ESI += EAX, wrapped once into [0, 0xb40) (a
 * single step each way, unlike Core_WrapPosition). Flags of the last
 * arithmetic instruction.
 */
HAND_FN(41cf6b)
{
    uint32_t esi = add32(ESI, EAX);
    if (esi & 0x80000000u) esi += 0xb40u;               /* flags overwritten by the compare */
    cmp32(esi, 0xb40u);
    if (!cond_l()) esi = sub32(esi, 0xb40u);
    WR_REG(R_ESI, 4, esi);
    x86_ret();
}

/*
 * Core_FineToIndex (0x42beb7): ESI = (ESI + 0x4000) >> 15 << 2, reduced by 0xb40
 * if that is not below it (the fixed-point position in 1/32768 steps, rounded,
 * as a multiple of 4).
 */
HAND_FN(42beb7)
{
    uint32_t esi = ((ESI + 0x4000u) >> 15) << 2;
    cmp32(esi, 0xb40u);
    if (!flag(R_CF)) esi = sub32(esi, 0xb40u);
    WR_REG(R_ESI, 4, esi);
    x86_ret();
}
