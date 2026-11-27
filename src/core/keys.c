/*
 * Reading the controls from the keyboard (Core_BindKeys*): each of the six
 * keys of one of the four layouts (G_KEY_LAYOUTS, six key codes each) has its
 * 2-bit history looked up in G_KEY_HISTORY and stored in the player's control
 * words (P1_CONTROLS / P2_CONTROLS). Eight entry points share the body: they
 * differ only in the layout (the original jumps into the common code).
 */
#include "core.h"

/* ESI = the layout's list; dest[i] = the history of key list[i]. Leaves ESI
 * after the list, EDI = the history table, EAX = the last history, the flags
 * of XOR EAX, EAX. */
static void bind_keys(uint32_t esi, const uint32_t dest[6])
{
    const uint32_t table = G_KEY_HISTORY;
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

/* Where the six keys of a layout list go: the second and first controls swap. */
static const uint32_t k_dest_p1[6] = {P1_CONTROLS + 4, P1_CONTROLS, P1_CONTROLS + 8, P1_CONTROLS + 0xc, P1_CONTROLS + 0x10, P1_CONTROLS + 0x14};
static const uint32_t k_dest_p2[6] = {P2_CONTROLS + 4, P2_CONTROLS, P2_CONTROLS + 8, P2_CONTROLS + 0xc, P2_CONTROLS + 0x10, P2_CONTROLS + 0x14};

/* Player 1's keys from the lists at 0x46f450, 0x46f456, 0x46f45c and 0x46f462
 * (0x416ba1, 0x416ba8, 0x416baf, 0x416bb6) and player 2's (0x416bff, 0x416c06,
 * 0x416c0d, 0x416c14). */
HAND_FN(416ba1) { bind_keys(G_KEY_LAYOUTS + 0 * 6, k_dest_p1); }
HAND_FN(416ba8) { bind_keys(G_KEY_LAYOUTS + 1 * 6, k_dest_p1); }
HAND_FN(416baf) { bind_keys(G_KEY_LAYOUTS + 2 * 6, k_dest_p1); }
HAND_FN(416bb6) { bind_keys(G_KEY_LAYOUTS + 3 * 6, k_dest_p1); }
HAND_FN(416bff) { bind_keys(G_KEY_LAYOUTS + 0 * 6, k_dest_p2); }
HAND_FN(416c06) { bind_keys(G_KEY_LAYOUTS + 1 * 6, k_dest_p2); }
HAND_FN(416c0d) { bind_keys(G_KEY_LAYOUTS + 2 * 6, k_dest_p2); }
HAND_FN(416c14) { bind_keys(G_KEY_LAYOUTS + 3 * 6, k_dest_p2); }
