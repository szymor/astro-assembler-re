/*
 * Vector arithmetic that takes a square root through the x87 FPU (FILD, FSQRT,
 * FISTP; x87_isqrt in core.h reproduces their effect on the FPU state). The
 * divisions behave as in the generated code: a zero divisor faults, and a
 * quotient that does not fit is truncated to 32 bits (the x86 would fault
 * there too; the game never divides that far).
 */
#include "core.h"

/* DIV r/m32 with EDX:EAX = (0, a): the quotient, *rem the remainder. */
static uint32_t div32(uint32_t a, uint32_t d, uint32_t *rem)
{
    *rem = (uint32_t)RT_UREM(a, d);
    return (uint32_t)RT_UDIV(a, d);
}
/* IMUL r/m32 (one operand) then IDIV r/m32: (a * b) / d as 64 bits, truncated. */
static uint32_t muldiv32(int32_t a, int32_t b, int32_t d, uint32_t *rem)
{
    const int64_t p = (int64_t)a * b;
    *rem = (uint32_t)RT_SREM(p, d);
    return (uint32_t)RT_SDIV(p, d);
}

/*
 * Math_PlanMove3D (0x41faa8): a straight move from the point at EDI to the
 * point at ESI (three 16.16 dwords each) at the speed in 0x46ff6c. The
 * difference goes to 0x46ff60..0x46ff68 and its length (of the integer parts
 * >> 12, so the squares stay in 32 bits) to 0x46ff5c; EBX = the frames the move
 * takes, the velocity goes to EBP, and the point at ESI is moved to where that
 * many frames of the velocity actually end. ECX = frames * speed, EDX the last
 * remainder, EAX the last coordinate; flags of its addition.
 */
HAND_FN(41faa8)
{
    const uint32_t from = EDI, to = ESI, vel = EBP;
    const uint32_t speed_at = 0x46ff6cu, diff = 0x46ff60u, len_at = 0x46ff5cu;
    uint32_t d[3];
    for (unsigned i = 0; i < 3; i++) d[i] = rd32(to + 4 * i) - rd32(from + 4 * i);
    for (unsigned i = 0; i < 3; i++) wr32(diff + 4 * i, d[i]);
    uint32_t sq[3];
    for (unsigned i = 0; i < 3; i++) {
        const uint32_t c = (uint32_t)((int32_t)d[i] >> 12);
        sq[i] = c * c;                                                  /* IMUL: the low dword */
    }
    const uint32_t sum = add32(sq[0] + sq[1], sq[2]);
    wr32(len_at, sum);
    const uint32_t len = x87_isqrt(len_at, (int32_t)sum, 0x41faecu);
    uint32_t edx;
    const uint32_t frames = div32(len << 12, rd32(speed_at), &edx);
    const uint32_t ecx = frames * rd32(speed_at);                               /* MUL: the low dword */
    uint32_t eax = 0;
    for (unsigned i = 0; i < 3; i++) {
        eax = muldiv32((int32_t)rd32(diff + 4 * i), (int32_t)rd32(speed_at), (int32_t)ecx, &edx);
        wr32(vel + 4 * i, eax);
        eax = add32(eax * frames, rd32(from + 4 * i));
        wr32(to + 4 * i, eax);
    }
    WR_REG(R_EAX, 4, eax); WR_REG(R_EBX, 4, frames); WR_REG(R_ECX, 4, ecx); WR_REG(R_EDX, 4, edx);
    x86_ret();
}

/*
 * Math_VectorAngle (0x42aab2): the angle of the vector (ECX, EDX) (also stored
 * at 0x46ffc8 / 0x46ffcc), in BX (0 .. 0xb3f, 8 units per degree): its length
 * (to 0x497b3c, through the FPU) divides the smaller component (as a 16.16
 * ratio, rounded to a byte) to index an arc-sine table in the arena
 * (0x1f4e73), and the signs pick the quadrant from the table at 0x4b0bbe. A
 * zero vector returns at once with EBX = 0. EAX's low word, ECX, EDX and ESI
 * keep the intermediate values.
 */
