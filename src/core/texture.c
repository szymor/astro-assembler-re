/*
 * Pixel loops whose constants the game patches into their own instructions:
 * a remapped span and the affine texture mapper. The patched fields are read
 * from (and, where the routine patches itself, written to) the code bytes in
 * emulated memory, as the original does.
 */
#include "core.h"

static uint32_t inc32(uint32_t a)
{
    const uint32_t r = a + 1;
    set_flag(R_OF, a == 0x7fffffffu);
    flags_szp32(r);
    return r;
}
static void set_al(uint32_t *eax, uint8_t v) { *eax = (*eax & 0xffffff00u) | v; }

/*
 * Render_RemapSpan2_q (0x442e3d): ECX pixels from x = EDX (clipped to 0..0x13f
 * with EAX the span's length) on the frame row at EDI are remapped through the
 * table whose base is patched into 0x442e68 (the 256 entries of the page in
 * 0x4ab684's second byte); if the row EBP is negative only the second row is
 * done: 0x46ff6c rows further (when it lies in 0..0xc7, EDI moving by
 * 0x46ff68), through the table at 0x442e8d.
 */
HAND_FN(442e3d)
{
    uint32_t eax = EAX, ecx = ECX, edx = EDX, edi = EDI, ebx = EBX;
    cmp32(edx, 0);
    if (cond_l()) { edi = sub32(edi, edx); ecx = add32(ecx, edx); }    /* clipped on the left */
    edx = add32(edx, eax);
    edx = sub32(edx, 0x140);
    if (!cond_le()) ecx = sub32(ecx, edx);                             /* and on the right */
    test32(ecx, 0xffffffffu);
    if (cond_le()) goto done;
    edx = EBP;
    eax = rd32(0x4ab684u);
    test32(edx, 0xffffffffu);
    if (!flag(R_SF)) {
        x86_push(edi);
        ebx = ecx;
        do {
            set_al(&eax, rd8(edi));
            set_al(&eax, rd8(eax + rd32(0x442e68u)));
            wr8(edi, (uint8_t)eax);
            edi = inc32(edi);
            ebx = dec32(ebx);
        } while (!flag(R_ZF));
        edi = x86_pop();
    }
    edx = add32(edx, rd32(0x46ff6cu));
    if (cond_l()) goto done;
    cmp32(edx, 0xc8);
    if (cond_ge()) goto done;
    edi = add32(edi, rd32(0x46ff68u));
    do {
        set_al(&eax, rd8(edi));
        set_al(&eax, rd8(eax + rd32(0x442e8du)));
        wr8(edi, (uint8_t)eax);
        edi = inc32(edi);
        ecx = dec32(ecx);
    } while (!flag(R_ZF));
done:
    WR_REG(R_EAX, 4, eax); WR_REG(R_EBX, 4, ebx); WR_REG(R_ECX, 4, ecx); WR_REG(R_EDX, 4, edx); WR_REG(R_EDI, 4, edi);
    x86_ret();
}

/* SHLD r32, r32, n (p-code semantics): CF = the last bit out, OF only for
 * n = 1, SF, ZF, PF of the result; nothing changes for n = 0. */
static uint32_t shld32(uint32_t dst, uint32_t src, unsigned n)
{
    n &= 0x1f;
    if (!n) return dst;
    const uint32_t r = dst << n | src >> (32 - n);
    set_flag(R_CF, ((dst << (n - 1)) & 0x80000000u) != 0);
    if (n == 1) set_flag(R_OF, flag(R_CF) != ((r & 0x80000000u) != 0));
    flags_szp32(r);
    return r;
}
/* SBB r32, 0 and ADC r32, 0: subtract / add the carry. */
static uint32_t sbb0(uint32_t a)
{
    const uint32_t c = flag(R_CF), r = a - c;
    set_flag(R_CF, a < c);
    set_flag(R_OF, c && a == 0x80000000u);
    flags_szp32(r);
    return r;
}
static uint32_t adc0(uint32_t a)
{
    const uint32_t c = flag(R_CF), r = a + c;
    set_flag(R_CF, r < a);
    set_flag(R_OF, c && a == 0x7fffffffu);
    flags_szp32(r);
    return r;
}

