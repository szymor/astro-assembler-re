/*
 * Small updates of counters and animation tables, written as translations of
 * the original register use (see core.h); the flags the callers may test are
 * those of the last flag-setting instruction, which the helpers reproduce.
 */
#include "core.h"

/*
 * Core_ClearFinishedA (0x41a93d): the word at 0x4b0c60 counts the live entries
 * (12 bytes, from 0x51e700; a zero first word = unused). Each live entry loses
 * 10 from its word (at 0 or below it becomes 0 and the count drops, which
 * stops the scan at 0), and, in even frames (bit 0 of the level time at
 * 0x46ffa0 clear), 4 from the dword at +4 if that is not 0. CX = the entries
 * left to visit.
 */
HAND_FN(41a93d)
{
    uint16_t cx = rd16(0x4b0c60u);
    test16(cx);
    if (flag(R_ZF)) { WR_REG(R_ECX, 2, cx); x86_ret(); return; }
    uint32_t edi = 0x51e6f4u;
    for (;;) {
        do { edi = add32(edi, 0xc); cmp16(rd16(edi), 0); } while (flag(R_ZF));
        test32(rd32(0x46ffa0u), 1);
        if (flag(R_ZF)) {
            cmp32(rd32(edi + 4), 0);
            if (!flag(R_ZF)) wr32(edi + 4, sub32(rd32(edi + 4), 4));
        }
        const uint16_t w = sub16(rd16(edi), 0xa);
        wr16(edi, w);
        bool counted = flag(R_ZF);
        if (!counted && !flag(R_SF)) goto next;       /* JNS: still positive */
        if (!counted) wr16(edi, 0);
        wr16(0x4b0c60u, dec16(rd16(0x4b0c60u)));
        if (flag(R_ZF)) break;
    next:
        cx = dec16(cx);
        if (flag(R_ZF)) break;
    }
    WR_REG(R_ECX, 2, cx);
    WR_REG(R_EDI, 4, edi);
    x86_ret();
}

/*
 * Core_ClearFinishedB (0x41b026): the word at 0x4b0dc8 counts the live entries
 * (14 bytes, from 0x51f354; a zero first word = unused). A live entry's
 * countdown word goes down by 1; at 0 the entry is freed (word cleared, count
 * down). Above 10 it also moves: the word at +4 drops by 2 (freed when below
 * 0xff74 as a signed word) and, in even frames, the dword at +6 grows by 4,
 * wrapping at 0x38. ECX = the entries left to visit.
 */
HAND_FN(41b026)
{
    uint32_t ecx = rd16(0x4b0dc8u);
    test16((uint16_t)ecx);
    if (flag(R_ZF)) { WR_REG(R_ECX, 4, ecx); x86_ret(); return; }
    uint32_t edi = 0x51f346u;
    for (;;) {
        do { edi = add32(edi, 0xe); cmp16(rd16(edi), 0); } while (flag(R_ZF));
        bool free_it;
        wr16(edi, dec16(rd16(edi)));
        free_it = flag(R_ZF);
        if (!free_it) {
            cmp16(rd16(edi), 0xa);
            if (!cond_le()) {
                wr16(edi + 4, sub16(rd16(edi + 4), 2));
                cmp16(rd16(edi + 4), 0xff74);
                if (cond_l()) {
                    free_it = true;
                } else {
                    test32(rd32(0x46ffa0u), 1);
                    if (flag(R_ZF)) {
                        wr32(edi + 6, add32(rd32(edi + 6), 4));
                        cmp32(rd32(edi + 6), 0x38);
                        if (!cond_l()) wr32(edi + 6, sub32(rd32(edi + 6), 0x38));
                    }
                }
            }
        }
        if (free_it) {
            wr16(edi, 0);
            wr16(0x4b0dc8u, dec16(rd16(0x4b0dc8u)));
        }
        ecx = dec32(ecx);
        if (flag(R_ZF)) break;
    }
    WR_REG(R_ECX, 4, ecx);
    WR_REG(R_EDI, 4, edi);
    x86_ret();
}

/*
 * Core_FindEntryOrFree (0x415e97): the word at 0x4ba740 counts the used
 * entries of a table of 10 (48 bytes each, from 0x519a60; a zero first word =
 * free). With the table full, CF = 1. Otherwise the used entries are searched
 * for one whose dword at +0xe equals EDI: found, CF = 1 and ESI is that entry.
 * Not found: ESI = the first free entry and CF = 0. CX = the entries left.
 */
