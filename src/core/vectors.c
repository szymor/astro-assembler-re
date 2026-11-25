/*
 * Arithmetic on small arrays of fixed-point values: angle updates, vector
 * addition and normalisation. Written as translations of the original
 * register use (see core.h).
 */
#include "core.h"

/*
 * Core_AddVectors (0x4435ec): ECX vectors of three dwords at ESI each get
 * (EAX, EBX, EDX) added. ESI ends after the last one, ECX at 0 (a count of
 * 0 would run 2^32 times, as in the original).
 */
HAND_FN(4435ec)
{
    regs32 r = regs_load();
    do {
        wr32(r.esi, rd32(r.esi) + r.eax);
        wr32(r.esi + 4, rd32(r.esi + 4) + r.ebx);
        wr32(r.esi + 8, rd32(r.esi + 8) + r.edx);
        r.esi = add32(r.esi, 0xc);
        r.ecx = dec32(r.ecx);
    } while (!flag(R_ZF));
    regs_store(&r);
    x86_ret();
}

/*
 * Core_StepAngles (0x44359c): three angles (words at ESI, +4 and +8; the
 * steps are the words after them) advance by their step and wrap into
 * [0, 0xb40) (0xb40 = 2880 = 360 * 8). CX = 0xb40; AX = the last angle.
 */
HAND_FN(44359c)
{
    const uint16_t period = 0xb40;
    uint16_t ax = 0;
    for (unsigned i = 0; i < 3; i++) {
        const uint32_t a = ESI + 4 * i;
        ax = add16(rd16(a), rd16(a + 2));
        if (ax & 0x8000) ax = add16(ax, period);         /* JNS not taken */
        cmp16(ax, period);
        if (!cond_l()) ax = sub16(ax, period);
        wr16(a, ax);
    }
    WR_REG(R_ECX, 2, period);
    set_ax(ax);
    x86_ret();
}

/*
 * Core_NormaliseVectors (0x443554): EBX vectors (x, y, z dwords) at ESI.
 * For each: the integer parts of the squares summed (the high dwords of the
 * 64-bit products, so a 16.16 length squared) go to a dword at EBP; with
 * n = that sum >> 6, the words x / n and z / n * 0xd5 >> 8 go to EDI (y is
 * skipped). The registers keep the last vector's intermediate values.
 */
HAND_FN(443554)
{
    regs32 r = regs_load();
    do {
        uint64_t p = (uint64_t)((int64_t)(int32_t)rd32(r.esi) * (int32_t)rd32(r.esi));
        r.eax = (uint32_t)p; r.edx = (uint32_t)(p >> 32); r.esi += 4;
        r.ecx = r.edx;
        p = (uint64_t)((int64_t)(int32_t)rd32(r.esi) * (int32_t)rd32(r.esi));
        r.eax = (uint32_t)p; r.edx = (uint32_t)(p >> 32); r.esi += 4;
        r.ecx += r.edx;
        p = (uint64_t)((int64_t)(int32_t)rd32(r.esi) * (int32_t)rd32(r.esi));
        r.eax = (uint32_t)p; r.edx = (uint32_t)(p >> 32);
        r.ecx += r.edx;
        wr32(r.ebp, r.ecx);
        r.ecx = (uint32_t)((int32_t)r.ecx >> 6);
        r.ebp += 4;
        int32_t num = (int32_t)rd32(r.esi - 8), den = (int32_t)r.ecx;
        r.eax = (uint32_t)(num / den); r.edx = (uint32_t)(num % den);
        wr16(r.edi, (uint16_t)r.eax);
        r.edi += 2;
        num = (int32_t)rd32(r.esi);
        r.eax = (uint32_t)(num / den); r.edx = (uint32_t)(num % den);
        r.esi = add32(r.esi, 4);
        r.eax = (uint32_t)((int32_t)(r.eax * 0xd5u) >> 8);
        wr16(r.edi, (uint16_t)r.eax);
        r.edi = add32(r.edi, 2);
        r.ebx = dec32(r.ebx);
    } while (!flag(R_ZF));
    regs_store(&r);
    x86_ret();
}

/*
 * Core_WrapAngle16 (0x41b54b): BX wrapped once into [0, 0x2d0) (720 = 360 * 2),
 * 16-bit arithmetic; flags of the last compare or subtraction.
 */