/*
 * Render_TextureAffine (0x442ecc): an affine-mapped, colour-keyed (0 = clear)
 * texture into the frame buffer. 0x442f91 and 0x442fa8 work out the steps
 * from the angle in ESI; the routine then patches its own inner loop: the
 * texture's width as a shift (the lowest set bit of 0x4b3be4, into the SHLD),
 * the texture (0x4b3be8), the per-pixel steps (EBP for u, EBX for v), the row
 * skip (0x140 less the width 0x4b3bd4) and the per-row steps (0x4b3bfc,
 * 0x4b3bf8). Rows (0x4b3bd8) of width pixels from (0x4b3bb4, 0x4b3bb8), the
 * texture coordinates starting at (0x4b3bf0, 0x4b3bf4).
 */
HAND_FN(442ecc)
{
    HAND_CALL(f_442f91, 0x442ed1u);
    HAND_CALL(f_442fa8, 0x442ed6u);
    regs32 r = regs_load();
    r.edi = (uint32_t)((int64_t)(int32_t)rd32(0x4b3bb8u) * 0x140);
    set_flag(R_CF, (int64_t)(int32_t)r.edi != (int64_t)(int32_t)rd32(0x4b3bb8u) * 0x140);
    set_flag(R_OF, flag(R_CF));
    r.edi = add32(r.edi, rd32(0x4b3bb4u));
    r.edi = add32(r.edi, rd32(0x55c7f4u));
    {                                                              /* BSF EAX, [0x4b3be4] */
        const uint32_t w = rd32(0x4b3be4u);
        set_flag(R_ZF, w == 0);
        uint32_t i = 0;
        if (w) while (!((w >> i) & 1)) i++;
        r.eax = i;
    }
    wr8(0x442f58u, (uint8_t)r.eax);
    r.eax = rd32(0x4b3be8u);
    wr32(0x442f5bu, r.eax);
    wr32(0x442f65u, r.ebp);
    wr32(0x442f6cu, r.ebx);
    r.eax = sub32(0x140, rd32(0x4b3bd4u));
    wr32(0x442f7au, r.eax);
    r.eax = rd32(0x4b3bfcu);
    wr32(0x442f80u, r.eax);
    r.eax = rd32(0x4b3bf8u);
    wr32(0x442f86u, r.eax);
    r.edx = rd32(0x4b3bf0u);
    r.ecx = rd32(0x4b3bf4u);
    r.esi = 0;
    test32(0, 0);                                                  /* XOR ESI, ESI */
    r.ebp = rd32(0x4b3bd8u);
    do {
        x86_push(r.ecx);
        x86_push(r.edx);
        r.ebx = rd32(0x4b3bd4u);
        do {
            set_lo16(&r.esi, lo16(r.ecx));
            r.eax = 0;
            test32(0, 0);                                          /* XOR EAX, EAX */
            r.esi = shld32(r.esi, r.edx, rd8(0x442f58u));
            const uint8_t texel = rd8(r.esi + rd32(0x442f5bu));
            set_al(&r.eax, (uint8_t)(r.eax | texel));
            test8((uint8_t)r.eax);                                 /* OR AL, [ESI + texture] */
            if (!flag(R_ZF)) wr8(r.edi, (uint8_t)r.eax);
            r.edx = add32(r.edx, rd32(0x442f65u));
            r.edi = inc32(r.edi);
            r.ecx = sub32(r.ecx, rd32(0x442f6cu));
            r.ecx = sbb0(r.ecx);
            r.ebx = dec32(r.ebx);
        } while (!flag(R_ZF));
        r.edx = x86_pop();
        r.ecx = x86_pop();
        r.edi = add32(r.edi, rd32(0x442f7au));
        r.edx = add32(r.edx, rd32(0x442f80u));
        r.ecx = add32(r.ecx, rd32(0x442f86u));
        r.ecx = adc0(r.ecx);
        r.ebp = dec32(r.ebp);
    } while (!flag(R_ZF));
    regs_store(&r);
    x86_ret();
}
