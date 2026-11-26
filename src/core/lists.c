/*
 * Per-frame steps over entry lists, and a few table set-up routines. Each step
 * keeps the flags of the instruction it is (see core.h); the loops visit the
 * entries of a list at a fixed stride and stop after as many live entries
 * (word 0 non-zero) as the list's counter says.
 */
#include "core.h"

static uint32_t inc32(uint32_t a)
{
    const uint32_t r = a + 1;
    set_flag(R_OF, a == 0x7fffffffu);
    flags_szp32(r);
    return r;
}

/* The start of an entry loop: CX = the counter (flags of OR CX, counter); false
 * if it is 0 (the routine returns at once). */
static bool loop_count(uint32_t counter_at, uint16_t *cx)
{
    *cx = rd16(counter_at);
    test16(*cx);
    WR_REG(R_ECX, 2, *cx);
    return *cx != 0;
}

/* 41bd40: live entries (stride 0x1e, from 51fd6e) whose deadline at +1a is not
 * above the limit of their kind (the record at +16 of the table at 4b16b8) get a
 * new deadline (limit + step) and the state 1. */
HAND_FN(41bd40)
{
    uint16_t cx;
    if (loop_count(0x4b16b6, &cx)) {
        uint32_t edi = 0x51fd6e;
        for (;;) {
            edi = add32(edi, 0x1e);
            cmp16(rd16(edi), 0);
            if (flag(R_ZF)) continue;
            const uint32_t esi = rd32(rd16(edi + 0x16) + 0x4b16b8);
            uint32_t eax = rd32(esi + 0x18);
            WR_REG(R_ESI, 4, esi);
            WR_REG(R_EAX, 4, eax);
            cmp32(rd32(edi + 0x1a), eax);
            if (!(cond_a())) {
                eax = add32(eax, rd32(esi + 0x1c));
                WR_REG(R_EAX, 4, eax);
                wr32(edi + 0x1a, eax);
                wr16(edi, 1);
            }
            cx = dec16(cx);
            if (flag(R_ZF)) break;
        }
        WR_REG(R_ECX, 2, cx);
        WR_REG(R_EDI, 4, edi);
    }
    x86_ret();
}

/* 419446: live entries (stride 0xc, from 51db34): +4 falls by 4 (not below 0), the
 * life at +0 by 10; at 0 or below the entry is freed. */
HAND_FN(419446)
{
    uint16_t cx;
    if (loop_count(0x4b08e4, &cx)) {
        uint32_t edi = 0x51db34;
        for (;;) {
            edi = add32(edi, 0xc);
            cmp16(rd16(edi), 0);
            if (flag(R_ZF)) continue;
            cmp32(rd32(edi + 4), 0);
            if (!flag(R_ZF)) wr32(edi + 4, sub32(rd32(edi + 4), 4));
            const uint16_t life = sub16(rd16(edi), 0xa);
            wr16(edi, life);
            if (!(!flag(R_ZF) && cond_ge())) {
                wr16(edi, 0);
                wr16(0x4b08e4, dec16(rd16(0x4b08e4)));
            }
            cx = dec16(cx);
            if (flag(R_ZF)) break;
        }
        WR_REG(R_ECX, 2, cx);
        WR_REG(R_EDI, 4, edi);
    }
    x86_ret();
}

/* 419971: live entries (stride 0xc, from 51e3c4): life +0 falls by 12; at 0 the
 * entry is freed and so ends the loop if it was the last; below 0 it is cleared
 * too; +4 grows by 4 for the others. */
HAND_FN(419971)
{
    uint16_t cx;
    if (loop_count(0x4b0aa0, &cx)) {
        uint32_t edi = 0x51e3c4;
        WR_REG(R_EAX, 2, 0xc);
        WR_REG(R_EDX, 2, 0);
        for (;;) {
            edi = add32(edi, 0xc);
            cmp16(rd16(edi), 0);
            if (flag(R_ZF)) continue;
            wr16(edi, sub16(rd16(edi), 0xc));
            bool freed = flag(R_ZF);
            if (!freed && !flag(R_SF)) goto next;          /* still alive */
            if (!freed) wr16(edi, 0);
            wr16(0x4b0aa0, dec16(rd16(0x4b0aa0)));
            if (flag(R_ZF)) break;
        next:
            wr32(edi + 4, add32(rd32(edi + 4), 4));
            cx = dec16(cx);
            if (flag(R_ZF)) break;
        }
        WR_REG(R_ECX, 2, cx);
        WR_REG(R_EDI, 4, edi);
    }
    x86_ret();
}

