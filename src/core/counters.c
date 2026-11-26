/*
 * Loops over 10-byte records, per-player counters and set-up stores. Each step
 * keeps the flags of the instruction it is (see core.h).
 */
#include "core.h"

static uint8_t add8(uint8_t a, uint8_t b)
{
    const uint8_t r = (uint8_t)(a + b);
    set_flag(R_CF, r < a);
    set_flag(R_OF, ((a ^ r) & (b ^ r) & 0x80) != 0);
    flags_szp8(r);
    return r;
}
static uint8_t dec8(uint8_t a)
{
    const uint8_t r = (uint8_t)(a - 1);
    set_flag(R_OF, a == 0x80);
    flags_szp8(r);
    return r;
}
static void flags_xor_self(void)
{
    set_flag(R_CF, false); set_flag(R_OF, false); set_flag(R_SF, false);
    set_flag(R_ZF, true); set_flag(0x202, true);
}

/* 4138f9: ECX records of 10 bytes from EDI: byte 0 is decremented, then ANDed with BL. */
HAND_FN(4138f9)
{
    uint32_t edi = EDI, ecx = ECX;
    const uint8_t bl = (uint8_t)EBX;
    do {
        uint8_t b = dec8(rd8(edi));
        b &= bl;
        test8(b);
        wr8(edi, b);
        edi = add32(edi, 0xa);
        ecx = dec32(ecx);
    } while (!flag(R_ZF));
    WR_REG(R_ECX, 4, ecx);
    WR_REG(R_EDI, 4, edi);
    x86_ret();
}

/* 413953: ECX records of 10 bytes from EDI: word 0 += BX, and reduced by DX if that reaches it. */
HAND_FN(413953)
{
    uint32_t edi = EDI, ecx = ECX;
    const uint16_t bx = (uint16_t)EBX, dx = (uint16_t)EDX;
    do {
        uint16_t w = add16(rd16(edi), bx);
        wr16(edi, w);
        cmp16(w, dx);
        if (!flag(R_CF)) wr16(edi, sub16(w, dx));
        edi = add32(edi, 0xa);
        ecx = dec32(ecx);
    } while (!flag(R_ZF));
    WR_REG(R_ECX, 4, ecx);
    WR_REG(R_EDI, 4, edi);
    x86_ret();
}

/* 4240e1 (4243b6, the second player's, is not called by any recording): the byte at `value` goes up by one (not below 0), at most to the byte at `limit`. */
#define COUNT_UP_LIMITED(addr, value, limit)                                \
    HAND_FN(addr)                                                           \
    {                                                                       \
        WR_REG(R_EBX, 1, 1);                                                \
        uint8_t al = add8(rd8(value), 1);                                   \
        if (flag(R_SF)) { al = 0; flags_xor_self(); }                       \
        cmp8(al, rd8(limit));                                               \
        if (flag(R_CF) == 0 && !flag(R_ZF)) al = rd8(limit);                \
        WR_REG(R_EAX, 1, al);                                               \
        wr8(value, al);                                                     \
        x86_ret();                                                          \
    }
COUNT_UP_LIMITED(4240e1, 0x47004e, 0x47004c)

/*
 * 424120 / 4243f5: a counter (at most 0x20) grows by one and the slot it
 * names in the table at `table` is given a lifetime of 0x48 and the value 1.
 */
static void grow_slot(uint32_t count_at, uint32_t table)
{
    uint32_t eax = rd8(count_at);
    WR_REG(R_EAX, 4, eax);
    cmp8((uint8_t)eax, 0x20);
    if (!flag(R_ZF)) {
        uint8_t al = inc8((uint8_t)eax);
        wr8(count_at, al);
        al = sub8(al, 0x19);
        set_flag(R_CF, ((al >> 6) & 1) != 0);                 /* SHL AL, 2 */
        al = (uint8_t)(al << 2);
        flags_szp8(al);
        eax = al;
        WR_REG(R_EAX, 4, eax);
        const uint32_t esi = add32(table, eax);
        WR_REG(R_ESI, 4, esi);
        wr16(esi, 0x48);
        wr16(esi + 2, 1);
    }
    x86_ret();
}
HAND_FN(424120) { grow_slot(0x47004c, 0x519d08); }

/* 42abf9: on every fourth tick four timers count down by 12, wrapping to 204. */
HAND_FN(42abf9)
{
    test32(rd32(0x46ffa0), 3);
    if (flag(R_ZF)) {
        static const uint32_t timer[4] = {0x497dae, 0x497e5e, 0x497f0e, 0x497fbe};
        for (int i = 0; i < 4; i++) {
            wr32(timer[i], sub32(rd32(timer[i]), 0xc));
            if (flag(R_SF)) wr32(timer[i], 0xcc);
        }
    }
    x86_ret();
}

/*
 * 41b710 / 41b9b6: the first `count` active entries (word 0 non-zero) of a list
 * with the given stride get word 0 reloaded from the word at offset 1 of the
 * frame their field `frame_at` selects in the table at `frames`.
 */
static void reload_entries(uint32_t count_at, uint32_t base, uint32_t stride, uint32_t frame_at, uint32_t frames)
{
    uint16_t cx = 0;
    test16(rd16(count_at));
    cx = rd16(count_at);
    WR_REG(R_ECX, 2, cx);
    if (cx) {
        uint32_t edi = base;
        WR_REG(R_EDI, 4, edi);
        edi = add32(edi, stride);
        do {
            cmp16(rd16(edi), 0);
            if (!flag(R_ZF)) {
                const uint32_t esi0 = rd16(edi + frame_at);
                const uint32_t esi = rd32(esi0 + frames);
                WR_REG(R_ESI, 4, esi);
                const uint16_t bx = rd16(esi + 1);
                WR_REG(R_EBX, 2, bx);
                wr16(edi, bx);
                cx = dec16(cx);
                if (flag(R_ZF)) break;
            }
            edi = add32(edi, stride);
        } while (1);
        WR_REG(R_ECX, 2, cx);
        WR_REG(R_EDI, 4, edi);
    }
    x86_ret();
}
HAND_FN(41b710) { reload_entries(0x4b1048, 0x51f498, 0xc, 0xa, 0x4b1050); }
HAND_FN(41b9b6) { reload_entries(0x4b135a, 0x51fb94, 0x18, 0x16, 0x4b1634); }
