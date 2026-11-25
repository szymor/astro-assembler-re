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
HAND(4137f4, Core_AddEntry8)
HAND(41c033, Core_FindFreeSlot200)
HAND(41cb30, Core_FindFreeSlot30)
HAND(42be97, Core_WrapPosition)
HAND(41c27d, Core_FindFreeSlot100)
HAND(41c9b2, Core_FindFreeSlot25)
HAND(41cdb2, Core_FindFreeSlot10)
HAND(41cf6b, Core_WrapIndex)
HAND(42beb7, Core_FineToIndex)
HAND(408cf1, Core_GetLevelScript)
HAND(41bea9, Core_StartObject28)
HAND(4435ec, Core_AddVectors)
HAND(44359c, Core_StepAngles)
HAND(443554, Core_NormaliseVectors)
HAND(41b54b, Core_WrapAngle16)
HAND(423f3f, Core_StartTimedEntry)
HAND(41c49a, Core_FindFreeSlot50)
HAND(42be54, Core_SplitFixed)
HAND(409658, Core_StoreFrameConstants)
HAND(409e91, Core_InitPlayer1Ship)
HAND(409f04, Core_InitPlayer2Ship)
HAND(445a93, Core_ClearTables)
HAND(40982e, Core_FillZeroThenOnes)
HAND(409818, Core_CopyRows)
HAND(41851f, Core_SetBestTime1)
HAND(418559, Core_SetBestTime2)