/* 41abba: live entries (stride 0x24, from 51ef10): +4 falls by 10; below -132 the
 * entry is freed, and the loop ends when no live entries are left. */
HAND_FN(41abba)
{
    uint16_t cx;
    if (loop_count(0x4b0ca8, &cx)) {
        uint32_t edi = 0x51ef10;
        WR_REG(R_EAX, 2, 0xa);
        WR_REG(R_EDX, 2, 0);
        for (;;) {
            edi = add32(edi, 0x24);
            cmp16(rd16(edi), 0);
            if (flag(R_ZF)) continue;
            wr16(edi + 4, sub16(rd16(edi + 4), 0xa));
            cmp16(rd16(edi + 4), 0xff7c);
            if (!cond_ge()) {
                wr16(edi, 0);
                wr16(0x4b0ca8, dec16(rd16(0x4b0ca8)));
                if (flag(R_ZF)) break;
            }
            cx = dec16(cx);
            if (flag(R_ZF)) break;
        }
        WR_REG(R_ECX, 2, cx);
        WR_REG(R_EDI, 4, edi);
    }
    x86_ret();
}

/* 446bc6: the first free entry (stride 0x14, from 517c5c) of a list is filled
 * with the state 1 and four values from 517ec8..517ed4; its counter grows. */
HAND_FN(446bc6)
{
    uint32_t edi = 0x517c5c;
    do {
        edi = add32(edi, 0x14);
        cmp32(rd32(edi), 0);
    } while (!flag(R_ZF));
    wr32(edi, 1);
    wr32(edi + 4, rd32(0x517ec8));
    wr32(edi + 8, rd32(0x517ecc));
    wr32(edi + 0xc, rd32(0x517ed0));
    wr32(edi + 0x10, rd32(0x517ed4));
    wr32(0x517c6c, inc32(rd32(0x517c6c)));
    x86_ret();
}

/*
 * 41b944: a new entry (stride 0x18, from 51fb94) is added to a list: kind DX, a
 * position from ECX/EAX (in 17.15 fixed point), a phase from the table at EDI
 * and BX wrapped into 0..0xb3f. EDI ends 4 further.
 */
HAND_FN(41b944)
{
    const uint32_t edi0 = EDI;
    uint32_t eax = EAX, ecx = ECX;
    wr16(0x4b135a, inc16(rd16(0x4b135a)));
    uint32_t esi = 0x51fb94;
    do {
        esi = add32(esi, 0x18);
        cmp16(rd16(esi), 0);
    } while (!flag(R_ZF));
    WR_REG(R_ESI, 4, esi);
    wr16(esi + 0x16, (uint16_t)EDX);
    wr16(esi, 2);
    ecx <<= 15;
    eax <<= 15;
    wr32(esi + 2, ecx);
    wr32(esi + 6, eax);
    uint16_t dx = rd16(0x46ff42);
    wr16(esi + 0x14, dx);
    uint16_t ax = rd16(edi0);
    uint16_t cx = rd16(edi0 + 2);
    wr16(esi + 0xe, ax);
    dx = (uint16_t)((int16_t)dx * (int16_t)cx);
    wr16(esi + 0xc, dx);
    ax = (uint16_t)((int16_t)ax * (int16_t)cx);
    ax = (uint16_t)((int16_t)ax >> 1);
    uint16_t bx = sub16((uint16_t)EBX, ax);
    if (flag(R_SF)) bx = add16(bx, 0xb40);
    cmp16(bx, 0xb40);
    if (!cond_l()) bx = sub16(bx, 0xb40);
    wr16(esi + 0xa, bx);
    const uint32_t edi = add32(edi0, 4);
    wr32(esi + 0x10, edi);
    set_lo16(&eax, ax); set_lo16(&ecx, cx);
    WR_REG(R_EAX, 4, eax); WR_REG(R_ECX, 4, ecx);
    WR_REG(R_EBX, 2, bx);
    WR_REG(R_EDX, 2, dx);
    WR_REG(R_EDI, 4, edi);
    x86_ret();
}

/* 42ac52: the stepper of the shot table: every 4th tick of 49810c the next
 * value is taken from the table at 498110 + 498114 (the cursor falls by 4;
 * below 0 the tick restarts); then five timers count down by 4 to 0. */
