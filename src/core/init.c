/*
 * Initialisation of tables and player state: fills, copies and fixed stores.
 * Written as translations of the original register use (see core.h).
 */
#include "core.h"

#include <stdio.h>
#include <stdlib.h>

#define G_PLAYER_MASK 0x46ffe4u    /* bit 0: player 1 takes part, bit 1: player 2 */

/*
 * Core_StoreFrameConstants (0x409658): EAX is stored as the immediate of 11
 * instructions in the 8-bpp/16-bpp blitting code (0x413a8a...), EBX in two
 * in the menu code, and (EAX, EBX, ECX, EDX) at 0x46ffd4. The core patches
 * its own code like this (see recompiler.md); stores into code memory behave
 * like any other store here.
 */
HAND_FN(409658)
{
    static const uint32_t from_eax[] = {0x413a8a, 0x413aac, 0x413bb4, 0x413b0f, 0x413b56, 0x413ce3,
                                        0x413c34, 0x413d2b, 0x413d66, 0x413db5, 0x413c80};
    wr32(0x46ffd4u, EAX);
    wr32(0x46ffd8u, EBX);
    wr32(0x46ffdcu, ECX);
    wr32(0x46ffe0u, EDX);
    for (unsigned i = 0; i < sizeof from_eax / sizeof from_eax[0]; i++) wr32(from_eax[i], EAX);
    wr32(0x444289u, EBX);
    wr32(0x444384u, EBX);
    x86_ret();
}

/*
 * Core_InitPlayer1Ship (0x409e91): if player 1 takes part, sets the fields of
 * its ship (0x4700a4.., 0x470164 = x, 0x470166 = y): x = 0x120 (0xa2 when
 * player 2 takes part too), y = BX; the other fields fixed. AX = x.
 */
HAND_FN(409e91)
{
    test32(rd32(G_PLAYER_MASK), 1);
    if (!flag(R_ZF)) {
        uint16_t x = 0x120;
        test32(rd32(G_PLAYER_MASK), 2);
        if (!flag(R_ZF)) x = 0xa2;
        wr16(0x4700a4u, 0);
        wr16(0x4700a6u, 0);
        wr16(0x4700a8u, 0xfa);
        wr16(0x4700e8u, 2);
        wr16(0x470164u, x);
        wr16(0x470166u, (uint16_t)EBX);
        wr16(0x4700eau, x);
        wr16(0x4700ecu, 0xb8);
        wr16(0x4700eeu, 0);
        wr16(0x4700f0u, 4);
        set_ax(x);
    }
    x86_ret();
}

/*
 * Core_InitPlayer2Ship (0x409f04): the same for player 2 (fields +0x158
 * from player 1's): x = 0x120, or 0x19e when player 1 takes part too.
 */
HAND_FN(409f04)
{
    test32(rd32(G_PLAYER_MASK), 2);
    if (!flag(R_ZF)) {
        uint16_t x = 0x120;
        test32(rd32(G_PLAYER_MASK), 1);
        if (!flag(R_ZF)) x = 0x19e;
        wr16(0x4701fcu, 0);
        wr16(0x4701feu, 0);
        wr16(0x470200u, 0xfa);
        wr16(0x470240u, 2);
        wr16(0x4702bcu, x);
        wr16(0x4702beu, (uint16_t)EBX);
        wr16(0x470242u, x);
        wr16(0x470244u, 0xb8);
        wr16(0x470246u, 0);
        wr16(0x470248u, 4);
        set_ax(x);
    }
    x86_ret();
}

/*
 * Core_ClearTables (0x445a93): four dwords cleared, then 150 dwords from
 * 0x517c70. Flags: those of XOR EAX, EAX.
 */
HAND_FN(445a93)
{
    wr32(0x517c40u, 0);
    wr32(0x50f2a8u, 0);
    wr32(0x5109d0u, 0);
    wr32(0x517c6cu, 0);
    fill_dwords(0x517c70u, 0, 0x96);
    flags_szp32(0);
    set_flag(R_CF, false);
    set_flag(R_OF, false);
    WR_REG(R_EAX, 4, 0);
    WR_REG(R_EDI, 4, 0x517c70u + 4 * 0x96);
    WR_REG(R_ECX, 4, 0);
    x86_ret();
}

/*
 * Core_FillZeroThenOnes (0x40982e): at EDI plus the arena offset (the dword at
 * 0x556ac8), ECX dwords of 0 and then EDX dwords of 0xffffffff. EDI ends after
 * them, EAX = 0xffffffff, ECX = 0. Flags: those of DEC EAX (CF of the address
 * addition).
 */
