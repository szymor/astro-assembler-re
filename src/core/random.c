/* Random numbers. */
#include "core.h"

#define G_RANDOM_SEED  0x46ffa6u
#define G_LEVEL_TIME   0x46ffa0u   /* frames since the level started */
#define G_P1_X         0x470164u
#define G_P1_Y         0x470166u
#define G_P1_SCORE     0x470050u   /* low word */
#define G_P2_X         0x4702bcu   /* player 2's fields: player 1's + 0x158 */
#define G_P2_Y         0x4702beu
#define G_P2_SCORE     0x4701a8u

/*
 * Core_Random (0x408f8c): the next 16-bit random number, in AX and as the new
 * seed. The seed is multiplied by 13 and rotated right by 2, then the players'
 * positions, the level time and the scores are added, so the sequence depends
 * on how the game is played (the reason recordings must replay every input).
 *
 * Leaves the flags of the last addition and the upper half of EAX unchanged.
 */
HAND_FN(408f8c)
{
    static const uint32_t mixed_in[] = {G_P1_X, G_P1_Y, G_P2_X, G_P2_Y, G_LEVEL_TIME, G_P1_SCORE, G_P2_SCORE};
    uint16_t x = ror16((uint16_t)(rd16(G_RANDOM_SEED) * 13u), 2);
    for (unsigned i = 0; i < sizeof mixed_in / sizeof mixed_in[0]; i++) x = add16(x, rd16(mixed_in[i]));
    set_ax(x);
    wr16(G_RANDOM_SEED, x);
    x86_ret();
}
