/*
 * Core functions rewritten by hand (the .c files in src/core), as an X-macro
 * list:
 *
 *   HAND(<address, lowercase hex>, <name>)
 *
 * The recompiler (re/recomp/recomp.py --replaced, run by CMake) generates
 * these as ref_<address> instead of f_<address>, so every call reaches the
 * hand-written version. In differential builds (-DASTRO_DIFF=ON) every call
 * runs both and compares them (recomp_rt.h, rt_diff_check); the regression
 * tests then check each one on real game states.
 */
HAND(408f8c, Core_Random)
HAND(442454, Core_DrawSprite)
HAND(4424f0, Core_DrawSpriteClipped)
HAND(4425dc, Core_DrawSilhouette)
