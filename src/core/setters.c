/* Setters: a record store and RET (script command handlers). */
#include "core.h"

HAND_FN(42414a) { wr16(0x0047009e, 0x1); x86_ret(); }
HAND_FN(424154) { wr16(0x004700a0, 0x1); x86_ret(); }
HAND_FN(42415e) { wr16(0x004700a2, 0x1); x86_ret(); }
