/*
 * Clamped steppers, free-slot searches, frame-buffer row copies and small
 * arithmetic helpers. Each step keeps the flags of the instruction it is (see
 * core.h).
 */
#include "core.h"

/*
 * A value at `cur` moves by the signed step at `step` towards the target at
 * `limit` and stops there (it is left alone once it has arrived): the bosses'
 * path speed and scale (Boss3_StepPathSpeed, Boss3_StepPathScale,
 * Boss2_StepPathSpeed).
 */
static void step_towards(uint32_t cur, uint32_t limit, uint32_t step)
{
    uint32_t eax = rd32(cur);
    WR_REG(R_EAX, 4, eax);
    cmp32(eax, rd32(limit));
    if (flag(R_ZF)) { x86_ret(); return; }
    const uint32_t ebx = rd32(step);
    WR_REG(R_EBX, 4, ebx);
    test32(ebx, 0xffffffffu);
    eax = add32(eax, ebx);
    cmp32(eax, rd32(limit));
    const bool neg = (ebx & 0x80000000u) != 0;          /* the sign was tested before the addition */
    if (neg ? !cond_ge() : !cond_le()) eax = rd32(limit);
    WR_REG(R_EAX, 4, eax);
    wr32(cur, eax);
    x86_ret();
}
HAND_FN(42d38b) { step_towards(0x49849c, 0x4984a0, 0x4984a4); }
HAND_FN(42d3c8) { step_towards(0x4984a8, 0x4984ac, 0x4984b0); }
HAND_FN(429a5f) { step_towards(0x497c3c, 0x497c40, 0x497c44); }

/*
 * The first free slot (word 0 is zero) after the first in a table of
 * `stride`-byte slots at `base`, unless the count at `count_at` says it is
 * full. Carry set: full; clear: EBP points at a free slot.
 */
static bool find_free_slot(uint32_t count_at, uint16_t full, uint32_t base, uint32_t stride)
{
    const uint16_t bp = rd16(count_at);
    WR_REG(R_EBP, 2, bp);
    cmp16(bp, full);
    if (flag(R_ZF)) { set_flag(R_CF, true); return false; }
    uint32_t ebp = base;
    do {
        ebp = add32(ebp, stride);
        cmp16(rd16(ebp), 0);
    } while (!flag(R_ZF));
    WR_REG(R_EBP, 4, ebp);
    set_flag(R_CF, false);
    return true;
}
HAND_FN(41cf17) { find_free_slot(0x4acccc, 0x32, 0x526ca8, 0x28); x86_ret(); }
HAND_FN(415fe8) { find_free_slot(0x4b37a8, 0x64, 0x519d58, 0x30); x86_ret(); }
HAND_FN(416283) { find_free_slot(0x4b3b7c, 0xc8, 0x51b034, 0x14); x86_ret(); }

/* 415d5c: carry set if the point (AX, CX) is outside the box around (BX, DX). */
HAND_FN(415d5c)
{
    uint16_t ax = (uint16_t)EAX, bx = (uint16_t)EBX, cx = (uint16_t)ECX, dx = (uint16_t)EDX;
    bool outside = false;
    cx = add16(cx, 0x20); WR_REG(R_ECX, 2, cx);
    dx = add16(dx, 0x24); WR_REG(R_EDX, 2, dx);
    cmp16(ax, dx);
    if (!flag(R_ZF) && cond_ge()) outside = true;           /* JG */
    else {
        bx = sub16(bx, 0x16); WR_REG(R_EBX, 2, bx);
        cmp16(cx, bx);
        if (cond_l()) outside = true;
        else {
            bx = add16(bx, 0x2c); WR_REG(R_EBX, 2, bx);
            cmp16(cx, bx);
            if (!flag(R_ZF) && cond_ge()) outside = true;
        }
    }
    set_flag(R_CF, outside);
    x86_ret();
}

