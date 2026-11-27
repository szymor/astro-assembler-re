/*
 * Named views of the game state that the hand-written core functions use:
 * the emulated memory addresses of fields whose meaning is known (see
 * docs/modules.md, "Game logic", and re/symbols.tsv). Fields still unknown
 * stay as plain addresses in the code, with a comment where it helps.
 *
 * Coordinates: x and y are 16-bit words in half pixels of a playfield 400
 * pixels wide; the view scrolls sideways over it by G_SCROLL_X.
 */
#pragma once
#include <stdint.h>

/* ---- the frame ------------------------------------------------------------- */
#define G_GAME_TIME     0x46ffa0u   /* dword: gameplay frames since the game began (not reset per level) */
#define G_RANDOM_SEED   0x46ffa6u   /* word: Core_Random's state */
#define G_SCROLL_X      0x4b1f9au   /* word: horizontal scroll of the view over the playfield */
#define G_ARENA_BASE    0x556ac8u   /* dword: the level data's arena (also in astro.h) */
#define G_PLAYER_MASK   0x46ffe4u   /* dword: bit 0 player 1 takes part, bit 1 player 2 */

/* ---- players ------------------------------------------------------------------
 * Player 2's record is player 1's plus PLAYER_STRIDE; the P1_ names are player
 * 1's fields and player_field() moves one to the given player (0 or 1). */
#define PLAYER_STRIDE   0x158u
static inline uint32_t player_field(unsigned player, uint32_t p1_field) { return p1_field + player * PLAYER_STRIDE; }

#define P1_ENERGY_MAX   0x47004cu   /* byte: energy cap, grows by one per pickup up to 0x20 (tentative) */
#define P1_ENERGY       0x47004eu   /* byte: energy (8/16/24 assemble the special ships) */
#define P1_LIVES        0x47004fu   /* byte */
#define P1_SCORE        0x470050u   /* dword */
#define P1_LEVEL_B      0x47005au   /* byte: an upgrade level (1..), raised every third pickup */
#define P1_LEVEL_B_STEP 0x47005bu   /* byte: pickups towards the next P1_LEVEL_B (0..2) */
#define P1_LEVEL_C      0x47005cu   /* byte: an upgrade level (0..) */
#define P1_PROTECT_A    0x47009eu   /* word: keep P1_LEVEL_A on the next hit (script command 23) */
#define P1_PROTECT_B    0x4700a0u   /* word: keep P1_LEVEL_B (command 24) */
#define P1_PROTECT_C    0x4700a2u   /* word: keep P1_LEVEL_C (command 25) */
#define P1_INVULNERABLE 0x4700a4u   /* word: frames of invulnerability left (140 after a hit) */
#define P1_SHIP_TYPE    0x4700f2u   /* byte: 0 main ship, 1-9 ALPHA .. RAIDEN */
#define P1_X            0x470164u   /* word */
#define P1_Y            0x470166u   /* word */
#define P1_SHIP_RECORD  0x470168u   /* 0x20 bytes: the current ship type's record (Core_CopyRecord1) */
#define P1_LEVEL_A      0x47019eu   /* byte: an upgrade level (2..) */
#define P1_CONTROLS     0x55c980u   /* six words 4 bytes apart: player 1's controls (Core_BindKeys*) */
#define P2_CONTROLS     0x55c998u
#define G_KEY_HISTORY   0x55cab0u   /* 256 bytes: per key, the 2-bit history (previous, current) */
#define G_KEY_LAYOUTS   0x46f450u   /* four lists of six key codes, one per control layout */

/* The player the enemies aim at (Core_PickNearerPlayer). */
#define G_TARGET_X      0x46fffcu   /* word */
#define G_TARGET_Y      0x46fffeu   /* word */
#define G_TARGET_RECORD 0x470000u   /* dword: a pointer into that player's record */
#define G_TARGET_PLAYER 0x470004u   /* dword: 0 player 1, 4 player 2 */

/* ---- the hall of fame -------------------------------------------------------- */
#define G_HALL_OF_FAME  0x46f498u   /* 8 entries of 8 bytes: name, score (dword at +4), best first */

/* ---- bosses -------------------------------------------------------------------
 * The level-2 and level-3 bosses move their parts the same way: each part has a
 * path record (offset of a path in the arena, position in it, scale) followed
 * by a reach record whose value is that scale. A part retracts to reach 0, takes
 * its next path, and extends again to its limit (Boss2_StepArmReach_q). */
#define PATH_OFFSET     0x00u       /* dword: the path's offset in the arena, 0 = none */
#define PATH_POS        0x04u       /* dword: byte position in the path */
#define PATH_SCALE      0x08u       /* dword: = the reach record's value */
#define REACH_VALUE     0x00u       /* dword */
#define REACH_RETRACT   0x04u       /* word: non-zero while retracting */
#define REACH_EXTEND    0x06u       /* word: non-zero while extending */
#define REACH_NEXT_PATH 0x08u       /* dword: the path taken at reach 0 */
#define REACH_STEP      0x0cu       /* dword */
#define REACH_LIMIT     0x10u       /* dword: full reach */
#define REACH_FROM_PATH 0x08u       /* a part's reach record follows its path record */

