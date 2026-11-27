/*
 * Entries 23-25 of player 1's level-script command table (0x471014): each sets a flag
 * that makes the next Player_Hit (0x433bd3) keep one upgrade level instead of
 * lowering it (0x47009e protects 0x47019e, 0x4700a0 protects 0x47005a, 0x4700a2
 * protects 0x47005c); entries 26-28 clear them.
 */
#include "core.h"

HAND_FN(42414a) { wr16(0x0047009e, 0x1); x86_ret(); }
HAND_FN(424154) { wr16(0x004700a0, 0x1); x86_ret(); }
HAND_FN(42415e) { wr16(0x004700a2, 0x1); x86_ret(); }