HAND_FN(42aab2)
{
    const uint32_t x = ECX, y = EDX;
    wr32(0x46ffc8u, x);
    wr32(0x46ffccu, y);
    const uint32_t xx = x * x, yy = y * y;
    uint32_t eax = add32(xx, yy);
    wr32(0x497b3cu, eax);
    const uint32_t len = x87_isqrt(0x497b3cu, (int32_t)eax, 0x42aad7u);
    uint32_t ebx = len;
    test32(ebx, 0xffffffffu);                                           /* OR EBX, [len] */
    if (flag(R_ZF)) {
        WR_REG(R_EAX, 4, eax); WR_REG(R_EBX, 4, ebx); WR_REG(R_ECX, 4, xx); WR_REG(R_EDX, 4, yy);
        x86_ret();
        return;
    }
    cmp32(xx, yy);
    const bool steep = flag(R_CF);                                     /* |x| < |y| */
    const uint32_t major_at = steep ? 0x46ffccu : 0x46ffc8u, minor_at = steep ? 0x46ffc8u : 0x46ffccu;
    const uint32_t major_bit = steep ? 4 : 2, minor_bit = steep ? 2 : 4;
    uint32_t ecx = 0;
    cmp32(rd32(major_at), 0);
    if (flag(R_SF)) { ecx |= major_bit; test32(ecx, 0xffffffffu); }    /* OR ECX, bit */
    eax = rd32(minor_at);
    set_flag(R_CF, ((eax >> 16) & 1) != 0);                             /* SHL EAX, 0x10 */
    eax <<= 16;
    flags_szp32(eax);
    uint32_t edx;
    eax = muldiv32((int32_t)eax, 1, (int32_t)ebx, &edx);               /* CDQ; IDIV EBX */
    uint32_t esi = add32(0x80, eax);
    esi = sar32(esi, 8);
    if (flag(R_SF)) {
        ecx |= minor_bit;
        test32(ecx, 0xffffffffu);
        esi = neg32(esi);
    }
    eax = rd32(G_ARENA_BASE);
    uint16_t ax = rd8(eax + esi + 0x1f4e73u);                         /* MOVZX AX: EAX keeps the base's top */
    if (steep) ax = neg16(sub16(ax, 0xb4));                            /* the angle from the other axis */
    eax = (eax & 0xffff0000u) | ax;
    ecx = add32(ecx, 0x4b0bbeu);
    uint16_t bx = sub16(rd16(ecx + 8), ax);
    if (flag(R_SF)) bx = neg16(bx);
    bx = add16(bx, rd16(ecx));
    set_lo16(&ebx, bx);
    WR_REG(R_EAX, 4, eax); WR_REG(R_EBX, 4, ebx); WR_REG(R_ECX, 4, ecx); WR_REG(R_EDX, 4, edx); WR_REG(R_ESI, 4, esi);
    x86_ret();
}

/*
 * Core_SortDescending (0x4438bc): quicksort of the dwords at EBP from byte
 * offset EBX to ECX (both inclusive, multiples of 4) into descending order
 * (unsigned), moving the dwords at EAX along with them; the 3D renderer
 * sorts its faces by depth with it. Recursive: after each recursive call the
 * code goes on with the ESI, EDI and EDX the call left, so the registers pass
 * through `cpu` around the calls.
 */