/* Level-2 boss: a body with four arms. */
#define BOSS2_HEALTH       0x497ae0u   /* word: 0 = not present */
#define BOSS2_HEALTH_STEP  0x497ae2u   /* word: health per damage stage */
#define BOSS2_TIME         0x497ae4u   /* dword: frames since it appeared */
#define BOSS2_DRIFT_LEFT   0x497aecu   /* dword: frames left of the current drift */
#define BOSS2_DRIFT_LIST   0x497ae8u   /* dword: the next (frames, dx, dy) entry */
#define BOSS2_DRIFT_DX     0x497af0u   /* dword */
#define BOSS2_DRIFT_DY     0x497af4u   /* dword */
#define BOSS2_ORIGIN_X     0x497af8u   /* dword: the drifting origin of its path */
#define BOSS2_ORIGIN_Y     0x497afcu
#define BOSS2_POS_X        0x497b00u   /* dword: its position (origin + path point) */
#define BOSS2_POS_Y        0x497b04u
#define BOSS2_PATH         0x497c34u   /* path record (PATH_*) */
#define BOSS2_SPEED        0x497c3cu   /* dword: path speed (16.16 bytes per frame) */
#define BOSS2_SPEED_TARGET 0x497c40u
#define BOSS2_SPEED_STEP   0x497c44u
#define BOSS2_FRAME        0x498108u   /* dword: the body's animation frame (an offset) */
#define BOSS2_FRAME_TICK   0x49810cu   /* word: 0 = no sequence running */
#define BOSS2_FRAME_LIST   0x498110u   /* dword: cursor into the frame list at BOSS2_FRAME_LIST_BASE */
#define BOSS2_FRAME_LIST_BASE 0x498114u
#define BOSS2_FLASH_TIMER  0x49813cu   /* dword: the body's hit flash (counts down by 4) */
#define BOSS2_ARMS         4
#define BOSS2_ARM_STRIDE   0xb0u
static inline uint32_t boss2_arm(unsigned arm, uint32_t field) { return 0x497d58u + arm * BOSS2_ARM_STRIDE + field; }
#define ARM_ACTIVE         0x00u       /* word */
#define ARM_FLASH_TIMER    0x08u       /* dword: hit flash, counts down by 4 */
#define ARM_FRAME          0x56u       /* dword: animation frame (12-byte steps, 204 down to 0) */
#define ARM_PATH           0x5au       /* path record (PATH_*) */
#define ARM_REACH          0x62u       /* reach record (REACH_*) */

/* Level-3 boss: the robot, with five moving parts. */
#define BOSS3_HEALTH       0x4983a0u   /* word: 0 = not present */
#define BOSS3_HEALTH_STEP  0x4983a2u
#define BOSS3_TIME         0x4983a4u
#define BOSS3_ORIGIN_X     0x4983a8u   /* dword */
#define BOSS3_ORIGIN_Y     0x4983acu
#define BOSS3_POS_X        0x4983b0u   /* dword: origin + path point */
#define BOSS3_POS_Y        0x4983b4u
#define BOSS3_PHASE_LIST   0x4983c8u   /* dword: the next (frames, mode) entry */
#define BOSS3_PHASE_LEFT   0x4983ccu   /* dword: countdown to the next phase */
#define BOSS3_PHASE_TIMER  0x4983d0u   /* word: 180 frames after a phase starts */
#define BOSS3_PHASE_MODE   0x4983d4u   /* dword: 4 = attacking */
#define BOSS3_PATH         0x498494u   /* path record (PATH_*) */
#define BOSS3_SPEED        0x49849cu
#define BOSS3_SPEED_TARGET 0x4984a0u
#define BOSS3_SPEED_STEP   0x4984a4u
#define BOSS3_PATH_SCALE   0x4984a8u   /* dword: the path's scale */
#define BOSS3_SCALE_TARGET 0x4984acu
#define BOSS3_SCALE_STEP   0x4984b0u
#define BOSS3_STAGE        0x498868u   /* word: damage stage (health / BOSS3_HEALTH_STEP) */
#define BOSS3_PARTS        5
/* Path records of its parts; the reach records follow (REACH_FROM_PATH). */
static const uint32_t k_boss3_part_path[BOSS3_PARTS] = {0x4985fcu, 0x498686u, 0x4986feu, 0x498776u, 0x4987eeu};
/* Its five attacks: a word each, non-zero while that attack runs. */
static const uint32_t k_boss3_attack[5] = {0x4984c4u, 0x498550u, 0x4984d0u, 0x498502u, 0x498536u};

/* ---- enemies ------------------------------------------------------------------
 * The nine enemy classes, in the order the collision routines check them:
 * each a table of `stride`-byte entries (first word non-zero = in use, the
 * first entry at `first`) and a count of entries in use. */
typedef struct { uint32_t first, stride, count; } enemy_class;
static const enemy_class k_enemy_class[9] = {
    {0x527568u, 0x38u, 0x4711e8u}, {0x5278b0u, 0x9au, 0x47125au}, {0x527e20u, 0x98u, 0x479608u},
    {0x528a00u, 0x42u, 0x494010u}, {0x528ff0u, 0x3eu, 0x494560u}, {0x529600u, 0xbeu, 0x49512cu},
    {0x52a014u, 0x74u, 0x495bb8u}, {0x5298f8u, 0xb6u, 0x495588u}, {0x52a2ccu, 0x4eu, 0x495db4u},
};