HAND_FN(42ac52)
{
    cmp16(rd16(0x49810c), 0);
    if (!flag(R_ZF)) {
        uint16_t ax = rd16(0x49810c);
        set_ax(ax);
        wr16(0x49810c, inc16(rd16(0x49810c)));
        WR_REG(R_EBX, 2, 4);
        WR_REG(R_EAX, 2, ax / 4);
        WR_REG(R_EDX, 2, ax % 4);
        test16(ax % 4);
        if (flag(R_ZF)) {
            const uint32_t esi = add32(rd32(0x498110), rd32(0x498114));
            WR_REG(R_ESI, 4, esi);
            const uint32_t v = rd32(esi);
            WR_REG(R_EAX, 4, v);
            wr32(0x498108, v);
            wr32(0x498110, sub32(rd32(0x498110), 4));
            if (flag(R_SF)) wr16(0x49810c, 0);
        }
    }
    static const uint32_t timer[5] = {0x49813c, 0x497d60, 0x497e10, 0x497ec0, 0x497f70};
    for (int i = 0; i < 5; i++) {
        cmp32(rd32(timer[i]), 0);
        if (!flag(R_ZF)) wr32(timer[i], sub32(rd32(timer[i]), 4));
    }
    x86_ret();
}

/* 424030: the next 18-byte record of the table at 519a00 is copied to 519a38;
 * the index at 47005b counts 0..2 and then moves the stage at 47005a on. */
HAND_FN(424030)
{
    cmp8(rd8(0x47005a), 3);
    if (!flag(R_ZF)) {
        uint8_t bl = rd8(0x47005b);
        WR_REG(R_EBX, 1, bl);
        const uint32_t eax = 0x12u * bl;
        WR_REG(R_EAX, 4, eax);
        const uint32_t esi = 0x519a00 + eax;
        copy_forward(0x519a38, esi, 0x12);
        WR_REG(R_ESI, 4, esi + 0x12);
        WR_REG(R_EDI, 4, 0x519a38 + 0x12);
        WR_REG(R_ECX, 4, 0);
        bl = inc8(bl);
        cmp8(bl, 3);
        if (flag(R_ZF)) {
            wr8(0x47005a, inc8(rd8(0x47005a)));
            bl = 0;
            set_flag(R_CF, false); set_flag(R_OF, false); set_flag(R_SF, false);
            set_flag(R_ZF, true); set_flag(0x202, true);          /* XOR BL, BL */
        }
        WR_REG(R_EBX, 1, bl);
        wr8(0x47005b, bl);
    }
    x86_ret();
}

/* 415b51: eight rows of three dwords at EDI are masked (AND, OR) with six dwords
 * per row from ESI; EDI moves down 0x140 bytes per row. */
HAND_FN(415b51)
{
    uint32_t esi = ESI, edi = EDI, eax = 0;
    uint16_t bx = 8;
    do {
        for (int i = 0; i < 3; i++) {
            eax = rd32(esi); esi += 4;
            wr32(edi + 4 * i, rd32(edi + 4 * i) & eax);
            eax = rd32(esi); esi += 4;
            wr32(edi + 4 * i, rd32(edi + 4 * i) | eax);
            if (i == 2) { set_flag(R_CF, false); set_flag(R_OF, false); }
        }
        edi = add32(edi, 0x140);
        bx = dec16(bx);
    } while (!flag(R_ZF));
    WR_REG(R_EAX, 4, eax);
    WR_REG(R_EBX, 2, bx);
    WR_REG(R_ESI, 4, esi);
    WR_REG(R_EDI, 4, edi);
    x86_ret();
}

/* 41ef8b: a 16 by 20 block of 20-byte pieces is copied from the arena (rows
 * picked by the words at EBP) to the screen at EDI, 0x140 bytes per row. */
HAND_FN(41ef8b)
{
    uint32_t edi = EDI, ebp = EBP, esi = 0, eax = 0, edx = 0x10, ebx = 0;
    const uint32_t keep = edi;
    do {
        eax = (uint32_t)rd16(ebp) * 0x190;
        esi = rd32(0x556ac8);
        ebp += 2;
        esi += rd32(0x4b1fc8);
        ebx = 0x14;
        esi += eax;
        do {
            copy_forward(edi, esi, 20);
            esi += 20;
            edi += 20;
            edi = add32(edi, 0x12c);
            ebx = dec32(ebx);
        } while (!flag(R_ZF));
        edi = sub32(edi, 0x18ec);
        edx = dec32(edx);
    } while (!flag(R_ZF));
    (void)keep;
    WR_REG(R_EAX, 4, eax); WR_REG(R_EBX, 4, 0); WR_REG(R_ECX, 4, 0); WR_REG(R_EDX, 4, 0);
    WR_REG(R_ESI, 4, esi);
    WR_REG(R_EBP, 4, ebp);
    x86_ret();
}
