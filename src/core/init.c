/*
 * Initialisation of tables and player state: fills, copies and fixed stores.
 * Written as translations of the original register use (see core.h).
 */
#include "core.h"

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