HAND_FN(415e97)
{
    uint16_t cx = rd16(0x4ba740u);
    uint32_t esi = 0x519a30u;
    cmp16(cx, 0xa);
    if (flag(R_ZF)) goto found;
    test16(cx);
    if (!flag(R_ZF)) {
        for (;;) {
            do { esi = add32(esi, 0x30); cmp16(rd16(esi), 0); } while (flag(R_ZF));
            cmp32(rd32(esi + 0xe), EDI);
            if (flag(R_ZF)) goto found;
            cx = dec16(cx);
            if (flag(R_ZF)) break;
        }
        esi = 0x519a30u;
    }
    do { esi = add32(esi, 0x30); cmp16(rd16(esi), 0); } while (!flag(R_ZF));
    set_flag(R_CF, false);
    goto done;
found:
    set_flag(R_CF, true);
done:
    WR_REG(R_ECX, 2, cx);
    WR_REG(R_ESI, 4, esi);
    x86_ret();
}

/*
 * Core_NextFrames (0x413849): 16 animation records of 8 bytes at the pointer in
 * 0x4bcb10 (dword = frame data, word = frame counter and count, word = frame
 * size): each steps its frame (wrapping to 0 at the count) and copies the
 * frame's pixels (size bytes) to the buffer from the pointer at 0x4be944 + 0x2d0.
 */
HAND_FN(413849)
{
    uint32_t edi = rd32(0x4be944u) + 0x2d0u, ebp = rd32(0x4bcb10u);
    uint32_t esi = 0, eax = EAX, ecx = ECX;
    uint16_t bx = 0x10;
    do {
        esi = rd32(ebp);
        const uint16_t w = rd16(ebp + 4);
        uint8_t al = (uint8_t)w, ah = (uint8_t)(w >> 8);
        ah++;
        if (al == ah) ah = 0;
        wr16(ebp + 4, (uint16_t)(al | ah << 8));
        ecx = rd16(ebp + 6);
        eax = (uint32_t)(int32_t)(int16_t)(uint16_t)(ah * (ecx & 0xff));
        esi = add32(esi, eax);
        copy_forward(edi, esi, ecx);
        edi += ecx;
        esi += ecx;
        ecx = 0;
        ebp = add32(ebp, 8);
        bx = dec16(bx);
    } while (!flag(R_ZF));
    WR_REG(R_EAX, 4, eax); WR_REG(R_ECX, 4, ecx); WR_REG(R_EBX, 2, bx);
    WR_REG(R_ESI, 4, esi); WR_REG(R_EDI, 4, edi); WR_REG(R_EBP, 4, ebp);
    x86_ret();
}

/*
 * Level tables (0x42d0fc, 0x42f888, 0x43029b: three copies of the same code): AX (a level 0..7) selects BX and CX: 1/0 for 7
 * and up, 0xa/1 for 6, 9/1 for 5, 8/1 for 4, 8/2 for 3, 8/3 for 2, 8/4 for 1,
 * 8/5 for 0. Flags: those of the last compare of AX.
 */
static void level_effect(void)
{
    static const uint16_t bx[8] = {1, 0xa, 9, 8, 8, 8, 8, 8};
    static const uint16_t cx[8] = {0, 1, 1, 1, 2, 3, 4, 5};
    const uint16_t ax = (uint16_t)EAX;
    unsigned i = 0;
    for (; i < 7; i++) {
        WR_REG(R_EBX, 2, bx[i]);
        WR_REG(R_ECX, 2, cx[i]);
        cmp16(ax, (uint16_t)(7 - i));
        if (cond_ae()) { x86_ret(); return; }
    }
    WR_REG(R_EBX, 2, 8);
    WR_REG(R_ECX, 2, 5);
    x86_ret();
}
HAND_FN(42d0fc) { level_effect(); }
HAND_FN(42f888) { level_effect(); }
HAND_FN(43029b) { level_effect(); }

/*
 * Palette_StartFade (0x4135ba): CX = the number of steps, EDI the
 * target palette (720 bytes: 240 colours of 3 components). Stores the step count
 * and target, then for each of the 720 components the current value (as a 16-bit
 * fixed point, byte << 8) at 0x4be94e + 2 i and the step (target - current) / -CX
 * at +0x5a0 (dividing by the negated count: CX is 16 bits, signed). If the
 * current palette pointer is not the work buffer at 0x4bf48e, the 720 bytes are
 * copied there and the pointer moved.
 */
