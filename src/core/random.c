/* Random numbers. */
#include "core.h"

/*
 * Core_Random (0x408f8c): the next 16-bit random number, in AX and as the new
 * seed. The seed is multiplied by 13 and rotated right by 2, then both players'
 * positions, the game time and both scores are added, so the sequence depends
 * on how the game is played (the reason recordings must replay every input).
 *
 * Leaves the flags of the last addition and the upper half of EAX unchanged.
 */
HAND_FN(408f8c)
{
    static const uint32_t mixed_in[] = {P1_X, P1_Y, P1_X + PLAYER_STRIDE, P1_Y + PLAYER_STRIDE,
                                        G_GAME_TIME, P1_SCORE, P1_SCORE + PLAYER_STRIDE};
    uint16_t x = ror16((uint16_t)(rd16(G_RANDOM_SEED) * 13u), 2);
    for (unsigned i = 0; i < sizeof mixed_in / sizeof mixed_in[0]; i++) x = add16(x, rd16(mixed_in[i]));
    set_ax(x);
    wr16(G_RANDOM_SEED, x);
    x86_ret();
}
