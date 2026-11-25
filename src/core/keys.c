/*
 * Binding of the keys: the six entries of a configuration list are looked up
 * in a table of key codes (0x55cab0) and stored as words in a player's key
 * fields. Eight entry points share two bodies: they differ only in where the
 * list starts (the original jumps into the common code).
 */
#include "core.h"

/* ESI = list start; the words go to dest[0..5], from table[list[i]]. Leaves
 * ESI after the list, EDI = the table, EAX = the last code, the flags of
 * XOR EAX, EAX. */
static void bind_keys(uint32_t esi, const uint32_t dest[6])
{
    const uint32_t table = 0x55cab0u;
    uint32_t eax = 0;
    for (unsigned i = 0; i < 6; i++) {
        eax = rd8(table + rd8(esi + i));
        wr16(dest[i], (uint16_t)eax);
    }
    flags_szp32(0);
    set_flag(R_CF, false);
    set_flag(R_OF, false);
    WR_REG(R_ESI, 4, esi + 6);
    WR_REG(R_EDI, 4, table);
    WR_REG(R_EAX, 4, eax);
    x86_ret();
}

static const uint32_t k_dest_p1[6] = {0x55c984, 0x55c980, 0x55c988, 0x55c98c, 0x55c990, 0x55c994};
static const uint32_t k_dest_p2[6] = {0x55c99c, 0x55c998, 0x55c9a0, 0x55c9a4, 0x55c9a8, 0x55c9ac};

/* Player 1's keys from the lists at 0x46f450, 0x46f456, 0x46f45c and 0x46f462
 * (0x416ba1, 0x416ba8, 0x416baf, 0x416bb6) and player 2's (0x416bff, 0x416c06,
 * 0x416c0d, 0x416c14). */
HAND_FN(416ba1) { bind_keys(0x46f450u, k_dest_p1); }
HAND_FN(416ba8) { bind_keys(0x46f456u, k_dest_p1); }
HAND_FN(416baf) { bind_keys(0x46f45cu, k_dest_p1); }
HAND_FN(416bb6) { bind_keys(0x46f462u, k_dest_p1); }
HAND_FN(416bff) { bind_keys(0x46f450u, k_dest_p2); }
HAND_FN(416c06) { bind_keys(0x46f456u, k_dest_p2); }
HAND_FN(416c0d) { bind_keys(0x46f45cu, k_dest_p2); }
HAND_FN(416c14) { bind_keys(0x46f462u, k_dest_p2); }