HAND_FN(41b54b)
{
    uint16_t bx = (uint16_t)EBX;
    cmp16(bx, 0);
    if (!cond_ge()) bx = add16(bx, 0x2d0);
    cmp16(bx, 0x2d0);
    if (!cond_l()) bx = sub16(bx, 0x2d0);
    WR_REG(R_EBX, 2, bx);
    x86_ret();
}

/*
 * Core_SplitFixed (0x42be54): ECX and EBX (16.16 fixed point) are split: the
 * rounded integer parts go to 0x4b3bb8 (from ECX) and
 * 0x4b3bb4 (from EBX); EAX and EDX are left holding them shifted back (<< 16); the remainders (below one unit, plus 2,
 * divided by 4) to 0x4b3bc0 (ECX) and 0x4b3bbc (EBX). Flags: those of the last
 * arithmetic shift (OF of the preceding addition).
 */
HAND_FN(42be54)
{
    uint32_t ecx = ECX, ebx = EBX;
    const uint32_t eax = (uint32_t)((int32_t)(ecx + 0x8000u) >> 16);
    const uint32_t edx = (uint32_t)((int32_t)(ebx + 0x8000u) >> 16);
    wr32(0x4b3bb8u, eax);
    wr32(0x4b3bb4u, edx);
    ecx = sub32(ecx, eax << 16);
    ebx = sub32(ebx, edx << 16);
    ecx = add32(ecx, 2);
    ebx = add32(ebx, 2);
    ecx = sar32(ecx, 2);
    ebx = sar32(ebx, 2);
    wr32(0x4b3bc0u, ecx);
    wr32(0x4b3bbcu, ebx);
    WR_REG(R_EAX, 4, eax << 16); WR_REG(R_EDX, 4, edx << 16); WR_REG(R_ECX, 4, ecx); WR_REG(R_EBX, 4, ebx);
    x86_ret();
}

/*
 * Core_SpawnShot (0x41b676): takes the first free entry (first word zero,
 * 12 bytes, from ESI + 0xc on) and fills it in: word 2 at +0, the dword at
 * 0x46ff64 at +2, DI at +0xa, and the two components of a velocity at +6 and +8:
 * the sine/cosine table at 0x1f4f74 (indexed by the angle BP, advanced by 16,
 * masked to 0xffe0, wrapped at 0x5a00 and divided by 8, plus the arena offset)
 * scaled by the dwords at 0x46ffc4 and 0x4b104c. EBP = the table index, EAX and
 * EDX the last products' parts. Flags: those of the last arithmetic shift.
 */
HAND_FN(41b676)
{
    uint32_t esi = ESI;
    do { esi = add32(esi, 0xc); cmp16(rd16(esi), 0); } while (!flag(R_ZF));
    wr16(esi + 0xa, (uint16_t)EDI);
    wr16(esi, 2);
    wr32(esi + 2, rd32(0x46ff64u));
    uint16_t bp = add16((uint16_t)EBP, 0x10);
    bp &= 0xffe0;
    cmp16(bp, 0x5a00);
    if (flag(R_CF) == 0) bp = sub16(bp, 0x5a00);
    bp >>= 3;
    const uint32_t ebp = ((EBP & 0xffff0000u) | bp) + rd32(0x556ac8u);
    int64_t p = (int64_t)(int16_t)rd16(ebp + 0x1f4f74u) * (int32_t)rd32(0x46ffc4u);
    p = (int64_t)(int32_t)(uint32_t)p * (int32_t)rd32(0x4b104cu);
    uint32_t edx = add32((uint32_t)((uint64_t)p >> 32), 0x100);
    edx = sar32(edx, 9);
    wr16(esi + 6, (uint16_t)edx);
    p = (int64_t)(int16_t)rd16(ebp + 0x1f4f76u) * (int32_t)rd32(0x46ffc4u);
    edx = (uint32_t)((uint64_t)p >> 32);
    uint32_t eax = add32((uint32_t)p, 0x200);
    eax = sar32(eax, 10);
    wr16(esi + 8, (uint16_t)eax);
    WR_REG(R_ESI, 4, esi);
    WR_REG(R_EBP, 4, ebp);
    WR_REG(R_EDX, 4, edx);
    WR_REG(R_EAX, 4, eax);
    x86_ret();
}

/*
 * Core_FollowPath (0x42d318): a point on a path: the dword at 0x498494 is the
 * path's offset (0 = none), 0x498498 the position in it. The point at that position
 * (two words, scaled by the dword at 0x4984a8 and offset by 0x4983a8 and 0x4983ac)
 * goes to 0x4983b0 / 0x4983b4, and the position advances by the step (the dword
 * at 0x49849c rounded to whole 4-byte units, at most 0x20); a word of 0x8000 at
 * the new position ends the path (position 0). EDI = the offset in the arena, EAX
 * and EBX the last values.
 */