HAND_FN(40982e)
{
    uint32_t edi = add32(EDI, rd32(0x556ac8u));
    fill_dwords(edi, 0, ECX);
    edi += 4 * ECX;
    const uint32_t n = EDX;
    (void)dec32(0);
    fill_dwords(edi, 0xffffffffu, n);
    edi += 4 * n;
    WR_REG(R_EAX, 4, 0xffffffffu);
    WR_REG(R_ECX, 4, 0);
    WR_REG(R_EDI, 4, edi);
    x86_ret();
}

/*
 * Core_CopyRows (0x409818): CX rows (a count of 0 means 65536) of 16 dwords
 * each are copied from ESI to EDI; EDI skips 0x330 bytes after each row.
 * ECX ends at 0; flags: those of DEC CX (CF of the last EDI addition).
 */
HAND_FN(409818)
{
    uint16_t rows = (uint16_t)ECX;
    uint32_t esi = ESI, edi = EDI;
    do {
        copy_forward(edi, esi, 0x40);
        esi += 0x40;
        edi = add32(edi + 0x40, 0x330);
        rows = dec16(rows);
    } while (!flag(R_ZF));
    WR_REG(R_ECX, 4, 0);
    WR_REG(R_ESI, 4, esi);
    WR_REG(R_EDI, 4, edi);
    x86_ret();
}

/*
 * Core_SetBestTime1 / 2 (0x41851f, 0x418559): the word at `field` (player 1's
 * 0x470058, player 2's 0x4701b0) is set from AX if it is 0 and the mode word at
 * `mode` has bit 0; or, if it is non-zero and the mode word is 2, lowered to BX
 * when BX is below it. Only the first case returns to the caller; the others
 * drop the caller's return address as well (ADD ESP, 4; RET), returning to its
 * caller. Flags: those of the last compare, or of that ADD.
 */
static void set_best(uint32_t field, uint32_t mode)
{
    cmp16(rd16(field), 0);
    if (flag(R_ZF)) {
        test16m(rd16(mode), 1);
        if (!flag(R_ZF)) {
            wr16(field, (uint16_t)EAX);
            x86_ret();
            return;
        }
    } else {
        cmp16(rd16(mode), 2);
        if (flag(R_ZF)) {
            cmp16(rd16(field), (uint16_t)EBX);
            if (!flag(R_CF)) wr16(field, (uint16_t)EBX);
        }
    }
    WR_REG(R_ESP, 4, add32(ESP, 4));
    x86_ret();
}
HAND_FN(41851f) { set_best(0x470058u, 0x55c990u); }
HAND_FN(418559) { set_best(0x4701b0u, 0x55c9a8u); }

/*
 * Core_PickNearerPlayer (0x4422d6): decides which player a pointer-using
 * routine follows. Nothing happens unless a player takes part. The target is
 * set from player 2's ship at 0x4702bc/0x4702be (marker 4, pointer 0x470258);
 * if player 1 takes part, from player 1's ship at 0x470164/0x470166 (marker 0,
 * pointer 0x470100); if both take part, player 2 is chosen when the point
 * (AX - 0x24, BX - 0x20) is closer to it (squared distance, 16-bit
 * coordinates). Result: words at 0x46fffc, 0x46fffe, the dword at 0x470000 and
 * the marker at 0x470004. All registers are preserved (PUSHAD/POPAD);
 * the flags are those of the deciding test or compare.
 */
HAND_FN(4422d6)
{
    test32(rd32(G_PLAYER_MASK), 3);
    if (flag(R_ZF)) { x86_ret(); return; }
    uint16_t si = rd16(0x4702bcu), di = rd16(0x4702beu);
    uint32_t marker = 4, ebp = 0x470258u;
    test32(rd32(G_PLAYER_MASK), 1);
    if (!flag(R_ZF)) {
        si = rd16(0x470164u);
        di = rd16(0x470166u);
        marker = 0;
        ebp = 0x470100u;
        test32(rd32(G_PLAYER_MASK), 2);
        if (!flag(R_ZF)) {
            const uint16_t ax = (uint16_t)(EAX - 0x24), bx = (uint16_t)(EBX - 0x20);
            const int32_t a = (int16_t)(uint16_t)(ax - di), b = (int16_t)(uint16_t)(bx - si);
            const int32_t c = (int16_t)(uint16_t)(ax - rd16(0x4702beu)), d = (int16_t)(uint16_t)(bx - rd16(0x4702bcu));
            cmp32((uint32_t)(a * a + b * b), (uint32_t)(c * c + d * d));
            if (cond_a()) {
                si = rd16(0x4702bcu);
                di = rd16(0x4702beu);
                ebp = 0x470258u;
                marker = 4;
            }
        }
    }
    wr32(0x470004u, marker);
    wr16(0x46fffcu, si);
    wr16(0x46fffeu, di);
    wr32(0x470000u, ebp);
    x86_ret();
}