HAND_FN(4135ba)
{
    uint16_t cx = (uint16_t)ECX;
    uint32_t ax_hi = EAX & 0xffff0000u, dx_hi = EDX & 0xffff0000u;
    uint32_t esi = rd32(0x4be944u), edi = EDI, ebp = 0x4be94eu, ebx = 0x2d0, ecx;
    uint16_t ax = 0, dx = 0;
    wr16(0x4be94cu, cx);
    wr32(0x4be948u, edi);
    cx = neg16(cx);
    do {
        uint8_t ah = rd8(esi);
        ax = (uint16_t)(ah << 8);
        wr16(ebp, ax);
        ah = sub8(ah, rd8(edi));
        ax = (uint16_t)(ah << 8);
        const int32_t num = (int16_t)ax;
        const int32_t den = (int16_t)cx;
        ax = (uint16_t)(num / den);
        dx = (uint16_t)(num % den);
        wr16(ebp + 0x5a0, ax);
        esi = add32(esi, 1);                           /* INC; its flags are overwritten below */
        edi = add32(edi, 1);
        ebp = add32(ebp, 2);
        ebx = dec32(ebx);
    } while (!flag(R_ZF));
    WR_REG(R_EAX, 4, ax_hi | ax);
    WR_REG(R_EDX, 4, dx_hi | dx);
    WR_REG(R_EBX, 4, ebx);
    WR_REG(R_EBP, 4, ebp);
    esi = rd32(0x4be944u);
    edi = 0x4bf48eu;
    cmp32(esi, edi);
    ecx = (ECX & 0xffff0000u) | cx;
    if (!flag(R_ZF)) {
        wr32(0x4be944u, edi);
        copy_forward(edi, esi, 0xb4 * 4);
        esi += 0xb4 * 4;
        edi += 0xb4 * 4;
        ecx = 0;
    }
    WR_REG(R_ECX, 4, ecx);
    WR_REG(R_ESI, 4, esi);
    WR_REG(R_EDI, 4, edi);
    x86_ret();
}

/*
 * Core_StoreFiveEntries (0x42e61e): like Core_StoreFourEntries (init.c) for five
 * records, at 0x498692, 0x49870a, 0x498782 and 0x4987fa (0x78 apart) and
 * 0x498608.
 */
HAND_FN(42e61e)
{
    static const uint32_t rec[5] = {0x498692, 0x49870a, 0x498782, 0x4987fa, 0x498608};
    uint32_t esi = ESI, eax = 0;
    for (unsigned i = 0; i < 5; i++) {
        eax = rd32(esi);
        esi += 4;
        wr16(rec[i], 1);
        wr16(rec[i] + 2, 0);
        wr32(rec[i] + 4, eax);
        wr32(rec[i] + 8, EBX);
        wr32(rec[i] + 0xc, ECX);
    }
    WR_REG(R_ESI, 4, esi);
    WR_REG(R_EAX, 4, eax);
    x86_ret();
}

/*
 * Core_SetRecordFields (0x430874): the dword at 0x4a95d0 (0..3) selects which record
 * of a table at 0x4a9604.. gets its fixed fields. Flags: those of the compare
 * with 3.
 */
HAND_FN(430874)
{
    const uint32_t sel = rd32(0x4a95d0u);
    if (sel == 0) {
        wr16(0x4a9604u, 1); wr16(0x4a9608u, 0xc8); wr16(0x4a9606u, 0xa0);
    }
    if (sel == 1) {
        wr16(0x4a9616u, 1); wr16(0x4a961au, 0x140); wr16(0x4a9618u, 0x118);
        wr16(0x4a9624u, 0xffa6); wr16(0x4a9626u, 0x10e);
    }
    if (sel == 2) {
        wr16(0x4a963au, 1); wr16(0x4a963eu, 0x140); wr16(0x4a963cu, 0x118);
        wr16(0x4a9648u, 0); wr16(0x4a964au, 0x168);
    }
    if (sel == 3) {
        wr16(0x4a9628u, 1); wr16(0x4a962cu, 0x140); wr16(0x4a962au, 0x118);
        wr16(0x4a9636u, 0xff79); wr16(0x4a9638u, 0xff);
    }
    cmp32(sel, 3);
    x86_ret();
}

/*
 * DIV by a byte of AX (a byte value zero-extended into AX; the original traps
 * on a zero divisor, as the C division does here): AL = quotient, AH = remainder,
 * flags untouched (the code ORs AH right after).
 */
static uint16_t div_ax_by_byte(uint16_t ax, uint8_t d) { return (uint16_t)((ax % d) << 8 | (ax / d)); }

/*
 * Core_StepEntries24 (0x419819): the word at 0x4b0a10 counts the live entries (24
 * bytes, from 0x51e070; first dword zero = unused). Each live entry's position
 * dword (+0) grows by its speed (+8), a position at 0 or below, or from 0x1b60000
 * on, ends the entry (cleared, count down; the scan stops when none is left),
 * +4 grows by +0xc, and every time the frame counter (byte at 0x46ffa0) is a
 * multiple of the byte at 0x4b0a16 the frame word (+0x12) advances by 2 (mod 0x10).
 */