HAND_FN(4438bc)
{
    uint32_t eax = EAX, ebx = EBX, ecx = ECX, ebp = EBP;
    uint32_t esi = add32(ebx, ecx) >> 1;                            /* ADD; SHR ESI, 1 */
    esi &= 0xfffffffcu;                                            /* AND: its flags stand */
    test32(esi, 0xffffffffu);
    uint32_t edx = rd32(ebp + esi);                                 /* the pivot */
    uint32_t edi = ecx;
    esi = ebx;
    for (;;) {
        cmp32(rd32(ebp + esi), edx);
        while (cond_a()) { esi = add32(esi, 4); cmp32(rd32(ebp + esi), edx); }
        cmp32(rd32(ebp + edi), edx);
        while (flag(R_CF)) { edi = sub32(edi, 4); cmp32(rd32(ebp + edi), edx); }
        cmp32(esi, edi);
        if (!flag(R_ZF) && cond_ge()) break;                        /* JG */
        x86_push(edx);
        uint32_t a = rd32(ebp + esi), b = rd32(ebp + edi);
        wr32(ebp + edi, a); wr32(ebp + esi, b);                     /* XCHG: the keys */
        a = rd32(eax + esi); b = rd32(eax + edi);
        wr32(eax + edi, a); wr32(eax + esi, b);                     /* and the values */
        esi = add32(esi, 4);
        edi = sub32(edi, 4);
        edx = x86_pop();
        cmp32(esi, edi);
        if (!cond_le()) break;                                      /* JLE: on */
    }
    regs32 r = {eax, ecx, edx, ebx, ebp, esi, edi};
    cmp32(ebx, edi);
    if (!cond_ge()) {                                               /* the left part */
        x86_push(r.ecx);
        r.ecx = r.edi;
        regs_store(&r);
        HAND_CALL(f_4438bc, 0x44391cu);
        r = regs_load();
        r.ecx = x86_pop();
    }
    cmp32(r.ecx, r.esi);
    if (!cond_le()) {                                               /* the right part */
        x86_push(r.ebx);
        r.ebx = r.esi;
        regs_store(&r);
        HAND_CALL(f_4438bc, 0x443929u);
        r = regs_load();
        r.ebx = x86_pop();
    }
    regs_store(&r);
    x86_ret();
}


/* IMUL r32, r/m32: the low dword; CF = OF = the product did not fit. */
static uint32_t imul32(uint32_t a, uint32_t b)
{
    const int64_t p = (int64_t)(int32_t)a * (int32_t)b;
    const uint32_t r = (uint32_t)p;
    set_flag(R_CF, (int64_t)(int32_t)r != p);
    set_flag(R_OF, (int64_t)(int32_t)r != p);
    return r;
}
/* SAR r16, n (n >= 2): CF = the last bit out, SF, ZF, PF; OF unchanged. */
static uint16_t sar16n(uint16_t a, unsigned n)
{
    set_flag(R_CF, (((int16_t)a >> (n - 1)) & 1) != 0);
    const uint16_t v = (uint16_t)((int16_t)a >> n);
    flags_szp16(v);
    return v;
}
static int32_t sxw(uint32_t va) { return (int16_t)rd16(va); }        /* MOVSX r32, word */

#define G_FACE_COUNT  0x46ff4au    /* word: faces left to draw */
#define G_FACE_END    0x4b4620u    /* dword: byte offset of the last visible face (-4: none) */
#define G_FACE_DEPTH  0x4b84a4u    /* dwords: the visible faces' average depths */
#define G_FACE_LIST   0x4b8c74u    /* dwords: the visible faces, sorted with them */
#define G_SCREEN_XY   0x4b7504u    /* per vertex: projected x, y words */
#define G_VERTEX_Z    0x4b7cd4u    /* per vertex: depth dword */
#define G_VERTICES    0x4b461cu    /* dword: the 3D vertices (12 bytes each; index * 3) */
#define G_LIGHT       0x4b94b4u    /* three dwords: the light direction */
#define G_POLYGON     0x4b9444u    /* the polygon's screen points for the fill routines */

