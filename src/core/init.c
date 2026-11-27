/*
 * Initialisation of tables and player state: fills, copies and fixed stores.
 * Written as translations of the original register use (see core.h).
 */
#include "core.h"

#include <stdio.h>
#include <stdlib.h>


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
 * Core_InitPlayer1Ship / 2Ship (0x409e91, 0x409f04): if the player takes part,
 * its ship starts at x = 0x120, or on its own side (player 1 0xa2, player 2
 * 0x19e) when both play, and y = BX, with no invulnerability left; the other
 * fields (meaning unknown) get fixed values. AX = x.
 */
static void init_ship(unsigned player)
{
    test32(rd32(G_PLAYER_MASK), player ? 2 : 1);
    if (!flag(R_ZF)) {
        uint16_t x = 0x120;
        test32(rd32(G_PLAYER_MASK), player ? 1 : 2);
        if (!flag(R_ZF)) x = player ? 0x19e : 0xa2;
        wr16(player_field(player, P1_INVULNERABLE), 0);
        wr16(player_field(player, 0x4700a6u), 0);
        wr16(player_field(player, 0x4700a8u), 0xfa);
        wr16(player_field(player, 0x4700e8u), 2);
        wr16(player_field(player, P1_X), x);
        wr16(player_field(player, P1_Y), (uint16_t)EBX);
        wr16(player_field(player, 0x4700eau), x);
        wr16(player_field(player, 0x4700ecu), 0xb8);
        wr16(player_field(player, 0x4700eeu), 0);
        wr16(player_field(player, 0x4700f0u), 4);
        set_ax(x);
    }
    x86_ret();
}
HAND_FN(409e91) { init_ship(0); }
HAND_FN(409f04) { init_ship(1); }

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
    uint32_t edi = add32(EDI, rd32(G_ARENA_BASE));
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
 * Player1_CanFire / Player2_CanFire (0x41851f, 0x418559): the gate at the start
 * of every weapon routine. With the fire cooldown at 0 and the fire button
 * held, the cooldown is set to AX and it returns to the weapon routine, which
 * fires. Otherwise it returns to that routine's caller (ADD ESP, 4; RET), so
 * nothing is fired; releasing the button shortens a running cooldown to BX, so
 * tapping fires faster than holding. Flags: those of the last compare, or of
 * that ADD.
 */