HAND_FN(419819)
{
    uint16_t cx = rd16(0x4b0a10u);
    test16(cx);
    if (flag(R_ZF)) { WR_REG(R_ECX, 2, cx); x86_ret(); return; }
    uint32_t edi = 0x51e058u, eax = EAX, esi = ESI;
    for (;;) {
        do { edi = add32(edi, 0x18); cmp32(rd32(edi), 0); } while (flag(R_ZF));
        eax = rd32(edi + 8);
        wr32(edi, add32(rd32(edi), eax));
        bool ended = flag(R_ZF);
        if (!ended) {
            if (flag(R_SF)) ended = true;
            else { cmp32(rd32(edi), 0x1b60000u); if (flag(R_CF) == 0) ended = true; }
            if (ended) wr32(edi, 0);
        }
        if (ended) {
            wr16(0x4b0a10u, dec16(rd16(0x4b0a10u)));
            if (flag(R_ZF)) break;
        }
        eax = rd32(edi + 0xc);
        wr32(edi + 4, add32(rd32(edi + 4), eax));
        eax = (eax & 0xffff0000u) | div_ax_by_byte(rd8(0x46ffa0u), rd8(0x4b0a16u));
        test8((uint8_t)(eax >> 8));
        if (flag(R_ZF)) {
            uint16_t si = add16(rd16(edi + 0x12), 2);
            cmp16(si, 0x10);
            if (flag(R_ZF)) { si = 0; test16(0); }
            wr16(edi + 0x12, si);
            esi = (esi & 0xffff0000u) | si;
        }
        cx = dec16(cx);
        if (flag(R_ZF)) break;
    }
    WR_REG(R_EAX, 4, eax); WR_REG(R_ECX, 2, cx); WR_REG(R_EDX, 4, 0); WR_REG(R_ESI, 4, esi); WR_REG(R_EDI, 4, edi);
    x86_ret();
}

/*
 * Core_StepEntries14 (0x41aa6d): the word at 0x4b0c94 counts the live entries (14
 * bytes, from 0x51ed90; first word zero = unused). A live entry's life word
 * (+0) drops by 8 (at 0 or below the entry is cleared and counted down; the
 * scan stops when none is left); a live one advances its tick byte (+8); while
 * the word at +4 is non-zero it drops by 4 every (byte at 0x4b0c9a) ticks; and
 * every (byte at 0x4b0c9b) ticks the word at +6 flips bit 1.
 */
HAND_FN(41aa6d)
{
    uint16_t cx = rd16(0x4b0c94u);
    test16(cx);
    if (flag(R_ZF)) { WR_REG(R_ECX, 2, cx); x86_ret(); return; }
    uint32_t edi = 0x51ed82u, eax = EAX;
    for (;;) {
        do { edi = add32(edi, 0xe); cmp16(rd16(edi), 0); } while (flag(R_ZF));
        wr16(edi, sub16(rd16(edi), 8));
        bool dead = flag(R_ZF);
        if (!dead && flag(R_SF)) { wr16(edi, 0); dead = true; }
        if (dead) {
            wr16(0x4b0c94u, dec16(rd16(0x4b0c94u)));
            if (flag(R_ZF)) break;                      /* none left: return */
            goto next;
        }
        wr16(edi + 8, inc16(rd16(edi + 8)));
        eax = (eax & 0xffff0000u) | rd8(edi + 8);
        cmp16(rd16(edi + 4), 0);
        if (!flag(R_ZF)) {
            eax = (eax & 0xffff0000u) | div_ax_by_byte((uint16_t)eax, rd8(0x4b0c9au));
            test8((uint8_t)(eax >> 8));
            if (flag(R_ZF)) wr16(edi + 4, sub16(rd16(edi + 4), 4));
            eax = (eax & 0xffff0000u) | rd8(edi + 8);
        }
        eax = (eax & 0xffff0000u) | div_ax_by_byte((uint16_t)eax, rd8(0x4b0c9bu));
        test8((uint8_t)(eax >> 8));
        if (flag(R_ZF)) {
            const uint16_t v = (uint16_t)(rd16(edi + 6) ^ 2);
            wr16(edi + 6, v);
            test16(v);
        }
    next:
        cx = dec16(cx);
        if (flag(R_ZF)) break;
    }
    WR_REG(R_EAX, 4, eax); WR_REG(R_ECX, 2, cx); WR_REG(R_EDX, 2, 0); WR_REG(R_EDI, 4, edi);
    x86_ret();
}
