/*
 * The wrappers that call out of the core (shell callbacks and handler tables)
 * and a few small store routines of the start-up code.
 */
#include "core.h"

/*
 * Core_CallShell (0x408cb5): saves the flags and all registers, calls the
 * routine whose address is in g_coreCallback (0x46ffb0; a shell callback or a
 * core function) and restores them, so the callback may use any register.
 */
HAND_FN(408cb5)
{
    x86_pushf();
    x86_pushad();
    HAND_CALLIND(rd32(0x46ffb0), 0x408cbeu);
    x86_popad();
    x86_popf();
    x86_ret();
}

/*
 * Core_Dispatch (0x408c44): the same around a command routine picked by the
 * index at 0x556ae4 from the table at 0x408c59, with the direction flag clear.
 */
HAND_FN(408c44)
{
    x86_pushf();
    x86_pushad();
    set_flag(0x20a, false);                                  /* CLD */
    const uint32_t esi = rd32(0x556ae4);
    WR_REG(R_ESI, 4, esi);
    HAND_CALLIND(rd32(0x408c59 + esi * 4), 0x408c55u);
    x86_popad();
    x86_popf();
    x86_ret();
}

/* 409639: the CD-volume setting at 46ec0c is copied to both volume fields,
 * then applied through the callback 0x401235. */
HAND_FN(409639)
{
    const uint32_t v = rd32(0x46ec0c);
    WR_REG(R_EAX, 4, v);
    wr32(0x518910, v);
    wr32(0x518914, v);
    wr32(0x46ffb0, 0x401235);
    HAND_CALL(f_408cb5, 0x409657u);
    x86_ret();
}

/*
 * Core_CopyRecord1 / 2 (0x4096ae, 0x4096df): the player's ship record
 * (P1_SHIP_RECORD) is copied from the table of 0x20-byte records at 0x4702fc,
 * slot P1_SHIP_TYPE. Slot 0 (the main ship) is filled in first: its first dword
 * from the dword at P1_LEVEL_A, its pointer field (0x470304) = `tail`.
 */
static void copy_record(unsigned player, uint32_t tail)
{
    const uint32_t first = player_field(player, P1_LEVEL_A), index_at = player_field(player, P1_SHIP_TYPE);
    const uint32_t dest = player_field(player, P1_SHIP_RECORD);
    const uint32_t v = rd32(first);
    WR_REG(R_EAX, 4, v);
    wr32(0x4702fc, v);
    wr32(0x470304, tail);
    const uint32_t idx = rd8(index_at);
    WR_REG(R_EAX, 4, idx);
    const uint32_t esi = add32(idx * 0x20, 0x4702fc);
    copy_forward(dest, esi, 0x20);
    WR_REG(R_ESI, 4, esi + 0x20);
    WR_REG(R_EDI, 4, dest + 0x20);
    WR_REG(R_ECX, 4, 0);
}
HAND_FN(4096ae) { copy_record(0, 0x47043c); x86_ret(); }
HAND_FN(4096df) { copy_record(1, 0x470450); x86_ret(); }

/* 409791: 0x1054 pairs of (0xffffffff, 0) are stored from EDI + the arena base. */
HAND_FN(409791)
{
    uint32_t edi = add32(EDI, rd32(G_ARENA_BASE));
    WR_REG(R_EAX, 4, 0xffffffffu);
    WR_REG(R_EBX, 4, 0);
    uint16_t n = 0x1054;
    do {
        wr32(edi, 0xffffffffu);
        wr32(edi + 4, 0);
        edi = add32(edi, 8);
        n = dec16(n);
    } while (!flag(R_ZF));
    WR_REG(R_ECX, 2, 0);
    WR_REG(R_EDI, 4, edi);
    x86_ret();
}
