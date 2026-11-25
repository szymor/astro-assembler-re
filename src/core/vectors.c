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
