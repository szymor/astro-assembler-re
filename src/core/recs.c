/*
 * Loops over 10-byte records (x at 0, y at 5, a colour index at 9) that
 * plot into a 320-byte-pitch (or, for the mask planes, 400-byte-pitch)
 * layer. The 16-bit registers keep the upper halves the caller left: they take
 * part in the addresses. Several operands carry a displacement the program
 * patches into its own code (read from the code bytes, as the generated code does). Each step keeps the flags of the instruction it is
 * (see core.h).
 */
#include "core.h"

typedef struct { uint32_t eax, ebx, ecx, edx, ebp, esi, edi; } loop_regs;

static loop_regs load(void)
{
    return (loop_regs){EAX, EBX, ECX, EDX, EBP, ESI, EDI};
}
static void store(const loop_regs *r)
{
    WR_REG(R_EAX, 4, r->eax); WR_REG(R_EBX, 4, r->ebx); WR_REG(R_ECX, 4, r->ecx);
    WR_REG(R_EDX, 4, r->edx); WR_REG(R_EBP, 4, r->ebp); WR_REG(R_ESI, 4, r->esi);
    WR_REG(R_EDI, 4, r->edi);
}
/* IMUL r16, [esi+5], 0x140 then ADD r16, [esi]: the 16-bit position of the record in a 320-wide layer. */
static uint16_t pos16(uint32_t esi)
{
    const uint16_t p = (uint16_t)((int16_t)rd16(esi + 5) * 0x140);
    return add16(p, rd16(esi));
}
static void set_dh(uint32_t *edx, uint8_t v) { *edx = (*edx & 0xffff00ffu) | (uint32_t)v << 8; }
static void set_dl(uint32_t *edx, uint8_t v) { *edx = (*edx & 0xffffff00u) | v; }
static void set_al(uint32_t *eax, uint8_t v) { *eax = (*eax & 0xffffff00u) | v; }
static void loop_end(loop_regs *r)
{
    r->esi = add32(r->esi, 0xa);
    r->ecx = dec32(r->ecx);
}

/* 4139d0: where the mask at EBP+pos (OR DH) is clear, plot BL. */
HAND_FN(4139d0)
{
    loop_regs r = load();
    do {
        set_lo16(&r.eax, pos16(r.esi));
        const uint32_t at = r.eax + r.ebp;
        const uint8_t v = rd8(at) | (uint8_t)(r.edx >> 8);
        test8(v);
        wr8(at, v);
        if (v == 0) wr8(at, (uint8_t)r.ebx);
        loop_end(&r);
    } while (!flag(R_ZF));
    store(&r);
    x86_ret();
}

/* 4139e8: the same, plotting the colour-table entry (table at EDI) of the record's index. */
HAND_FN(4139e8)
{
    loop_regs r = load();
    do {
        set_lo16(&r.eax, pos16(r.esi));
        const uint32_t at = r.eax + r.ebp;
        const uint8_t v = rd8(at) | (uint8_t)(r.edx >> 8);
        test8(v);
        wr8(at, v);
        if (v == 0) {
            set_dl(&r.edx, rd8(r.esi + 9));
            set_dl(&r.edx, rd8(r.edx + r.edi));
            wr8(at, (uint8_t)r.edx);
        }
        loop_end(&r);
    } while (!flag(R_ZF));
    store(&r);
    x86_ret();
}

/* 413a79: the layer byte at EDI+pos is remapped through the table at EDX (page in DH). */
HAND_FN(413a79)
{
    loop_regs r = load();
    do {
        set_lo16(&r.ebx, pos16(r.esi));
        set_dh(&r.edx, rd8(r.ebx + r.edi));
        set_al(&r.eax, rd8(r.edx + rd32(0x413a8a)));
        wr8(r.ebx + r.edi, (uint8_t)r.eax);
        loop_end(&r);
    } while (!flag(R_ZF));
    store(&r);
    x86_ret();
}

/* 413a95: the layer byte (as AH) and a byte from the table at EBP (AL, index in the record) select a byte through EAX. */
HAND_FN(413a95)
{
    loop_regs r = load();
    do {
        const uint16_t bx = (uint16_t)((int16_t)rd16(r.esi + 5) * 0x140);
        set_dl(&r.edx, rd8(r.esi + 9));
        set_lo16(&r.ebx, add16(bx, rd16(r.esi)));
        set_al(&r.eax, rd8(r.edx + r.ebp));
        r.eax = (r.eax & 0xffff00ffu) | (uint32_t)rd8(r.ebx + r.edi) << 8;
        set_al(&r.eax, rd8(r.eax + rd32(0x413aac)));
        wr8(r.ebx + r.edi, (uint8_t)r.eax);
        loop_end(&r);
    } while (!flag(R_ZF));
    store(&r);
    x86_ret();
}

