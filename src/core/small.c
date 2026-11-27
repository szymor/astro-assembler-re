/* Small routines: table set-up, joystick polls, swaps and fills. */
#include "core.h"

/* XOR r, r: the register becomes 0; CF = OF = SF = 0, ZF = PF = 1. */
static void flags_xor_self(void)
{
    set_flag(R_CF, false); set_flag(R_OF, false); set_flag(R_SF, false);
    set_flag(R_ZF, true); set_flag(0x202, true);
}

/* 408cc2: the pointers and sizes of the first command's ranges. */
HAND_FN(408cc2)
{
    wr32(0x556ad4, 0x408c44);
    wr32(0x556ad8, 0x3f163);
    wr32(0x556adc, 0x46ff3c);
    wr32(0x556ae0, 0xbd274);
    x86_ret();
}

/* 40b81c: 0x40 dwords from 55cab0 cleared. */
HAND_FN(40b81c)
{
    WR_REG(R_EDI, 4, 0x55cab0);
    WR_REG(R_EAX, 4, 0);
    flags_xor_self();
    WR_REG(R_ECX, 4, 0x40);
    fill_dwords(0x55cab0, 0, 0x40);
    WR_REG(R_ECX, 4, 0);
    WR_REG(R_EDI, 4, 0x55cab0 + 0x100);
    x86_ret();
}

/* 41d288: 0x3e80 dwords from EDI cleared. */
HAND_FN(41d288)
{
    const uint32_t edi = EDI;
    WR_REG(R_EAX, 4, 0);
    flags_xor_self();
    fill_dwords(edi, 0, 0x3e80);
    WR_REG(R_ECX, 4, 0);
    WR_REG(R_EDI, 4, edi + 0x3e80 * 4);
    x86_ret();
}

/* 41d292: 200 rows of 80 dwords copied from ESI to EDI; ESI skips 80 bytes
 * after each row. Flags: those of the last DEC EBX. */
HAND_FN(41d292)
{
    uint32_t esi = ESI, edi = EDI, ebx = 0xc8;
    do {
        copy_forward(edi, esi, 0x140);
        edi += 0x140;
        esi = add32(esi + 0x140, 0x50);
        ebx = dec32(ebx);
    } while (!flag(R_ZF));
    WR_REG(R_EBX, 4, ebx);
    WR_REG(R_ECX, 4, 0);
    WR_REG(R_ESI, 4, esi);
    WR_REG(R_EDI, 4, edi);
    x86_ret();
}

/* 416c5d: the first joystick poll: stick 0, slot 0 go into 556aec / 556af0,
 * then the shell callback 40108c runs. (The other three polls are not called
 * in any recording.) */
HAND_FN(416c5d)
{
    wr32(0x556aec, 0);
    wr32(0x556af0, 0);
    wr32(0x46ffb0, 0x40108c);
    HAND_CALL(f_408cb5, 0x416c80u);
    x86_ret();
}

/* 41f0e1: 4b1f88 = 4b1fb8 + 4b1fc2; 4b1f9a = 0x50; 4b1f9c = 0. */
HAND_FN(41f0e1)
{
    const uint16_t dx = add16(rd16(0x4b1fb8), rd16(0x4b1fc2));
    WR_REG(R_EDX, 2, dx);
    wr16(0x4b1f88, dx);
    wr16(G_SCROLL_X, 0x50);
    wr16(0x4b1f9c, 0);
    x86_ret();
}
