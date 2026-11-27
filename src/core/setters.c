/*
 * Entries 23-25 of player 1's level-script command table (0x471014): each sets a flag
 * that makes the next Player_Hit (0x433bd3) keep one upgrade level instead of
 * lowering it (P1_PROTECT_SPEED..C for P1_SPEED..C, see game.h); entries 26-28
 * clear them.
 */
#include "core.h"

HAND_FN(42414a) { wr16(P1_PROTECT_SPEED, 1); x86_ret(); }
HAND_FN(424154) { wr16(P1_PROTECT_WEAPON, 1); x86_ret(); }
HAND_FN(42415e) { wr16(P1_PROTECT_WEAPON2, 1); x86_ret(); }
