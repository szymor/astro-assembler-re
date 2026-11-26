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
#ifndef ASTRO_HAVE_RD32                              /* also defined by astro.h */
static inline uint32_t rd32(uint32_t va) { return (uint32_t)MEM_RD(va, 4); }
static inline void wr32(uint32_t va, uint32_t v) { MEM_WR(va, 4, v); }
#endif
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

/* 32-bit counterparts: SF, ZF, PF of a result, ADD and CMP/SUB. */
static inline void flags_szp32(uint32_t r)
{
    set_flag(R_SF, (r & 0x80000000u) != 0);
    set_flag(R_ZF, r == 0);
    set_flag(0x202, parity_even((uint8_t)r));          /* PF */
}
static inline uint32_t add32(uint32_t a, uint32_t b)
{
    const uint32_t r = a + b;
    set_flag(R_CF, r < a);
    set_flag(R_OF, ((a ^ r) & (b ^ r) & 0x80000000u) != 0);
    flags_szp32(r);
    return r;
}
static inline uint32_t sub32(uint32_t a, uint32_t b)
{
    const uint32_t r = a - b;
    set_flag(R_CF, a < b);
    set_flag(R_OF, ((a ^ b) & (a ^ r) & 0x80000000u) != 0);
    flags_szp32(r);
    return r;
}
/* 8-bit counterparts: SF, ZF, PF, CMP/SUB and INC. */
static inline void flags_szp8(uint8_t r)
{
    set_flag(R_SF, (r & 0x80) != 0);
    set_flag(R_ZF, r == 0);
    set_flag(0x202, parity_even(r));                   /* PF */
}
static inline uint8_t sub8(uint8_t a, uint8_t b)
{
    const uint8_t r = (uint8_t)(a - b);
    set_flag(R_CF, a < b);
    set_flag(R_OF, ((a ^ b) & (a ^ r) & 0x80) != 0);
    flags_szp8(r);
    return r;
}
#define cmp8(a, b) ((void)sub8((a), (b)))
static inline uint8_t inc8(uint8_t a)
{
    const uint8_t r = (uint8_t)(a + 1);
    set_flag(R_OF, a == 0x7f);
    flags_szp8(r);
    return r;
}
static inline uint8_t rd8(uint32_t va) { return (uint8_t)MEM_RD(va, 1); }
static inline void wr8(uint32_t va, uint8_t v) { MEM_WR(va, 1, v); }

/* OR r8, r8 / XOR-style result flags of an 8-bit value: CF = OF = 0, SF, ZF, PF. */
static inline void test8(uint8_t a)
{
    set_flag(R_CF, false);
    set_flag(R_OF, false);
    flags_szp8(a);
}
/* DEC r16: OF, SF, ZF, PF; CF is left alone. */
static inline uint16_t dec16(uint16_t a)
{
    const uint16_t r = (uint16_t)(a - 1);
    set_flag(R_OF, a == 0x8000);
    flags_szp16(r);
    return r;
}

/* SAR r32, n for n >= 2: CF = the last bit shifted out, SF, ZF, PF; OF stays as it was. */
static inline uint32_t sar32(uint32_t a, unsigned n)
{
    set_flag(R_CF, (((int32_t)a >> (n - 1)) & 1) != 0);
    const uint32_t r = (uint32_t)((int32_t)a >> n);
    flags_szp32(r);
    return r;
}

/* TEST r32, mask / TEST r16, mask: CF = OF = 0, SF, ZF, PF of the AND. */
static inline void test32(uint32_t a, uint32_t mask)
{
    set_flag(R_CF, false);
    set_flag(R_OF, false);
    flags_szp32(a & mask);
}
static inline void test16m(uint16_t a, uint16_t mask)
{
    set_flag(R_CF, false);
    set_flag(R_OF, false);
    flags_szp16((uint16_t)(a & mask));
}

/* DEC r32: OF, SF, ZF, PF; CF is left alone. */
static inline uint32_t dec32(uint32_t a)
{
    const uint32_t r = a - 1;
    set_flag(R_OF, a == 0x80000000u);
    flags_szp32(r);
    return r;
}
#define cmp32(a, b) ((void)sub32((a), (b)))

static inline uint16_t ror16(uint16_t v, unsigned n) { n &= 15; return (uint16_t)(v >> n | v << ((16 - n) & 15)); }

/* CMP/SUB r16, x: result, and CF, OF, SF, ZF, PF. */
static inline uint16_t sub16(uint16_t a, uint16_t b)
{
    const uint16_t r = (uint16_t)(a - b);
    set_flag(R_CF, a < b);
    set_flag(R_OF, ((a ^ b) & (a ^ r) & 0x8000) != 0);
    flags_szp16(r);
    return r;
}
#define cmp16(a, b) ((void)sub16((a), (b)))
/* NEG r16: CF = (a != 0), the other flags as for 0 - a. */
static inline uint16_t neg16(uint16_t a) { return sub16(0, a); }