/*
 * Render_DrawFaces (0x44360c): draws 0x46ffa4 faces of 0x14 bytes from ESI
 * (flags, vertex list (count at -4), vertex step, shade count, shade table or
 * colour). One-sided faces (flag bit 0 clear) facing away (the cross product
 * of their first three screen points) are dropped; the others are sorted by
 * their average vertex depth, far ones first (Core_SortDescending). Faces with
 * flag bit 1 are shaded: the cosine between their normal and the light (the
 * normal's length through the FPU) picks the colour from their shade table.
 * Each face's points then go to G_POLYGON (offset by the view's centre
 * 0x4b4618/0x4b461a) and it is filled by 0x44392c (flags >> 3 == 0) or by the
 * textured filler 0x443d40 (with the flags - 8 in EDI).
 */
HAND_FN(44360c)
{
    regs32 r = regs_load();
    wr16(G_FACE_COUNT, 0);
    wr32(G_FACE_END, 0xfffffffcu);
    do {                                                           /* cull and measure */
        x86_push(r.esi);
        test32(rd32(r.esi), 1);
        const bool two_sided = !flag(R_ZF);
        r.ebp = rd32(r.esi + 8);
        r.esi = rd32(r.esi + 4);
        bool visible = true;
        if (!two_sided) {
            r.edi = rd32(r.esi);
            r.ebx = (uint32_t)sxw(r.edi + G_SCREEN_XY);
            r.edx = (uint32_t)sxw(r.edi + G_SCREEN_XY + 2);
            r.edi = rd32(r.ebp + r.esi);
            r.eax = (uint32_t)sxw(r.edi + G_SCREEN_XY);
            r.ecx = (uint32_t)sxw(r.edi + G_SCREEN_XY + 2);
            r.edi = rd32(r.esi + r.ebp * 2);
            r.ebp = (uint32_t)sxw(r.edi + G_SCREEN_XY);
            r.edi = (uint32_t)sxw(r.edi + G_SCREEN_XY + 2);
            r.ebp = sub32(r.ebp, r.eax);
            r.edi = sub32(r.edi, r.ecx);
            r.eax = sub32(r.eax, r.ebx);
            r.ecx = sub32(r.ecx, r.edx);
            r.eax = imul32(r.eax, r.edi);
            r.ecx = imul32(r.ecx, r.ebp);
            r.eax = sub32(r.eax, r.ecx);
            visible = !flag(R_SF);                                 /* JS: facing away */
        }
        if (visible) {
            r.ecx = rd32(r.esi - 4);
            r.ebx = 0;
            test32(0, 0);                                          /* XOR EBX, EBX */
            r.ebp = r.ecx;
            do {
                r.eax = rd32(r.esi);
                r.esi = add32(r.esi, 4);
                r.ebx = add32(r.ebx, rd32(r.eax + G_VERTEX_Z));
                r.ecx = dec32(r.ecx);
            } while (!flag(R_ZF));
            r.edx = 0;
            test32(0, 0);                                          /* XOR EDX, EDX */
            r.eax = (uint32_t)RT_UDIV(r.ebx, r.ebp);               /* DIV EBP */
            r.edx = (uint32_t)RT_UREM(r.ebx, r.ebp);
            wr32(G_FACE_END, add32(rd32(G_FACE_END), 4));
            r.ebx = rd32(ESP);
            r.esi = rd32(G_FACE_END);
            wr32(r.esi + G_FACE_DEPTH, r.eax);
            wr32(r.esi + G_FACE_LIST, r.ebx);
            wr16(G_FACE_COUNT, inc16(rd16(G_FACE_COUNT)));
        }
        r.esi = x86_pop();
        r.esi = add32(r.esi, 0x14);
        wr16(0x46ffa4u, dec16(rd16(0x46ffa4u)));
    } while (!flag(R_ZF));

    r.ebx = 0;
    test32(0, 0);                                                  /* XOR EBX, EBX */
    cmp16(rd16(G_FACE_COUNT), 0);
    if (flag(R_ZF)) { regs_store(&r); x86_ret(); return; }
    r.ecx = rd32(G_FACE_END);
    r.ebp = G_FACE_DEPTH;
    r.eax = G_FACE_LIST;
    regs_store(&r);
    HAND_CALL(f_4438bc, 0x4436e6u);
    r = regs_load();
    r.edi = G_FACE_LIST;
    do {                                                           /* shade, project, fill */
        x86_push(r.edi);
        r.esi = rd32(r.edi);
        x86_push(r.esi);
        r.eax = rd32(r.esi);
        r.esi = add32(r.esi, 4);
        wr32(0x46ff80u, r.eax);
        test32(r.eax, 2);
        const bool shaded = !flag(R_ZF);
        r.eax = rd32(r.esi + 0xc);
        r.esi = rd32(r.esi);
        if (shaded) {
            const uint32_t verts = rd32(G_VERTICES);
            r.edi = imul32(rd32(r.esi), 3);
            r.edi = add32(r.edi, verts);
            r.eax = rd32(r.edi); r.ebx = rd32(r.edi + 4); r.ecx = rd32(r.edi + 8);
            r.edi = imul32(rd32(r.esi + 8), 3);
            r.edi = add32(r.edi, rd32(G_VERTICES));
            r.edx = rd32(r.edi); r.ebp = rd32(r.edi + 4); r.edi = rd32(r.edi + 8);
            r.esi = imul32(rd32(r.esi + 4), 3);
            r.esi = add32(r.esi, rd32(G_VERTICES));
            r.eax = sub32(r.eax, rd32(r.esi));
            r.ebx = sub32(r.ebx, rd32(r.esi + 4));
            r.ecx = sub32(r.ecx, rd32(r.esi + 8));
            r.edx = sub32(r.edx, rd32(r.esi));
            r.ebp = sub32(r.ebp, rd32(r.esi + 4));
            r.edi = sub32(r.edi, rd32(r.esi + 8));
            r.eax = sar32(r.eax, 0xe); r.ebx = sar32(r.ebx, 0xe); r.ecx = sar32(r.ecx, 0xe);
            r.edx = sar32(r.edx, 0xe); r.ebp = sar32(r.ebp, 0xe);
            r.esi = r.eax;
            r.edi = sar32(r.edi, 0xe);
            r.esi = imul32(r.esi, r.ebp);
            r.eax = imul32(r.eax, r.edi);
            r.ebp = imul32(r.ebp, r.ecx);
            r.edi = imul32(r.edi, r.ebx);
            r.ecx = imul32(r.ecx, r.edx);
            r.ebx = imul32(r.ebx, r.edx);
            r.edi = sub32(r.edi, r.ebp);                           /* the normal: EDI, ECX, ESI */
            r.ecx = sub32(r.ecx, r.eax);
            r.esi = sub32(r.esi, r.ebx);
            r.edi = sar32(r.edi, 4);
            r.ecx = sar32(r.ecx, 4);
            x86_push(r.edi);
            r.esi = sar32(r.esi, 4);
            r.edi = imul32(r.edi, r.edi);
            r.ebp = r.ecx;
            r.ecx = imul32(r.ecx, r.ecx);
            r.eax = r.esi;
            r.eax = imul32(r.eax, r.eax);
            r.edi = add32(r.edi, r.ecx);
            r.edi = add32(r.edi, r.eax);
            wr32(0x46ff5cu, r.edi);
            const int32_t len_in = (int32_t)r.edi;
            r.eax = x86_pop();
            r.eax = imul32(r.eax, rd32(G_LIGHT));                  /* the dot product with the light */
            r.ebp = imul32(r.ebp, rd32(G_LIGHT + 4));
            r.esi = imul32(r.esi, rd32(G_LIGHT + 8));
            r.eax = add32(r.eax, r.ebp);
            r.eax = add32(r.eax, r.esi);
            r.edx = (r.eax & 0x80000000u) ? 0xffffffffu : 0;      /* CDQ */
            const uint32_t len = x87_isqrt(0x46ff5cu, len_in, 0x4437b6u);
            r.esi = rd32(ESP);
            r.ebx = rd32(r.esi + 0xc);
            r.eax = muldiv32((int32_t)r.eax, 1, (int32_t)len, &r.edx);   /* IDIV dword [len] */
            r.edi = rd32(r.esi + 0x10);
            r.eax = imul32(r.eax, r.ebx);
            r.eax = sar32(r.eax, 8);
            if (flag(R_SF)) { r.eax = 0; test32(0, 0); }           /* XOR EAX, EAX */
            cmp32(r.eax, r.ebx);
            if (!cond_l()) r.eax = r.ebx;
            r.eax = rd32(r.edi + r.eax * 4);
        }
        wr32(0x46ff78u, r.eax);                                    /* the colour */
        r.esi = x86_pop();
        r.edi = G_POLYGON;
        r.esi = rd32(r.esi + 4);
        wr32(0x46ff5cu, r.edi);
        r.ecx = rd32(r.esi - 4);
        r.ebx = rd16(0x4b461au);
        set_lo16(&r.ebp, rd16(0x4b4618u));
        set_flag(R_CF, ((r.ebx >> 16) & 1) != 0);                  /* SHL EBX, 0x10 */
        r.ebx <<= 16;
        flags_szp32(r.ebx);
        x86_push(r.ecx);
        do {
            r.eax = rd32(r.esi);
            r.esi = add32(r.esi, 4);
            r.eax = rd32(r.eax + G_SCREEN_XY);
            set_lo16(&r.edx, lo16(r.eax));
            r.eax = sar32(r.eax, 2);
            set_lo16(&r.edx, sar16n(lo16(r.edx), 2));
            r.eax = add32(r.eax, r.ebx);
            set_lo16(&r.edx, add16(lo16(r.edx), lo16(r.ebp)));
            set_lo16(&r.eax, lo16(r.edx));
            wr32(r.edi, r.eax);
            r.edi = add32(r.edi, 4);
            r.ecx = dec32(r.ecx);
        } while (!flag(R_ZF));
        r.ecx = x86_pop();
        r.edi = rd32(0x46ff80u);
        r.edi &= 0xfffffff8u;
        test32(r.edi, 0xffffffffu);
        regs_store(&r);
        if (flag(R_ZF)) {
            HAND_CALL(f_44392c, 0x44383du);
        } else {
            WR_REG(R_EDI, 4, sub32(r.edi, 8));
            HAND_CALL(f_443d40, 0x443847u);
        }
        r = regs_load();
        r.edi = x86_pop();
        r.edi = add32(r.edi, 4);
        wr16(G_FACE_COUNT, dec16(rd16(G_FACE_COUNT)));
    } while (!flag(R_ZF));
    regs_store(&r);
    x86_ret();
}