static void can_fire(unsigned player)
{
    const uint32_t field = player_field(player, P1_FIRE_COOLDOWN), mode = (player ? P2_CONTROLS : P1_CONTROLS) + CTRL_FIRE;
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
HAND_FN(41851f) { can_fire(0); }
HAND_FN(418559) { can_fire(1); }

/*
 * Core_PickNearerPlayer (0x4422d6): decides which player a pointer-using
 * routine follows. Nothing happens unless a player takes part. The target is
 * player 2's ship, or player 1's if player 1 takes part; if both do, player 2
 * when it is nearer the point (BX - 0x20, AX - 0x24) (squared distance, 16-bit
 * coordinates). Result: G_TARGET_X/Y, G_TARGET_PLAYER (0 or 4) and
 * G_TARGET_RECORD (a pointer into that player's record, 0x470100 + stride). All registers are preserved (PUSHAD/POPAD);
 * the flags are those of the deciding test or compare.
 */
HAND_FN(4422d6)
{
    test32(rd32(G_PLAYER_MASK), 3);
    if (flag(R_ZF)) { x86_ret(); return; }
    const uint32_t p2_x = player_field(1, P1_X), p2_y = player_field(1, P1_Y);
    uint16_t x = rd16(p2_x), y = rd16(p2_y);
    uint32_t target = 4, record = player_field(1, 0x470100u);
    test32(rd32(G_PLAYER_MASK), 1);
    if (!flag(R_ZF)) {
        x = rd16(P1_X);
        y = rd16(P1_Y);
        target = 0;
        record = 0x470100u;
        test32(rd32(G_PLAYER_MASK), 2);
        if (!flag(R_ZF)) {
            /* both play: the nearer one to the point, AX the y and BX the x */
            const uint16_t py = (uint16_t)(EAX - 0x24), px = (uint16_t)(EBX - 0x20);
            const int32_t dy1 = (int16_t)(uint16_t)(py - y), dx1 = (int16_t)(uint16_t)(px - x);
            const int32_t dy2 = (int16_t)(uint16_t)(py - rd16(p2_y)), dx2 = (int16_t)(uint16_t)(px - rd16(p2_x));
            cmp32((uint32_t)(dy1 * dy1 + dx1 * dx1), (uint32_t)(dy2 * dy2 + dx2 * dx2));
            if (cond_a()) {
                x = rd16(p2_x);
                y = rd16(p2_y);
                record = player_field(1, 0x470100u);
                target = 4;
            }
        }
    }
    wr32(G_TARGET_PLAYER, target);
    wr16(G_TARGET_X, x);
    wr16(G_TARGET_Y, y);
    wr32(G_TARGET_RECORD, record);
    x86_ret();
}

/*
 * Boss2_StartArmMoves_q (0x42b030): every arm starts retracting, to take the
 * next path from the four dwords at ESI (one per arm) and extend again by EBX
 * per frame up to ECX. ESI ends after the list, EAX = the last dword.
 */
HAND_FN(42b030)
{
    uint32_t esi = ESI, eax = 0;
    for (unsigned arm = 0; arm < BOSS2_ARMS; arm++) {
        const uint32_t reach = boss2_arm(arm, ARM_REACH);
        eax = rd32(esi);
        esi += 4;
        wr16(reach + REACH_RETRACT, 1);
        wr16(reach + REACH_EXTEND, 0);
        wr32(reach + REACH_NEXT_PATH, eax);
        wr32(reach + REACH_STEP, EBX);
        wr32(reach + REACH_LIMIT, ECX);
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
    uint32_t esi = ESI, edi = EDI + rd32(G_ARENA_BASE);
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
    uint32_t edi = rd16(0x4b0640u + 2 * rd32(0x4b0624u)) + EAX + rd32(G_ARENA_BASE);
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
 * Boss3_StepPhase_q (0x42d1a0): the level-3 boss's phases. The countdown to the
 * next phase drops by an amount its damage stage chooses (4 at stage 14 and up
 * ... 0x17 at stage 1, 0x18 at 0: the more damaged, the faster). When it runs
 * out and none of its attacks is running, the next phase is taken from the list
 * (countdown, mode; mode 4 lets the attacks start), the phase timer is set to
 * 180 frames and the dword at 0x4983c4 (meaning unknown) to 0x20000; when the
 * timer then runs out with the attack k_boss3_attack[1] idle, that dword is set
 * to 0x18000.
 */
HAND_FN(42d1a0)
{
    static const uint8_t amount[14] = {4, 6, 8, 0xa, 0xc, 0xe, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17};
    uint32_t eax = 4, esi = ESI;
    unsigned i = 0;
    for (; i < 14; i++) {
        eax = amount[i];
        cmp16(rd16(BOSS3_STAGE), (uint16_t)(0xe - i));
        if (cond_ae()) break;
    }
    if (i == 14) eax = 0x18;
    const uint32_t left = sub32(rd32(BOSS3_PHASE_LEFT), eax);
    wr32(BOSS3_PHASE_LEFT, left);
    if (!flag(R_ZF) && flag(R_SF) == flag(R_OF)) goto timer;       /* JG: not yet */
    for (unsigned k = 0; k < 5; k++) {
        cmp16(rd16(k_boss3_attack[k]), 0);
        if (!flag(R_ZF)) goto timer;                               /* an attack is running */
    }
    esi = rd32(BOSS3_PHASE_LIST);
    eax = rd32(esi);
    wr32(BOSS3_PHASE_LEFT, eax);
    eax = rd32(esi + 4);
    wr32(BOSS3_PHASE_MODE, eax);
    esi += 8;
    wr32(BOSS3_PHASE_LIST, esi);
    wr16(BOSS3_PHASE_TIMER, 0xb4);
    wr32(0x4983c4u, 0x20000);
timer:
    cmp16(rd16(BOSS3_PHASE_TIMER), 0);
    if (!flag(R_ZF)) {
        wr16(BOSS3_PHASE_TIMER, dec16(rd16(BOSS3_PHASE_TIMER)));
        if (flag(R_ZF)) {
            cmp16(rd16(k_boss3_attack[1]), 0);
            if (flag(R_ZF)) wr32(0x4983c4u, 0x18000);
        }
    }
    WR_REG(R_EAX, 4, eax);
    WR_REG(R_ESI, 4, esi);
    x86_ret();
}

/*
 * Core_PlayerDistance (0x41b494): the distance from the point (AX, BX) to the
 * player Core_PickNearerPlayer chooses (G_TARGET_X/Y offset by 0x1a and 0x24,
 * and the scroll G_SCROLL_X; AX is the point's y, BX its x): returns CF = 1 when the
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
    uint16_t cx = rd16(G_TARGET_Y), dx = rd16(G_TARGET_X);
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
    dx = add16(dx, rd16(G_SCROLL_X));
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