HAND_FN(42d318)
{
    const uint32_t off = rd32(0x498494u);
    test32(off, 0xffffffffu);
    if (flag(R_ZF)) { WR_REG(R_EDI, 4, 0); x86_ret(); return; }
    const uint32_t edi = off + rd32(0x556ac8u);
    uint32_t ebp = rd32(0x498498u);
    uint32_t eax = (uint32_t)((int32_t)(int16_t)rd16(ebp + edi) * (int32_t)rd32(0x4984a8u));
    uint32_t ebx = (uint32_t)((int32_t)(int16_t)rd16(ebp + edi + 2) * (int32_t)rd32(0x4984a8u));
    eax = add32(eax, rd32(0x4983a8u));
    ebx = add32(ebx, rd32(0x4983acu));
    wr32(0x4983b0u, eax);
    wr32(0x4983b4u, ebx);
    eax = ((rd32(0x49849cu) + 0x8000u) >> 16) << 2;
    cmp32(eax, 0x20);
    if (cond_a()) eax = 0x20;
    ebp = add32(ebp, eax);
    cmp16(rd16(ebp + edi), 0x8000);
    if (flag(R_ZF)) {
        ebp = 0;
        flags_szp32(0);
        set_flag(R_CF, false);
        set_flag(R_OF, false);
    }
    wr32(0x498498u, ebp);
    WR_REG(R_EAX, 4, eax); WR_REG(R_EBX, 4, ebx); WR_REG(R_EBP, 4, ebp); WR_REG(R_EDI, 4, edi);
    x86_ret();
}

/*
 * Core_FollowPath2 (0x4299b8): as Core_FollowPath for the path at 0x497c34 (position
 * 0x497c38, step 0x497c3c, point 0x497b00/0x497b04 from the words scaled by 1024
 * and offset by 0x497af8/0x497afc), and then the point drifts: a countdown at
 * 0x497aec; when it runs out, the next (countdown, dx, dy) triple of the list
 * pointed to by 0x497ae8 is loaded. Each call adds (dx, dy) at 0x497af0/0x497af4
 * to the offsets. Flags: those of the last addition.
 */
HAND_FN(4299b8)
{
    uint32_t eax = EAX;
    const uint32_t off = rd32(0x497c34u);
    uint32_t edi = 0, ebp = EBP, ebx = EBX;
    test32(off, 0xffffffffu);
    if (!flag(R_ZF)) {
        edi = off + rd32(0x556ac8u);
        ebp = rd32(0x497c38u);
        eax = (uint32_t)(int32_t)(int16_t)rd16(ebp + edi) << 10;
        ebx = (uint32_t)(int32_t)(int16_t)rd16(ebp + edi + 2) << 10;
        eax = add32(eax, rd32(0x497af8u));
        ebx = add32(ebx, rd32(0x497afcu));
        wr32(0x497b00u, eax);
        wr32(0x497b04u, ebx);
        eax = ((rd32(0x497c3cu) + 0x8000u) >> 16) << 2;
        cmp32(eax, 0x20);
        if (cond_a()) eax = 0x20;
        ebp = add32(ebp, eax);
        cmp16(rd16(ebp + edi), 0x8000);
        if (flag(R_ZF)) ebp = 0;
        wr32(0x497c38u, ebp);
    }
    uint32_t esi = ESI;
    const uint32_t left = dec32(rd32(0x497aecu));
    wr32(0x497aecu, left);
    if (flag(R_ZF)) {
        esi = rd32(0x497ae8u);
        wr32(0x497aecu, rd32(esi));
        wr32(0x497af0u, rd32(esi + 4));
        wr32(0x497af4u, rd32(esi + 8));
        esi += 12;
        eax = rd32(esi - 4);
        wr32(0x497ae8u, esi);
    }
    eax = rd32(0x497af0u);
    wr32(0x497af8u, add32(rd32(0x497af8u), eax));
    eax = rd32(0x497af4u);
    wr32(0x497afcu, add32(rd32(0x497afcu), eax));
    WR_REG(R_EAX, 4, eax); WR_REG(R_EBX, 4, ebx); WR_REG(R_EBP, 4, ebp);
    WR_REG(R_ESI, 4, esi); WR_REG(R_EDI, 4, edi);
    x86_ret();
}