/* INC r16: OF, SF, ZF, PF; CF is left alone. */
static inline uint16_t inc16(uint16_t a)
{
    const uint16_t r = (uint16_t)(a + 1);
    set_flag(R_OF, a == 0x7fff);
    flags_szp16(r);
    return r;
}

/* OR r16, r16 (the "test for sign" idiom): CF = OF = 0, SF, ZF, PF. */
static inline void test16(uint16_t a)
{
    set_flag(R_CF, false);
    set_flag(R_OF, false);
    flags_szp16(a);
}

/* Conditions of the last flag-setting instruction. */
static inline bool flag(unsigned off) { return RD_REG(off, 1) != 0; }
static inline bool cond_ge(void) { return flag(R_SF) == flag(R_OF); }          /* JGE */
static inline bool cond_l(void) { return flag(R_SF) != flag(R_OF); }           /* JL  */
static inline bool cond_le(void) { return flag(R_ZF) || cond_l(); }            /* JLE */
static inline bool cond_ae(void) { return !flag(R_CF); }                       /* JAE */
static inline bool cond_a(void) { return !flag(R_CF) && !flag(R_ZF); }         /* JA  */

/* REP MOVSB/MOVSD with the direction flag clear (the core sets it only around
 * one copy and clears it right after): n bytes from src to dst, front to
 * back. Writes go through the differential build's recording. */
static inline void copy_forward(uint32_t dst, uint32_t src, uint32_t n)
{
    if (!n) return;
    if ((uint64_t)src + n > RT_MEM_SIZE) rt_bad_access(src, (int)n);
    if ((uint64_t)dst + n > RT_MEM_SIZE) rt_bad_access(dst, -(int)n);
#ifdef RT_DIFF
    if (rt_diff_active) rt_diff_note_range(dst, n);
#endif
    if (dst > src && dst < src + n) {
        for (uint32_t i = 0; i < n; i++) mem[dst + i] = mem[src + i];   /* overlapping: as REP MOVSB does */
    } else {
        memcpy(&mem[dst], &mem[src], n);
    }
}

/* REP STOSB: n bytes of `value` from dst on (direction flag clear). */
static inline void fill_bytes(uint32_t dst, uint8_t value, uint32_t n)
{
    if (!n) return;
    if ((uint64_t)dst + n > RT_MEM_SIZE) rt_bad_access(dst, -(int)n);
#ifdef RT_DIFF
    if (rt_diff_active) rt_diff_note_range(dst, n);
#endif
    memset(&mem[dst], value, n);
}

/* REP STOSD: n dwords of `value` (little-endian) from dst on. */
static inline void fill_dwords(uint32_t dst, uint32_t value, uint32_t n)
{
    if (!n) return;
    if ((uint64_t)dst + 4ull * n > RT_MEM_SIZE) rt_bad_access(dst, -(int)(4 * n));
#ifdef RT_DIFF
    if (rt_diff_active) rt_diff_note_range(dst, 4 * n);
#endif
    for (uint32_t i = 0; i < n; i++) memcpy(&mem[dst + 4 * i], &value, 4);
}

/* The general registers, for functions written as a translation of their
 * register usage; loaded at the start, stored before returning. */
typedef struct { uint32_t eax, ecx, edx, ebx, ebp, esi, edi; } regs32;
static inline regs32 regs_load(void) { return (regs32){EAX, ECX, EDX, EBX, EBP, ESI, EDI}; }
static inline void regs_store(const regs32 *r)
{
    WR_REG(R_EAX, 4, r->eax); WR_REG(R_ECX, 4, r->ecx); WR_REG(R_EDX, 4, r->edx); WR_REG(R_EBX, 4, r->ebx);
    WR_REG(R_EBP, 4, r->ebp); WR_REG(R_ESI, 4, r->esi); WR_REG(R_EDI, 4, r->edi);
}
/* Low 16 bits of a 32-bit register: read, and write leaving the top alone. */
static inline uint16_t lo16(uint32_t r) { return (uint16_t)r; }
static inline void set_lo16(uint32_t *r, uint16_t v) { *r = (*r & 0xffff0000u) | v; }

/* RET: pop the return address (Ghidra keeps it in its EIP slot, 0x284) and
 * report it to the caller's check (RT_CALL). */
static inline void x86_ret(void)
{
    const uint32_t ra = (uint32_t)MEM_RD(ESP, 4);
    WR_REG(0x284, 4, ra);
    WR_REG(R_ESP, 4, ESP + 4);
    RT_RET(ra);
}

/* CALL to another core function (f_<addr>, generated or hand-written): pushes
 * the return address `ret` and runs it, as the generated code does; returns
 * from the calling hand-written function if the callee returned somewhere
 * else (a caller further up dropped the return address). Use only directly
 * in a HAND_FN body. */
#define HAND_CALL(fn, ret)                                                  \
    do {                                                                    \
        WR_REG(R_ESP, 4, ESP - 4);                                          \
        MEM_WR(ESP, 4, (ret));                                              \
        RT_CALL(fn, (ret));                                                 \
    } while (0)

/* Startup check that the generated core was made with the current list of
 * replacements (replaced.h); false with a message otherwise. */
bool core_check_replaced(void);