/* 413b94: where the mask byte at EBP (400-byte pitch) is set, remap the layer byte through the table at EDX. */
HAND_FN(413b94)
{
    loop_regs r = load();
    do {
        r.ebp = (uint32_t)((int32_t)rd32(r.esi + 5) * 0x190);
        const uint16_t bx = (uint16_t)((int16_t)rd16(r.esi + 5) * 0x140);
        r.ebp = add32(r.ebp, rd32(r.esi));
        set_lo16(&r.ebx, add16(bx, rd16(r.esi)));
        cmp8(rd8(r.ebp + rd32(0x413ba8)), 0);
        if (!flag(R_ZF)) {
            set_dh(&r.edx, rd8(r.ebx + r.edi));
            set_al(&r.eax, rd8(r.edx + rd32(0x413bb4)));
            wr8(r.ebx + r.edi, (uint8_t)r.eax);
        }
        loop_end(&r);
    } while (!flag(R_ZF));
    store(&r);
    x86_ret();
}

/* 413cbb: the same, but the plot also needs the source byte at EBX to be set (AND with the mask byte). */
HAND_FN(413cbb)
{
    loop_regs r = load();
    do {
        r.ebp = (uint32_t)((int32_t)rd32(r.esi + 5) * 0x190);
        const uint16_t bx = (uint16_t)((int16_t)rd16(r.esi + 5) * 0x140);
        r.ebp = add32(r.ebp, rd32(r.esi));
        set_lo16(&r.ebx, add16(bx, rd16(r.esi)));
        set_al(&r.eax, rd8(r.ebx + rd32(0x413ccf)));
        r.esi = add32(r.esi, 0xa);
        const uint8_t al = (uint8_t)r.eax & rd8(r.ebp + rd32(0x413cd8));
        set_al(&r.eax, al);
        test8(al);
        if (!flag(R_ZF)) {
            set_dh(&r.edx, rd8(r.ebx + r.edi));
            set_al(&r.eax, rd8(r.edx + rd32(0x413ce3)));
            wr8(r.ebx + r.edi, (uint8_t)r.eax);
        }
        r.ecx = dec32(r.ecx);
    } while (!flag(R_ZF));
    store(&r);
    x86_ret();
}

/* 413da1: like 413b94 with the mask plane at EBP indexed like the layer (320-byte pitch). */
HAND_FN(413da1)
{
    loop_regs r = load();
    do {
        set_lo16(&r.ebx, pos16(r.esi));
        cmp8(rd8(r.ebx + r.ebp), 0);
        if (!flag(R_ZF)) {
            set_dh(&r.edx, rd8(r.ebx + r.edi));
            set_al(&r.eax, rd8(r.edx + rd32(0x413db5)));
            wr8(r.ebx + r.edi, (uint8_t)r.eax);
        }
        loop_end(&r);
    } while (!flag(R_ZF));
    store(&r);
    x86_ret();
}

/*
 * The drivers of the record sets (0x413ab7, 0x413bc2, 0x413cee): two sets of
 * records (0x28 at arena + 0x4c2aa with the colour table at 0x4a9abe, 0x46 at
 * arena + 0x4bfee with 0x4a9ab6) are plotted here, through the remap table
 * the frame constants patched in (the dword field of the MOV AL, [EAX + ...]),
 * and two more (0x78 at arena + 0x4bb3e, colour 0xaa; 0x12c at arena + 0x4af86,
 * colour 0x95) by the matching loop above, the second by falling into it.
 */
typedef struct { uint32_t base, count, colours, remap_field, mask_field, src_field; } rec_set;

/* IMUL r16, r/m16, imm: the low word; CF = OF = the product did not fit. */
static uint16_t imul16(uint16_t a, int16_t b)
{
    const int32_t p = (int16_t)a * b;
    set_flag(R_CF, (int16_t)p != p);
    set_flag(R_OF, (int16_t)p != p);
    return (uint16_t)p;
}
/* IMUL r32, r/m32, imm. */
static uint32_t imul32i(uint32_t a, int32_t b)
{
    const int64_t p = (int64_t)(int32_t)a * b;
    set_flag(R_CF, (int32_t)p != p);
    set_flag(R_OF, (int32_t)p != p);
    return (uint32_t)p;
}
/* AL = colours[DL], AH = the frame byte, the remap table entry back. */
static void remap_plot(loop_regs *r, uint32_t colours, uint32_t remap_field)
{
    set_al(&r->eax, rd8((r->edx & 0xffffffffu) + colours));
    r->eax = (r->eax & 0xffff00ffu) | (uint32_t)rd8(r->ebx + r->edi) << 8;
    set_al(&r->eax, rd8(r->eax + rd32(remap_field)));
    wr8(r->ebx + r->edi, (uint8_t)r->eax);
}
static void sets_start(loop_regs *r)
{
    r->edi = rd32(0x55c7f4u);                                       /* the frame buffer */
    r->edx = 0; r->ebx = 0; r->eax = 0;
    test32(0, 0);                                                   /* XOR EAX, EAX */
}
/* The last two sets, through the loop at `tail` (0x413b94, 0x413cbb, 0x413da1). */
static void sets_tail(loop_regs *r, uint32_t call_ret, void (*tail)(void))
{
    r->esi = add32(rd32(G_ARENA_BASE), 0x4bb3e);
    r->edx = 0xaa;
    r->ecx = 0x78;
    store(r);
    HAND_CALL(tail, call_ret);
    *r = load();
    r->esi = add32(rd32(G_ARENA_BASE), 0x4af86);
    r->edx = 0x95;
    r->ecx = 0x12c;
    store(r);
    tail();                                                         /* falls into it */
}

