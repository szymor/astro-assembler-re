/*
 * Small routines that read a table entry or start an object, written as
 * translations of the original register use (see core.h).
 */
#include "core.h"

/*
 * Core_GetLevelScript (0x408cf1): EAX = LevelScriptTable[level - 1][section * 3
 * + difficulty], with EDX = level (1..6) and EAX = section. The table at
 * 0x4c1da4 holds one pointer per level; each points to the level's script
 * variants, three per section (g_cfgDifficulty selects one).
 *
 * EDX ends as the level's table pointer. Flags: those of the final addition
 * of the difficulty.
 */
HAND_FN(408cf1)
{
    const uint32_t level = rd32(0x4c1da4u + 4u * (EDX - 1));
    const uint32_t index = add32(EAX * 3u, rd32(0x556abcu));
    WR_REG(R_EDX, 4, level);
    WR_REG(R_EAX, 4, rd32(level + 4u * index));
    x86_ret();
}

/*
 * Core_StartObject28 (0x41bea9): takes the first free entry (first word zero)
 * of the table of 28-byte entries after 0x520220 and fills it in: in-use word 1;
 * ECX and EAX as dwords at +2 and +6; zero at +0x12; the words at 0x46ff42,
 * BX, DX and 0x46ff44 at +0x14..+0x1a; and from the table at 0x1f4f74 (indexed
 * by EDI plus the dword at 0x556ac8) two sign-extended words at +0xe and +0xa.
 * The count word at 0x4b1b28 goes up. EAX, EBX are left as those two values and
 * EDI as the offset; ESI is preserved. Flags: those of the addition to EDI.
 */
HAND_FN(41bea9)
{
    wr16(0x4b1b28u, inc16(rd16(0x4b1b28u)));
    uint32_t e = 0x520220u;
    do e += 0x1c; while (rd16(e) != 0);
    wr16(e, 1);
    wr32(e + 2, ECX);
    wr32(e + 6, EAX);
    wr16(e + 0x12, 0);
    wr16(e + 0x14, rd16(0x46ff42u));
    wr16(e + 0x16, (uint16_t)EBX);
    wr16(e + 0x18, (uint16_t)EDX);
    wr16(e + 0x1a, rd16(0x46ff44u));
    const uint32_t edi = add32(EDI, rd32(0x556ac8u));
    const uint32_t eax = (uint32_t)(int32_t)(int16_t)rd16(edi + 0x1f4f74u);
    const uint32_t ebx = (uint32_t)(int32_t)(int16_t)rd16(edi + 0x1f4f76u);
    wr32(e + 0xe, eax);
    wr32(e + 0xa, ebx);
    WR_REG(R_EDI, 4, edi);
    WR_REG(R_EAX, 4, eax);
    WR_REG(R_EBX, 4, ebx);
    x86_ret();
}