/*
 * Core_StoreFourEntries (0x42b030): four records at 0x497dbe, 0x497e6e, 0x497f1e and
 * 0x497fce (0xb0 apart) are initialised from
 * the four dwords at ESI: in use (word 1), word 0, the dword, then EBX and ECX.
 * ESI ends after the list, EAX = the last dword.
 */
HAND_FN(42b030)
{
    uint32_t esi = ESI, eax = 0;
    for (uint32_t rec = 0x497dbeu; rec <= 0x497fceu; rec += 0xb0) {
        eax = rd32(esi);
        esi += 4;
        wr16(rec, 1);
        wr16(rec + 2, 0);
        wr32(rec + 4, eax);
        wr32(rec + 8, EBX);
        wr32(rec + 0xc, ECX);
    }
    WR_REG(R_ESI, 4, esi);
    WR_REG(R_EAX, 4, eax);
    x86_ret();
}

/*
 * Core_CopyTiles (0x4097d9): copies 3 x 3 x 2 blocks of 32 bytes from ESI to EDI
 * (plus the arena offset at 0x556ac8): the two blocks of a pair go 0x350 bytes
 * past each other's end, the same source row is repeated for three destination
 * rows (0xb0 further each), then the source moves on by 0x40. Registers: ESI
 * ends 0xc0 on, EDI at the end of the last block, ECX = EDX = 0. Flags: those of
 * the last DEC CX (CF of the preceding ESI addition).
 */
HAND_FN(4097d9)
{
    uint32_t esi = ESI, edi = EDI + rd32(0x556ac8u);
    uint16_t outer = 3, mid, inner;
    do {
        mid = 3;
        do {
            const uint32_t row_esi = esi;
            inner = 2;
            do {
                copy_forward(edi, esi, 32);
                edi += 32 + 0x350;
                esi += 32;
                inner = dec16(inner);
            } while (!flag(R_ZF));
            esi = row_esi;
            edi = sub32(edi, 0x690);
            mid = dec16(mid);
        } while (!flag(R_ZF));
        esi = add32(esi, 0x40);
        outer = dec16(outer);
    } while (!flag(R_ZF));
    WR_REG(R_ESI, 4, esi);
    WR_REG(R_EDI, 4, edi);
    WR_REG(R_ECX, 4, 0);
    WR_REG(R_EDX, 2, 0);
    x86_ret();
}

/*
 * Core_MaskBlit (0x415a58): CX rows (the word at 0x4b0630) of 4 dword pairs (AND
 * mask, OR bits: the sprite masks at the pointer in 0x4b0632) are combined into
 * the screen at EAX + the row table entry (word index at 0x4b0624) + the arena
 * offset; pairs are 8 bytes apart, rows 0x370. EAX = the last dword read.
 * Flags: those of the DEC CX (CF of the last EDI addition).
 */
HAND_FN(415a58)
{
    uint32_t edi = rd16(0x4b0640u + 2 * rd32(0x4b0624u)) + EAX + rd32(0x556ac8u);
    uint32_t esi = rd32(0x4b0632u), eax = 0;
    uint16_t cx = rd16(0x4b0630u);
    do {
        for (unsigned i = 0; i < 4; i++) {
            eax = rd32(esi); esi += 4;
            wr32(edi, rd32(edi) & eax);
            eax = rd32(esi); esi += 4;
            wr32(edi, rd32(edi) | eax);
            edi = add32(edi, i < 3 ? 8 : 0x358);
        }
        cx = dec16(cx);
    } while (!flag(R_ZF));
    WR_REG(R_EAX, 4, eax);
    WR_REG(R_ECX, 2, cx);
    WR_REG(R_ESI, 4, esi);
    WR_REG(R_EDI, 4, edi);
    x86_ret();
}

/*
 * Core_Cross4 (0x433acd): which side of the line through (EAX, EBX) and
 * (EBP, EDI) the four corners made of the words at 0x470888/0x47088a (x) and
 * 0x47088c/0x47088e (y) lie on: the byte at 0x46ffa8 collects a bit per corner
 * that is on the non-negative side (cross product, 32 bits). EBP, EDI become the
 * line's direction; ECX and EDX keep the last corner's values.
 */