/* Core_RemapSetsMask400 (0x413ab7): records where the mask (400-byte pitch,
 * base EBX, patched into the loops) is set are remapped. */
HAND_FN(413ab7)
{
    loop_regs r = load();
    const uint32_t mask = r.ebx;
    wr32(0x413afdu, mask); wr32(0x413b44u, mask); wr32(0x413ba8u, mask);
    static const rec_set set[2] = {{0x4c2aa, 0x28, 0x4a9abe, 0x413b0f, 0x413afd, 0},
                                   {0x4bfee, 0x46, 0x4a9ab6, 0x413b56, 0x413b44, 0}};
    for (unsigned s = 0; s < 2; s++) {
        r.esi = add32(rd32(G_ARENA_BASE), set[s].base);
        if (s == 0) sets_start(&r);
        r.ecx = set[s].count;
        do {
            r.ebp = imul32i(rd32(r.esi + 5), 0x190);
            set_lo16(&r.ebx, imul16(rd16(r.esi + 5), 0x140));
            set_dl(&r.edx, rd8(r.esi + 9));
            r.ebp = add32(r.ebp, rd32(r.esi));
            set_lo16(&r.ebx, add16(lo16(r.ebx), rd16(r.esi)));
            cmp8(rd8(r.ebp + rd32(set[s].mask_field)), 0);
            if (!flag(R_ZF)) remap_plot(&r, set[s].colours, set[s].remap_field);
            r.esi = add32(r.esi, 0xa);
            r.ecx = dec32(r.ecx);
        } while (!flag(R_ZF));
    }
    sets_tail(&r, 0x413b7eu, f_413b94);
}

/* Core_RemapSetsMaskAnd (0x413bc2): the same where both the source byte
 * (320-byte pitch, base EBP) and the mask byte (base EBX) are set. */
HAND_FN(413bc2)
{
    loop_regs r = load();
    wr32(0x413cd8u, r.ebx); wr32(0x413c23u, r.ebx); wr32(0x413c6fu, r.ebx);
    wr32(0x413ccfu, r.ebp); wr32(0x413c1au, r.ebp); wr32(0x413c66u, r.ebp);
    static const rec_set set[2] = {{0x4c2aa, 0x28, 0x4a9abe, 0x413c34, 0x413c23, 0x413c1a},
                                   {0x4bfee, 0x46, 0x4a9ab6, 0x413c80, 0x413c6f, 0x413c66}};
    for (unsigned s = 0; s < 2; s++) {
        r.esi = add32(rd32(G_ARENA_BASE), set[s].base);
        if (s == 0) sets_start(&r);
        r.ecx = set[s].count;
        do {
            r.ebp = imul32i(rd32(r.esi + 5), 0x190);
            set_lo16(&r.ebx, imul16(rd16(r.esi + 5), 0x140));
            r.ebp = add32(r.ebp, rd32(r.esi));
            set_lo16(&r.ebx, add16(lo16(r.ebx), rd16(r.esi)));
            set_dl(&r.edx, rd8(r.esi + 9));
            set_al(&r.eax, rd8(r.ebx + rd32(set[s].src_field)));
            r.esi = add32(r.esi, 0xa);
            const uint8_t al = (uint8_t)(r.eax & rd8(r.ebp + rd32(set[s].mask_field)));
            set_al(&r.eax, al);
            test8(al);                                              /* AND AL, [EBP + ...] */
            if (!flag(R_ZF)) remap_plot(&r, set[s].colours, set[s].remap_field);
            r.ecx = dec32(r.ecx);
        } while (!flag(R_ZF));
    }
    sets_tail(&r, 0x413ca5u, f_413cbb);
}

/* Core_RemapSetsMask320 (0x413cee): the same with the mask plane at EBP
 * indexed like the frame (320-byte pitch). */
HAND_FN(413cee)
{
    loop_regs r = load();
    static const rec_set set[2] = {{0x4c2aa, 0x28, 0x4a9abe, 0x413d2b, 0, 0},
                                   {0x4bfee, 0x46, 0x4a9ab6, 0x413d66, 0, 0}};
    for (unsigned s = 0; s < 2; s++) {
        r.esi = add32(rd32(G_ARENA_BASE), set[s].base);
        if (s == 0) sets_start(&r);
        r.ecx = set[s].count;
        do {
            set_lo16(&r.ebx, imul16(rd16(r.esi + 5), 0x140));
            set_dl(&r.edx, rd8(r.esi + 9));
            set_lo16(&r.ebx, add16(lo16(r.ebx), rd16(r.esi)));
            r.esi = add32(r.esi, 0xa);
            cmp8(rd8(r.ebx + r.ebp), 0);
            if (!flag(R_ZF)) remap_plot(&r, set[s].colours, set[s].remap_field);
            r.ecx = dec32(r.ecx);
        } while (!flag(R_ZF));
    }
    sets_tail(&r, 0x413d8bu, f_413da1);
}
