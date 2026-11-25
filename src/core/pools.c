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
 * Core_FindFreeSlot200 (0x41c033): two counters at 0x4accce and 0x4accd0
 * add up to the number of used entries of a 200-entry table of 0x14 bytes
 * (at 0x520afc on, first dword zero = free). If all are used, CF = 1 and
 * EBP's low half is 200. Otherwise EBP is the address of the first free entry
 * and CF = 0.
 */
HAND_FN(41c033)
{
    uint16_t bp = rd16(0x4accceu);
    bp = add16(bp, rd16(0x4accd0u));
    cmp16(bp, 0xc8);
    if (flag(R_ZF)) {
        WR_REG(R_EBP, 2, bp);
        set_flag(R_CF, true);
    } else {
        uint32_t e = 0x520ae8u;
        do e += 0x14; while (rd32(e) != 0);
        cmp32(0, 0);
        WR_REG(R_EBP, 4, e);
        set_flag(R_CF, false);
    }
    x86_ret();
}

/*
 * Core_FindFreeSlot30 (0x41cb30): like Core_FindFreeSlot200 for 30 entries of
 * 0xd8 bytes at 0x524340 on (first word zero = free), the counters being the
 * words at 0x4acab0 and 0x4acab2.
 */
HAND_FN(41cb30)
{
    uint16_t bp = rd16(0x4acab0u);
    bp = add16(bp, rd16(0x4acab2u));
    cmp16(bp, 0x1e);
    if (flag(R_ZF)) {
        WR_REG(R_EBP, 2, bp);
        set_flag(R_CF, true);
    } else {
        uint32_t e = 0x524268u;
        do e += 0xd8; while (rd16(e) != 0);
        cmp16(0, 0);
        WR_REG(R_EBP, 4, e);
        set_flag(R_CF, false);
    }
    x86_ret();
}

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
