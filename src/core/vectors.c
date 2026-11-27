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
    const uint32_t ebp = ((EBP & 0xffff0000u) | bp) + rd32(G_ARENA_BASE);
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
 * The rounded speed (16.16 bytes per frame) as whole 4-byte path steps, at
 * most 0x20 bytes, with the flags of that compare.
 */
static uint32_t path_advance(uint32_t speed_at)
{
    uint32_t step = ((rd32(speed_at) + 0x8000u) >> 16) << 2;
    cmp32(step, 0x20);
    if (cond_a()) step = 0x20;
    return step;
}

/*
 * Boss3_FollowPath (0x42d318): the level-3 boss's movement. Its position is
 * its origin plus the point of its path (BOSS3_PATH, two words per point)
 * times the path's scale; the path position advances by the rounded speed,
 * back to 0 at a word of 0x8000. Nothing happens without a path (EDI = 0).
 * EDI = the path in the arena, EAX the advance and EBX the y.
 */
HAND_FN(42d318)
{
    const uint32_t off = rd32(BOSS3_PATH + PATH_OFFSET);
    test32(off, 0xffffffffu);
    if (flag(R_ZF)) { WR_REG(R_EDI, 4, 0); x86_ret(); return; }
    const uint32_t path = off + rd32(G_ARENA_BASE);
    uint32_t pos = rd32(BOSS3_PATH + PATH_POS);
    const int32_t scale = (int32_t)rd32(BOSS3_PATH_SCALE);
    uint32_t x = (uint32_t)((int32_t)(int16_t)rd16(path + pos) * scale);
    uint32_t y = (uint32_t)((int32_t)(int16_t)rd16(path + pos + 2) * scale);
    x = add32(x, rd32(BOSS3_ORIGIN_X));
    y = add32(y, rd32(BOSS3_ORIGIN_Y));
    wr32(BOSS3_POS_X, x);
    wr32(BOSS3_POS_Y, y);
    const uint32_t step = path_advance(BOSS3_SPEED);
    pos = add32(pos, step);
    cmp16(rd16(path + pos), 0x8000);
    if (flag(R_ZF)) {                                               /* the end: XOR EBP, EBP */
        pos = 0;
        flags_szp32(0);
        set_flag(R_CF, false);
        set_flag(R_OF, false);
    }
    wr32(BOSS3_PATH + PATH_POS, pos);
    WR_REG(R_EAX, 4, step); WR_REG(R_EBX, 4, y); WR_REG(R_EBP, 4, pos); WR_REG(R_EDI, 4, path);
    x86_ret();
}

/*
 * Boss2_FollowPath (0x4299b8): the level-2 boss's movement: as Boss3_FollowPath
 * for its path (points scaled by 1024), and then its origin drifts: each frame
 * by (dx, dy); when the drift's frames run out the next (frames, dx, dy) entry
 * of the drift list is taken. Flags: those of the last addition.
 */
HAND_FN(4299b8)
{
    uint32_t eax = EAX;
    const uint32_t off = rd32(BOSS2_PATH + PATH_OFFSET);
    uint32_t path = 0, pos = EBP, y = EBX;
    test32(off, 0xffffffffu);
    if (!flag(R_ZF)) {
        path = off + rd32(G_ARENA_BASE);
        pos = rd32(BOSS2_PATH + PATH_POS);
        uint32_t x = (uint32_t)(int32_t)(int16_t)rd16(path + pos) << 10;
        y = (uint32_t)(int32_t)(int16_t)rd16(path + pos + 2) << 10;
        x = add32(x, rd32(BOSS2_ORIGIN_X));
        y = add32(y, rd32(BOSS2_ORIGIN_Y));
        wr32(BOSS2_POS_X, x);
        wr32(BOSS2_POS_Y, y);
        eax = path_advance(BOSS2_SPEED);
        pos = add32(pos, eax);
        cmp16(rd16(path + pos), 0x8000);
        if (flag(R_ZF)) pos = 0;
        wr32(BOSS2_PATH + PATH_POS, pos);
    }
    uint32_t esi = ESI;
    const uint32_t left = dec32(rd32(BOSS2_DRIFT_LEFT));
    wr32(BOSS2_DRIFT_LEFT, left);
    if (flag(R_ZF)) {                                               /* the next drift */
        esi = rd32(BOSS2_DRIFT_LIST);
        wr32(BOSS2_DRIFT_LEFT, rd32(esi));
        wr32(BOSS2_DRIFT_DX, rd32(esi + 4));
        wr32(BOSS2_DRIFT_DY, rd32(esi + 8));
        esi += 12;
        eax = rd32(esi - 4);
        wr32(BOSS2_DRIFT_LIST, esi);
    }
    eax = rd32(BOSS2_DRIFT_DX);
    wr32(BOSS2_ORIGIN_X, add32(rd32(BOSS2_ORIGIN_X), eax));
    eax = rd32(BOSS2_DRIFT_DY);
    wr32(BOSS2_ORIGIN_Y, add32(rd32(BOSS2_ORIGIN_Y), eax));
    WR_REG(R_EAX, 4, eax); WR_REG(R_EBX, 4, y); WR_REG(R_EBP, 4, pos);
    WR_REG(R_ESI, 4, esi); WR_REG(R_EDI, 4, path);
    x86_ret();
}