/* 415d7f: x and y of the object at ESI from two scaled word sources (offsets and divisors in the object). */
HAND_FN(415d7f)
{
    const uint32_t esi = ESI;
    uint32_t edi = rd32(esi + 0x14);
    WR_REG(R_EDI, 4, edi);
    int32_t a = (int16_t)rd16(edi), d = (int16_t)rd16(esi + 0x1c);
    uint16_t ax = (uint16_t)(a / d);
    WR_REG(R_EDX, 2, (uint16_t)(a % d));
    ax = sub16(ax, rd16(0x4b1f9a));
    set_flag(R_CF, ax & 1);                                  /* SAR AX, 1 */
    ax = (uint16_t)((int16_t)ax >> 1);
    set_flag(R_OF, false);
    flags_szp16(ax);
    ax = add16(ax, rd16(esi + 0x20));
    wr16(esi + 2, ax);
    edi = rd32(esi + 0x18);
    WR_REG(R_EDI, 4, edi);
    a = (int16_t)rd16(edi); d = (int16_t)rd16(esi + 0x1e);
    ax = (uint16_t)(a / d);
    WR_REG(R_EDX, 2, (uint16_t)(a % d));
    ax = add16(ax, rd16(esi + 0x22));
    wr16(esi + 4, ax);
    WR_REG(R_EAX, 2, ax);
    x86_ret();
}

/* 44688f: EAX is a row offset and ECX the length; copy a run from the source (row * pitch + EAX) to the frame buffer. */
HAND_FN(44688f)
{
    uint32_t esi = rd32(0x55b5dc) * rd32(0x46ff9c);
    esi = add32(esi, EAX);
    esi = add32(esi, rd32(0x50f2ac));
    const uint32_t eax = add32(EAX, rd32(0x46ff64));
    uint32_t edi = add32(rd32(0x55c7fc), eax);
    const uint32_t n = ECX;
    copy_forward(edi, esi, n & ~3u);
    copy_forward(edi + (n & ~3u), esi + (n & ~3u), n & 3u);
    test32(n, 3);
    WR_REG(R_EAX, 4, eax);
    WR_REG(R_EBX, 4, n);
    WR_REG(R_ECX, 4, 0);
    WR_REG(R_ESI, 4, esi + n);
    WR_REG(R_EDI, 4, edi + n);
    x86_ret();
}

/* 446c99: if the flag at 0x55c804 is set, copy the 480 rows from 0x55b610 to the frame buffer and clear the flag. */
HAND_FN(446c99)
{
    cmp32(rd32(0x55c804), 0);
    if (flag(R_ZF)) { x86_ret(); return; }
    uint32_t esi = rd32(0x55b610), edi = rd32(0x55c7fc), ebx = rd32(0x55c800);
    const uint32_t width = rd32(0x55b5dc);
    ebx = sub32(ebx, width);
    const uint32_t eax = width >> 2, bytes = 4 * eax;
    uint32_t edx = 0x1e0;
    do {
        copy_forward(edi, esi, bytes);
        esi += bytes;
        edi += bytes;
        edi = add32(edi, ebx);
        edx = dec32(edx);
    } while (!flag(R_ZF));
    wr32(0x55c804, 0);
    WR_REG(R_EAX, 4, eax);
    WR_REG(R_EBX, 4, ebx);
    WR_REG(R_ECX, 4, 0);
    WR_REG(R_EDX, 4, edx);
    WR_REG(R_ESI, 4, esi);
    WR_REG(R_EDI, 4, edi);
    x86_ret();
}

/* 4233c9: AX, BX = the position (0x46ff50, 0x46ff52) moved by the table vector at index EDX (times the radius at EBP+0x48). */
HAND_FN(4233c9)
{
    uint16_t ax = rd16(0x46ff50), bx = rd16(0x46ff52);
    const uint32_t edx = (EDX << 2) + rd32(0x556ac8);
    int32_t ecx = (int16_t)rd16(edx + 0x1f4f74), dx = (int16_t)rd16(edx + 0x1f4f76);
    const uint32_t esi = rd16(EBP + 0x48);
    ecx = (int32_t)((uint32_t)ecx * esi);
    dx = (int32_t)((uint32_t)dx * esi);
    ecx = (int32_t)((uint32_t)ecx * 0xd5u);
    dx >>= 14;
    ecx >>= 22;
    bx = add16(bx, (uint16_t)dx);
    ax = sub16(ax, (uint16_t)ecx);
    WR_REG(R_EAX, 2, ax);
    WR_REG(R_EBX, 2, bx);
    WR_REG(R_ECX, 4, (uint32_t)ecx);
    WR_REG(R_EDX, 4, (uint32_t)dx);
    WR_REG(R_ESI, 4, esi);
    x86_ret();
}
