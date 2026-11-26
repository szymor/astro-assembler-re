/*
 * A table search that shifts the table down, a sign-folded lookup with a
 * 64-bit division, and a countdown over eight timer pairs. Each step keeps the
 * flags of the instruction it is (see core.h).
 */
#include "core.h"

/* INC r32: OF, SF, ZF, PF; CF is left alone. */
static uint32_t inc32(uint32_t a)
{
    const uint32_t r = a + 1;
    set_flag(R_OF, a == 0x7fffffffu);
    flags_szp32(r);
    return r;
}

/*
 * 444a7c: the first of eight 8-byte entries (at 0x46f498) whose second dword
 * is below EAX (unsigned): the entries from there on move down by one
 * (backward copy). Carry set: found (EDX = its index); clear: none.
 */
HAND_FN(444a7c)
{
    uint32_t ebp = 0x46f498, edx = 0, ecx = 8;
    const uint32_t eax = EAX;
    for (;;) {
        cmp32(eax, rd32(ebp + 4));
        if (flag(R_CF) || flag(R_ZF)) {                      /* JBE: not here, next */
            ebp = add32(ebp, 8);
            edx = inc32(edx);
            ecx = dec32(ecx);
            if (!flag(R_ZF)) continue;
            WR_REG(R_EBP, 4, ebp); WR_REG(R_EDX, 4, edx); WR_REG(R_ECX, 4, ecx);
            set_flag(R_CF, false);                           /* CLC */
            x86_ret();
            return;
        }
        break;
    }
    ecx = dec32(ecx);
    if (!(flag(R_ZF) || cond_l())) {                         /* not JLE: move the tail down */
        ecx = add32(ecx, ecx);
        uint32_t esi = 0x46f4cc, edi = 0x46f4d4;
        for (uint32_t i = 0; i < ecx; i++, esi -= 4, edi -= 4) wr32(edi, rd32(esi));
        WR_REG(R_ESI, 4, esi); WR_REG(R_EDI, 4, edi);
        ecx = 0;
        set_flag(0x20a, false);                              /* STD ... CLD */
    }
    WR_REG(R_EBP, 4, ebp); WR_REG(R_EDX, 4, edx); WR_REG(R_ECX, 4, ecx);
    set_flag(R_CF, true);                                    /* STC */
    x86_ret();
}

/*
 * 41b740: bits 0-2 of the table row (ECX) from the signs of 0x46ffc8 and of
 * the quotient (0x46ffcc:0 / EBX, rounded and scaled by 256); BX = a distance
 * looked up with the quotient's magnitude, taken from the row.
 */
HAND_FN(41b740)
{
    uint32_t ecx = 0;
    if ((int32_t)rd32(0x46ffc8) < 0) ecx |= 2;
    const uint32_t edx = rd32(0x46ffcc);
    const int64_t dividend = (int64_t)((uint64_t)edx << 32);
    const int32_t divisor = (int32_t)EBX;
    const int32_t quot = (int32_t)(dividend / divisor);
    const int32_t rem = (int32_t)(dividend % divisor);
    int32_t esi = (int32_t)(0x80u + (uint32_t)quot) >> 8;
    if (esi < 0) { ecx |= 4; esi = -esi; }
    const uint32_t eax = rd32(0x556ac8);
    WR_REG(R_EAX, 4, (eax & 0xffff0000u) | rd8(eax + (uint32_t)esi + 0x1f4e73u));
    ecx = add32(ecx, 0x4b0bbe);
    uint16_t bx = sub16(rd16(ecx + 8), (uint16_t)rd8(eax + (uint32_t)esi + 0x1f4e73u));
    if (flag(R_SF)) bx = neg16(bx);
    bx = add16(bx, rd16(ecx));
    WR_REG(R_EBX, 2, bx);
    WR_REG(R_ECX, 4, ecx);
    WR_REG(R_EDX, 4, (uint32_t)rem);
    WR_REG(R_ESI, 4, (uint32_t)esi);
    x86_ret();
}

/* 414a91: eight pairs (counter word, flag word) at ESI: a nonzero counter counts down and the flag toggles whenever it reaches a multiple of 8. */
HAND_FN(414a91)
{
    uint32_t esi = ESI;
    uint16_t cx = 8, ax = (uint16_t)EAX;
    do {
        cmp16(rd16(esi), 0);
        if (!flag(R_ZF)) {
            const uint16_t c = (uint16_t)(rd16(esi) - 1);
            wr16(esi, c);
            ax = (uint16_t)((c % 8) << 8 | c / 8);
            if ((ax >> 8) == 0) wr16(esi + 2, rd16(esi + 2) ^ 1);
        }
        esi = add32(esi, 4);
        cx = dec16(cx);
    } while (!flag(R_ZF));
    WR_REG(R_EAX, 2, ax);
    WR_REG(R_EBX, 1, 8);
    WR_REG(R_ECX, 2, cx);
    WR_REG(R_EDX, 2, 0);
    WR_REG(R_EDI, 2, 1);
    WR_REG(R_ESI, 4, esi);
    x86_ret();
}