HAND_FN(433acd)
{
    static const uint32_t corner[4][2] = {{0x470888, 0x47088c}, {0x470888, 0x47088e},
                                          {0x47088a, 0x47088e}, {0x47088a, 0x47088c}};
    const uint32_t eax = EAX, ebx = EBX;
    uint32_t ebp = sub32(EBP, eax), edi = sub32(EDI, ebx), ecx = 0, edx = 0;
    uint8_t mask = 0;
    wr8(0x46ffa8u, 0);
    for (unsigned i = 0; i < 4; i++) {
        ecx = (uint32_t)(int32_t)(int16_t)rd16(corner[i][0]);
        edx = (uint32_t)(int32_t)(int16_t)rd16(corner[i][1]);
        ecx = sub32(ecx, eax);
        edx = sub32(edx, ebx);
        ecx *= edi;
        edx *= ebp;
        edx = sub32(edx, ecx);
        ecx = (ecx & 0xffffff00u) | (cond_ge() ? 1u : 0u);
        mask |= (uint8_t)ecx;
        wr8(0x46ffa8u, mask);
        test8(mask);
    }
    WR_REG(R_EBP, 4, ebp); WR_REG(R_EDI, 4, edi); WR_REG(R_ECX, 4, ecx); WR_REG(R_EDX, 4, edx);
    x86_ret();
}

/*
 * Core_MaskBlitRows (0x41599f): CX rows of 10 dword pairs (AND mask, OR bits;
 * the data at ESI) combined into the screen at EDI, the pairs 4 bytes apart;
 * after a row ESI skips 0x324 bytes and EDI 0x11c (counted from the last pair).
 * EAX = the last dword read. Flags: those of the DEC CX (CF of the EDI addition).
 */
HAND_FN(41599f)
{
    uint32_t esi = ESI, edi = EDI, eax = 0;
    uint16_t cx = (uint16_t)ECX;
    do {
        for (unsigned i = 0; i < 10; i++) {
            eax = rd32(esi); esi += 4;
            wr32(edi, rd32(edi) & eax);
            eax = rd32(esi);
            esi += i < 9 ? 4 : 0x324;
            wr32(edi, rd32(edi) | eax);
            edi = add32(edi, i < 9 ? 4 : 0x11c);
        }
        cx = dec16(cx);
    } while (!flag(R_ZF));
    WR_REG(R_EAX, 4, eax);
    WR_REG(R_ECX, 2, cx);
    WR_REG(R_ESI, 4, esi);
    WR_REG(R_EDI, 4, edi);
    x86_ret();
}

/*
 * Core_EnergyStep (0x42d1a0): the countdown dword at 0x4983cc drops by an amount
 * chosen by the word at 0x498868 (a level: 4 below 0xe, 6 below 0xd ... 0x18 for
 * 0). When it runs out and none of five entries (words at 0x4984c4, 0x498550,
 * 0x4984d0, 0x498502, 0x498536) is in use, the next pair of dwords from the list
 * at the pointer in 0x4983c8 is loaded (countdown, 0x4983d4) and a timer of 180
 * started. The timer (word at 0x4983d0) counts down, and when it reaches 0 with
 * the entry at 0x498550 unused, the dword at 0x4983c4 is set to 0x18000.
 */