/*
 * Boss2_StepArmPaths_q (0x42aee4): the level-2 boss's arms follow their paths
 * (each arm's ARM_PATH record): for an arm with a path, the two dwords at the
 * position times the reach (64-bit, >> 16) are the arm's offsets (-0x18 and
 * -0xc from the record), and the position moves on by 8 (to 0 at a 0x80000000
 * end marker). EAX = the last result, EDX = its high dword, EBP =
 * the last position, EDI the last offset. Flags: those of the last compare, or
 * of the test of the first record without a path.
 */
typedef struct { uint32_t eax, edx, ebp, edi; } points_regs;

/* One part's path record at `b` (PATH_*, game.h): the part's offsets are the
 * path's values at the position (nvals dwords, 16.16) times the scale (its
 * reach), written to out[0..]; the position advances by `adv`, back to 0 at a
 * 0x80000000 end marker. */
static void points_record(points_regs *r, uint32_t b, unsigned nvals, const uint32_t out[2], uint32_t adv)
{
    r->edi = rd32(b + PATH_OFFSET);
    test32(r->edi, 0xffffffffu);
    if (flag(R_ZF)) { r->edi = 0; return; }                         /* no path */
    r->edi += rd32(G_ARENA_BASE);
    r->ebp = rd32(b + PATH_POS);
    for (unsigned k = 0; k < nvals; k++) {
        r->eax = rd32(b + PATH_SCALE);
        const int64_t p = (int64_t)(int32_t)r->eax * (int32_t)rd32(r->ebp + r->edi + 4 * k);
        r->edx = (uint32_t)((uint64_t)p >> 32);
        r->eax = (uint32_t)(((uint64_t)p >> 16) & 0xffffffffu);
        wr32(out[k], r->eax);
    }
    r->ebp = add32(r->ebp, adv);
    cmp32(rd32(r->ebp + r->edi), 0x80000000u);
    if (flag(R_ZF)) { r->ebp = 0; flags_szp32(0); set_flag(R_CF, false); set_flag(R_OF, false); }
    wr32(b + PATH_POS, r->ebp);
}

HAND_FN(42aee4)
{
    points_regs r = {EAX, EDX, EBP, EDI};
    for (unsigned arm = 0; arm < BOSS2_ARMS; arm++) {
        const uint32_t b = boss2_arm(arm, ARM_PATH);
        const uint32_t out[2] = {b - 0x18, b - 0xc};                /* the arm's offsets (fields still unnamed) */
        points_record(&r, b, 2, out, 8);
    }
    WR_REG(R_EAX, 4, r.eax); WR_REG(R_EDX, 4, r.edx); WR_REG(R_EBP, 4, r.ebp); WR_REG(R_EDI, 4, r.edi);
    x86_ret();
}

/* Boss3_StepLimbPaths_q (0x42e495): the same for the level-3 boss's parts:
 * part 0 with one value per step (position step 4), parts 1-4 with two. */
HAND_FN(42e495)
{
    points_regs r = {EAX, EDX, EBP, EDI};
    const uint32_t one[2] = {k_boss3_part_path[0] - 8, 0};
    points_record(&r, k_boss3_part_path[0], 1, one, 4);
    for (unsigned i = 1; i < BOSS3_PARTS; i++) {
        const uint32_t b = k_boss3_part_path[i];
        const uint32_t out[2] = {b - 0x18, b - 0xc};
        points_record(&r, b, 2, out, 8);
    }
    WR_REG(R_EAX, 4, r.eax); WR_REG(R_EDX, 4, r.edx); WR_REG(R_EBP, 4, r.ebp); WR_REG(R_EDI, 4, r.edi);
    x86_ret();
}

/*
 * Core_ProjectPoints (0x41be2a): CX points (word pairs at EDI) are transformed
 * by the factors in the object at EBP (+0xa, +0xe multipliers; +2 and +6
 * offsets; +0x12 added to the second coordinate, which is set to 0 when the sum
 * is not positive) and written as screen words to the table at
 * 0x4b1b2a (x halved, adjusted by the word at 0x4b1f9a when the byte at +0x1a
 * is set). ESI is preserved; EDI ends after the input. Flags: those of the DEC CX
 * (CF of the last ESI addition).
 */
