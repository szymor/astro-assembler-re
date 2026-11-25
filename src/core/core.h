/*
 * Hand-written replacements for functions of the recompiled assembly core.
 *
 * A replacement must leave exactly what the original instructions left:
 * registers (also the upper halves it does not use), the flags of the last
 * flag-setting instruction, memory, the emulated stack and, through RET, the
 * return address (recomp_rt.h). The differential build checks this on every
 * call. The helpers below produce the x86 effects the generated code
 * produces, in Ghidra's p-code semantics (no AF for additions).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "gen_funcs.h"
#include "recomp_rt.h"

/* Defines the hand-written version of the core function at `addr` (hex,
 * without 0x). Differential builds rename it so a checking wrapper can
 * call both versions (replaced.c). */
#ifdef RT_DIFF
#define HAND_FN(addr) void hand_##addr(void)
#else
#define HAND_FN(addr) void f_##addr(void)
#endif

/* ---- memory and registers ------------------------------------------------- */
static inline uint16_t rd16(uint32_t va) { return (uint16_t)MEM_RD(va, 2); }
static inline void wr16(uint32_t va, uint16_t v) { MEM_WR(va, 2, v); }
static inline void set_ax(uint16_t v) { WR_REG(R_EAX, 2, v); }

static inline void set_flag(unsigned off, bool v) { WR_REG(off, 1, v ? 1 : 0); }
static inline bool parity_even(uint8_t v) { return !(RT_POPCOUNT(v) & 1); }

/* SF, ZF, PF of a 16-bit result. */
static inline void flags_szp16(uint16_t r)
{
    set_flag(R_SF, (r & 0x8000) != 0);
    set_flag(R_ZF, r == 0);
    set_flag(0x202, parity_even((uint8_t)r));          /* PF */
}

/* ADD r16, x: result, and CF, OF, SF, ZF, PF. */
static inline uint16_t add16(uint16_t a, uint16_t b)
{
    const uint16_t r = (uint16_t)(a + b);
    set_flag(R_CF, r < a);
    set_flag(R_OF, ((a ^ r) & (b ^ r) & 0x8000) != 0);
    flags_szp16(r);
    return r;
}

static inline uint16_t ror16(uint16_t v, unsigned n) { n &= 15; return (uint16_t)(v >> n | v << ((16 - n) & 15)); }

/* RET: pop the return address (Ghidra keeps it in its EIP slot, 0x284) and
 * report it to the caller's check (RT_CALL). */
static inline void x86_ret(void)
{
    const uint32_t ra = (uint32_t)MEM_RD(ESP, 4);
    WR_REG(0x284, 4, ra);
    WR_REG(R_ESP, 4, ESP + 4);
    RT_RET(ra);
}

/* Startup check that the generated core was made with the current list of
 * replacements (replaced.h); false with a message otherwise. */
bool core_check_replaced(void);