HAND_FN(42d1a0)
{
    static const uint8_t amount[14] = {4, 6, 8, 0xa, 0xc, 0xe, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
    uint32_t eax = 4, esi = ESI;
    unsigned i = 0;
    for (; i < 14; i++) {
        eax = amount[i];
        cmp16(rd16(0x498868u), (uint16_t)(0xe - i));
        if (cond_ae()) break;
    }
    if (i == 14) eax = 0x18;
    const uint32_t left = sub32(rd32(0x4983ccu), eax);
    wr32(0x4983ccu, left);
    if (!flag(R_ZF) && flag(R_SF) == flag(R_OF)) goto timer;       /* JG */
    {
        static const uint32_t used[5] = {0x4984c4, 0x498550, 0x4984d0, 0x498502, 0x498536};
        for (unsigned k = 0; k < 5; k++) {
            cmp16(rd16(used[k]), 0);
            if (!flag(R_ZF)) goto timer;
        }
        esi = rd32(0x4983c8u);
        eax = rd32(esi);
        wr32(0x4983ccu, eax);
        eax = rd32(esi + 4);
        wr32(0x4983d4u, eax);
        esi += 8;
        wr32(0x4983c8u, esi);
        wr16(0x4983d0u, 0xb4);
        wr32(0x4983c4u, 0x20000);
    }
timer:
    cmp16(rd16(0x4983d0u), 0);
    if (!flag(R_ZF)) {
        wr16(0x4983d0u, dec16(rd16(0x4983d0u)));
        if (flag(R_ZF)) {
            cmp16(rd16(0x498550u), 0);
            if (flag(R_ZF)) wr32(0x4983c4u, 0x18000);
        }
    }
    WR_REG(R_EAX, 4, eax);
    WR_REG(R_ESI, 4, esi);
    x86_ret();
}

/*
 * Core_PlayerDistance (0x41b494): the distance from the point (AX, BX) to the
 * player Core_PickNearerPlayer chooses (its position words at 0x46fffc/0x46fffe,
 * offset by 0x1a and 0x24, and the word at 0x4b1f9a): returns CF = 1 when the
 * point is outside the visible area (AX outside -0x14..0x15e, BX above 0x320 as
 * an unsigned value) or has no distance; otherwise EBX = the distance in 16.16
 * fixed point (a square root by 12 Newton steps) and CF = 0 if it is above
 * 0x100000. Also stores the point times 16 (0x46ff64/0x46ff66) and the scaled
 * differences (0x46ffcc, 0x46ffc8).
 */
HAND_FN(41b494)
{
    HAND_CALL(f_4422d6, 0x41b499u);
    uint32_t eax = EAX, ebx = EBX, ecx = ECX, edx = EDX;
    uint16_t cx = rd16(0x46fffeu), dx = rd16(0x46fffcu);
    cx = add16(cx, 0x24);
    dx = add16(dx, 0x1a);
    uint16_t ax = (uint16_t)eax, bx = (uint16_t)ebx;
    cmp16(ax, 0xffec);
    if (cond_l()) goto fail;
    cmp16(ax, 0x15e);
    if (!flag(R_ZF) && flag(R_SF) == flag(R_OF)) goto fail;       /* JG */
    cmp16(bx, 0x320);
    if (cond_a()) goto fail;
    wr16(0x46ff64u, ax);
    wr16(0x46ff66u, bx);
    wr16(0x46ff64u, (uint16_t)(rd16(0x46ff64u) << 4));
    wr16(0x46ff66u, (uint16_t)(rd16(0x46ff66u) << 4));
    ax = sub16(ax, cx);
    dx = add16(dx, rd16(0x4b1f9au));
    bx = sub16(bx, dx);
    bx = add16(bx, 2);
    {
        const int32_t p = (int32_t)(int16_t)ax * 0x4ccd;
        ax = (uint16_t)p;
        dx = (uint16_t)((uint32_t)p >> 16);
    }
    eax = (uint32_t)(int32_t)(int16_t)dx;
    bx = (uint16_t)((int16_t)bx >> 2);
    wr32(0x46ffccu, eax);
    ebx = (uint32_t)(int32_t)(int16_t)bx;
    wr32(0x46ffc8u, ebx);
    eax *= eax;
    ebx *= ebx;
    eax = add32(eax, ebx);
    if (flag(R_ZF)) { set_lo16(&edx, dx); goto fail_regs; }
    {
        const uint32_t ebp = eax;
        uint16_t n = 0xc;
        ebx = 0x10000;
        do {
            const uint64_t dividend = (uint64_t)ebp << 32;
            const uint64_t q = dividend / ebx;
            if (q > 0xffffffffu) { fprintf(stderr, "Core_PlayerDistance: divide overflow\n"); abort(); }
            eax = (uint32_t)q;
            edx = (uint32_t)(dividend % ebx);
            ebx += eax;
            ebx >>= 1;
            n--;
        } while (n != 0);
        cx = 0;
        set_lo16(&ecx, cx);
        cmp32(ebx, 0x100000u);
        if (!cond_a()) set_flag(R_CF, true);
        WR_REG(R_EAX, 4, eax); WR_REG(R_EBX, 4, ebx); WR_REG(R_ECX, 4, ecx); WR_REG(R_EDX, 4, edx);
        x86_ret();
        return;
    }
fail:
    set_lo16(&eax, ax); set_lo16(&ebx, bx); set_lo16(&edx, dx);
fail_regs:
    set_lo16(&ecx, cx);
    set_flag(R_CF, true);
    WR_REG(R_EAX, 4, eax); WR_REG(R_EBX, 4, ebx); WR_REG(R_ECX, 4, ecx); WR_REG(R_EDX, 4, edx);
    x86_ret();
}
