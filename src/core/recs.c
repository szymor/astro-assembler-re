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