HAND_FN(41be2a)
{
    uint32_t esi = 0x4b1b2au, edi = EDI;
    const uint32_t ebp = EBP;
    uint16_t cx = (uint16_t)ECX;
    uint32_t eax = EAX, ebx = EBX, ecx = ECX, edx = EDX;
    do {
        uint16_t bx = add16(rd16(edi + 2), rd16(ebp + 0x12));
        if (!(!flag(R_ZF) && flag(R_SF) == flag(R_OF))) bx = 0;       /* not JG */
        ecx = (uint32_t)(int32_t)(int16_t)rd16(edi);
        ebx = (uint32_t)(int32_t)(int16_t)bx;
        edx = ecx;
        eax = ebx;
        edx *= rd32(ebp + 0xa);
        eax *= rd32(ebp + 0xa);
        ecx *= rd32(ebp + 0xe);
        ebx *= rd32(ebp + 0xe);
        eax = sub32(eax, ecx);
        edx = add32(edx, ebx);
        eax *= 0xd5;
        eax = add32(eax, 0x80);
        eax = sar32(eax, 8);
        edx = add32(edx, rd32(ebp + 2));
        eax = add32(eax, rd32(ebp + 6));
        edx = add32(edx, 0x1000);
        eax = add32(eax, 0x2000);
        edx = sar32(edx, 13);
        eax = sar32(eax, 14);
        uint16_t dx = (uint16_t)edx;
        cmp8(rd8(ebp + 0x1a), 0);
        if (!flag(R_ZF)) dx = (uint16_t)(dx - rd16(G_SCROLL_X));
        dx = (uint16_t)(dx + 1);
        dx = (uint16_t)((int16_t)dx >> 1);
        edx = (edx & 0xffff0000u) | dx;
        wr16(esi, dx);
        wr16(esi + 2, (uint16_t)eax);
        edi += 4;
        esi = add32(esi, 4);
        ecx = (ecx & 0xffff0000u) | cx;
        cx = dec16(cx);
    } while (!flag(R_ZF));
    WR_REG(R_EAX, 4, eax); WR_REG(R_EBX, 4, ebx); WR_REG(R_ECX, 4, (ecx & 0xffff0000u) | cx);
    WR_REG(R_EDX, 4, edx); WR_REG(R_EDI, 4, edi);
    x86_ret();
}

/*
 * Math_BuildRotation (0x4434c4): three angles (words at EBP, +4, +8: indices into
 * the sine table at 0x1f4f74 of the arena) and the factor ECX give three
 * values at 0x4b94b4, 0x4b94b8 and 0x4b94bc (products of sines and cosines, in
 * 14-bit fixed point). EAX, EBX, ECX, EDX, EDI keep the last intermediate
 * values. Flags: those of the last arithmetic shift (OF of the preceding addition).
 */
HAND_FN(4434c4)
{
    const uint32_t arena = rd32(G_ARENA_BASE);
    uint32_t ebp = EBP, ecx = ECX;
#define SIN(i) ((uint32_t)(int32_t)(int16_t)rd16((i) + 0x1f4f74u))
#define COS(i) ((uint32_t)(int32_t)(int16_t)rd16((i) + 0x1f4f76u))
#define SAR14(v) ((uint32_t)((int32_t)(v) >> 14))
    uint32_t edx = rd16(ebp) + arena;
    uint32_t eax = SAR14(0u - SIN(edx) * ecx);
    uint32_t ebx = COS(edx);
    ecx = SAR14(ecx * ebx);
    edx = rd16(ebp + 8) + arena;
    ebx = SAR14(0u - SIN(edx) * ecx);
    edx = COS(edx);
    ecx = SAR14(ecx * edx);
    wr32(0x4b94b8u, ecx);
    edx = rd16(ebp + 4) + arena;
    ecx = SIN(edx);
    edx = COS(edx);
    uint32_t edi = edx;
    edx *= eax;
    edi *= ebx;
    eax *= ecx;
    ebx *= ecx;
    edx = sub32(edx, ebx);
    eax = add32(eax, edi);
    edx = sar32(edx, 14);
    eax = sar32(eax, 14);
    wr32(0x4b94b4u, edx);
    wr32(0x4b94bcu, eax);
#undef SIN
#undef COS
#undef SAR14
    WR_REG(R_EAX, 4, eax); WR_REG(R_EBX, 4, ebx); WR_REG(R_ECX, 4, ecx); WR_REG(R_EDX, 4, edx); WR_REG(R_EDI, 4, edi);
    x86_ret();
}