/*
 * Math_RotateVertices (0x4431db) and Math_RotateVerticesScaled (0x44334b):
 * the rotation matrix of the three angles at EBP (words: +0, +8, +4, byte
 * offsets into the sine/cosine pairs at arena + 0x1f4f74, 2.14 fixed point)
 * is patched into the nine IMUL immediates of the transform loop, which then
 * rotates the 0x46ffa4 vertices (three dwords each) from ESI to EDI; the
 * second variant scales the results back by 2^14. setup[] are the fields in
 * the order the set-up writes them, loop[] in the order the loop reads them.
 */
typedef struct { uint32_t setup[9], loop[9]; bool shift; } rotate_code;

static void rotate_vertices(const rotate_code *k)
{
    regs32 r = regs_load();
    x86_push(r.esi);
    x86_push(r.edi);
    r.edx = rd32(G_ARENA_BASE);
    const uint32_t a = r.ebp;
    r.esi = rd16(a); r.edi = rd16(a + 8); r.ebp = rd16(a + 4);
    r.esi = add32(r.esi, r.edx); r.edi = add32(r.edi, r.edx); r.ebp = add32(r.ebp, r.edx);
    r.eax = (uint32_t)sxw(r.esi + 0x1f4f74u); r.ebx = (uint32_t)sxw(r.esi + 0x1f4f76u);
    r.ecx = (uint32_t)sxw(r.edi + 0x1f4f74u); r.edx = (uint32_t)sxw(r.edi + 0x1f4f76u);
    r.esi = (uint32_t)sxw(r.ebp + 0x1f4f74u); r.edi = (uint32_t)sxw(r.ebp + 0x1f4f76u);
    const uint32_t t0 = 0x4b9494u, t1 = 0x4b9498u, t2 = 0x4b949cu, t3 = 0x4b94a0u;
    r.ebp = sar32(imul32(r.eax, r.edi), 0xe); wr32(t0, r.ebp);
    r.ebp = sar32(imul32(r.ebx, r.edi), 0xe); wr32(t1, r.ebp);
    r.ebp = sar32(imul32(r.eax, r.esi), 0xe); wr32(t2, r.ebp);
    r.ebp = sar32(imul32(r.ebx, r.esi), 0xe); wr32(t3, r.ebp);
    r.ebp = sub32(sar32(imul32(r.ebp, r.ecx), 0xe), rd32(t0)); wr32(k->setup[0], r.ebp);
    r.ebp = sub32(sar32(imul32(rd32(t0), r.ecx), 0xe), rd32(t3)); wr32(k->setup[1], r.ebp);
    r.ebp = add32(sar32(imul32(rd32(t1), r.ecx), 0xe), rd32(t2)); wr32(k->setup[2], r.ebp);
    r.ebp = add32(sar32(imul32(rd32(t2), r.ecx), 0xe), rd32(t1)); wr32(k->setup[3], r.ebp);
    r.ecx = neg32(r.ecx); wr32(k->setup[4], r.ecx);
    r.eax = sar32(imul32(r.eax, r.edx), 0xe); wr32(k->setup[5], r.eax);
    r.ebx = sar32(imul32(r.ebx, r.edx), 0xe); wr32(k->setup[6], r.ebx);
    r.edi = sar32(imul32(r.edi, r.edx), 0xe); wr32(k->setup[7], r.edi);
    r.edx = sar32(imul32(r.edx, r.esi), 0xe); wr32(k->setup[8], r.edx);
    r.edi = x86_pop();
    r.esi = x86_pop();
    r.ecx = rd16(0x46ffa4u);
    do {
        for (unsigned row = 0; row < 3; row++) {
            r.eax = imul32(rd32(r.esi), rd32(k->loop[3 * row]));
            r.ebx = imul32(rd32(r.esi + 8), rd32(k->loop[3 * row + 1]));
            r.eax = add32(r.eax, r.ebx);
            r.ebx = imul32(rd32(r.esi + 4), rd32(k->loop[3 * row + 2]));
            r.eax = add32(r.eax, r.ebx);
            if (k->shift) r.eax = sar32(r.eax, 0xe);
            wr32(r.edi + 4 * row, r.eax);
        }
        r.esi = add32(r.esi, 0xc);
        r.edi = add32(r.edi, 0xc);
        r.ecx = dec32(r.ecx);
    } while (!flag(R_ZF));
    regs_store(&r);
    x86_ret();
}
HAND_FN(4431db)
{
    static const rotate_code k = {
        {0x443338, 0x4432fa, 0x443303, 0x44332f, 0x44330d, 0x443314, 0x44331d, 0x4432f3, 0x443328},
        {0x4432f3, 0x4432fa, 0x443303, 0x44330d, 0x443314, 0x44331d, 0x443328, 0x44332f, 0x443338}, false};
    rotate_vertices(&k);
}
HAND_FN(44334b)
{
    static const rotate_code k = {
        {0x4434ae, 0x44346a, 0x443473, 0x4434a5, 0x443480, 0x443487, 0x443490, 0x443463, 0x44349e},
        {0x443463, 0x44346a, 0x443473, 0x443480, 0x443487, 0x443490, 0x44349e, 0x4434a5, 0x4434ae}, true};
    rotate_vertices(&k);
}
