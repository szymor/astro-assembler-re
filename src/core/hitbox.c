/*
 * Hitboxes and list entries. The box routines read an object (EDI) and leave
 * its box in 16-bit words: CX..DX the x range (less the horizontal scroll at
 * 0x4b1f9a where the object lives in the 400-wide playfield), AX..BX the y
 * range; the upper halves of the registers are kept. Enemy1_TwinPod_Box_q to
 * Enemy9_Turner_Box_q serve the nine enemy classes (each with its own table), in
 * the order the collision routines check them; BossArm_SegmentBox1_q to 5 the
 * five segments of a boss part (the level-2 and level-3 bosses). Each step
 * keeps the flags of the instruction it is, so the last one decides what the
 * caller sees.
 */
#include "core.h"

typedef struct { uint32_t eax, ebx, ecx, edx; } box_regs;

static void box_load(box_regs *r) { r->eax = EAX; r->ebx = EBX; r->ecx = ECX; r->edx = EDX; }
static void box_store(const box_regs *r)
{
    WR_REG(R_EAX, 4, r->eax); WR_REG(R_EBX, 4, r->ebx); WR_REG(R_ECX, 4, r->ecx); WR_REG(R_EDX, 4, r->edx);
}
#define AXV lo16(r.eax)
#define BXV lo16(r.ebx)
#define CXV lo16(r.ecx)
#define DXV lo16(r.edx)
#define SET_AX(v) set_lo16(&r.eax, (v))
#define SET_BX(v) set_lo16(&r.ebx, (v))
#define SET_CX(v) set_lo16(&r.ecx, (v))
#define SET_DX(v) set_lo16(&r.edx, (v))
#define F16(off) rd16(EDI + (off))
#define SCROLL rd16(G_SCROLL_X)

/* SAR r16, n for n >= 2: CF = the last bit out, SF, ZF, PF; OF unchanged. */
static uint16_t sar16(uint16_t a, unsigned n)
{
    set_flag(R_CF, (((int16_t)a >> (n - 1)) & 1) != 0);
    const uint16_t v = (uint16_t)((int16_t)a >> n);
    flags_szp16(v);
    return v;
}

#define BOX_FN(addr) HAND_FN(addr) { box_regs r; box_load(&r);
#define BOX_END box_store(&r); x86_ret(); }

BOX_FN(4342e0)
    SET_CX(F16(6)); SET_AX(F16(4));
    SET_CX(sar16(CXV, 5));
    SET_AX(add16(AXV, F16(0x24)));
    SET_CX(sub16(CXV, SCROLL));
    SET_CX(add16(CXV, F16(0x22)));
    SET_BX(AXV); SET_BX(add16(BXV, F16(0x28)));
    SET_DX(CXV); SET_DX(add16(DXV, F16(0x26)));
BOX_END

BOX_FN(43430a)
    SET_CX(F16(2)); SET_AX(F16(4));
    SET_CX(add16(CXV, CXV)); SET_AX(add16(AXV, AXV));
    SET_CX(sub16(CXV, F16(0x7e))); SET_AX(sub16(AXV, F16(0x80)));
    SET_DX(CXV); SET_BX(AXV);
    SET_DX(add16(DXV, F16(0x82))); SET_BX(add16(BXV, F16(0x84)));
BOX_END

BOX_FN(434338)
    SET_CX(F16(2)); SET_AX(F16(4));
    SET_CX(sub16(CXV, SCROLL)); SET_CX(sub16(CXV, F16(0x56))); SET_AX(sub16(AXV, F16(0x58)));
    SET_DX(CXV); SET_BX(AXV);
    SET_DX(add16(DXV, F16(0x5a))); SET_BX(add16(BXV, F16(0x5c)));
BOX_END

BOX_FN(43435e)
    SET_CX(F16(2)); SET_AX(F16(4));
    SET_CX(sar16(CXV, 5)); SET_AX(add16(AXV, AXV));
    SET_CX(sub16(CXV, SCROLL));
    SET_AX(add16(AXV, F16(0x2a))); SET_CX(add16(CXV, F16(0x28)));
    SET_BX(AXV); SET_DX(CXV);
    SET_BX(add16(BXV, F16(0x2e))); SET_DX(add16(DXV, F16(0x2c)));
BOX_END

BOX_FN(43438b)
    SET_CX(F16(2));
    SET_CX(add16(CXV, F16(0x24))); SET_CX(sub16(CXV, SCROLL));
    SET_DX(CXV); SET_DX(add16(DXV, F16(0x28)));
    SET_AX(F16(4)); SET_AX(add16(AXV, F16(0x26)));
    SET_BX(AXV); SET_BX(add16(BXV, F16(0x2a)));
BOX_END

BOX_FN(4343d4)
    SET_CX(F16(0xe)); SET_AX(F16(0x10));
    SET_CX(add16(CXV, CXV)); SET_AX(add16(AXV, AXV));
    SET_CX(add16(CXV, F16(0x62))); SET_AX(add16(AXV, F16(0x64)));
    SET_DX(CXV); SET_BX(AXV);
    SET_DX(add16(DXV, F16(0x66))); SET_BX(add16(BXV, F16(0x68)));
BOX_END

BOX_FN(4343f9)
    SET_CX(F16(0xa)); SET_AX(F16(0xc));
    SET_CX(add16(CXV, CXV)); SET_AX(add16(AXV, AXV));
    SET_CX(sub16(CXV, SCROLL));
    SET_CX(add16(CXV, F16(0x3a))); SET_CX(add16(CXV, F16(0x58)));
    SET_AX(add16(AXV, F16(0x5a)));
    SET_DX(CXV); SET_BX(AXV);
    SET_DX(add16(DXV, F16(0x5c))); SET_BX(add16(BXV, F16(0x5e)));
BOX_END

BOX_FN(434429)
    SET_CX(F16(0xe)); SET_AX(F16(0x12));
    SET_CX(sub16(CXV, SCROLL));
    SET_CX(add16(CXV, F16(0x64))); SET_AX(add16(AXV, F16(0x66)));
    SET_DX(CXV); SET_BX(AXV);
    SET_DX(add16(DXV, F16(0x68))); SET_BX(add16(BXV, F16(0x6a)));
BOX_END

/* A box around a position kept in 17.15 fixed point at EDI + x, EDI + y,
 * with the half sizes hw, hh. */
#define FIXED_BOX(addr, x, y, hw, hh)                                       \
    BOX_FN(addr)                                                            \
        r.eax = rd32(EDI + (x)); r.ebx = rd32(EDI + (y));                   \
        r.eax = sar32(r.eax, 15); r.ebx = sar32(r.ebx, 15);                 \
        SET_CX(AXV); SET_DX(BXV);                                           \
        SET_AX(sub16(AXV, hw)); SET_BX(sub16(BXV, hh));                     \
        SET_CX(add16(CXV, hw)); SET_DX(add16(DXV, hh));                     \
    BOX_END
FIXED_BOX(43444f, 0xc, 0x10, 0xe, 0xc)
FIXED_BOX(434472, 0x14, 0x18, 0xc, 0xa)
FIXED_BOX(434495, 0x1c, 0x20, 0xc, 0xa)
FIXED_BOX(4344b8, 0x24, 0x28, 0xc, 0xa)
FIXED_BOX(4344db, 0x2c, 0x30, 0x8, 0x7)

BOX_FN(43457c)
    r.ecx = rd32(EDI + 4); r.eax = rd32(EDI + 8);
    r.ecx = sar32(r.ecx, 15); r.eax = sar32(r.eax, 15);
    SET_CX(sub16(CXV, SCROLL));
    SET_CX(add16(CXV, F16(0x36))); SET_AX(add16(AXV, F16(0x38)));
    SET_DX(CXV); SET_BX(AXV);
    SET_DX(add16(DXV, F16(0x3a))); SET_BX(add16(BXV, F16(0x3c)));
BOX_END

/*
 * 429b26, 42d48f (18-byte entries) and 429bc4 (14-byte entries): the
 * first free entry of a list of 0x32 gets the values in the registers and the
 * list's counter is incremented; a full list is left alone.
 */
static void add_list_entry(uint32_t counter, uint32_t base, uint32_t stride, bool four_values)
{
    cmp16(rd16(counter), 0x32);
    if (!flag(R_ZF)) {
        uint32_t ebp = base;
        do {
            ebp = add32(ebp, stride);
            cmp16(rd16(ebp), 0);
        } while (!flag(R_ZF));
        WR_REG(R_EBP, 4, ebp);
        wr16(ebp, 1);
        if (four_values) {
            wr32(ebp + 2, EAX);
            wr32(ebp + 6, EBX);
            const uint32_t ecx = add32(ECX, rd32(G_ARENA_BASE));
            WR_REG(R_ECX, 4, ecx);
            wr32(ebp + 0xa, ecx);
            wr32(ebp + 0xe, EDX);
        } else {
            wr32(ebp + 2, EBX);
            const uint32_t ecx = add32(ECX, rd32(G_ARENA_BASE));
            WR_REG(R_ECX, 4, ecx);
            wr32(ebp + 6, ecx);
            wr32(ebp + 0xa, EDX);
        }
        wr16(counter, inc16(rd16(counter)));
    }
    x86_ret();
}
HAND_FN(429b26) { add_list_entry(0x497c8e, 0x52c396, 0x12, true); }
HAND_FN(42d48f) { add_list_entry(0x4984c6, 0x52c9d6, 0x12, true); }
HAND_FN(429bc4) { add_list_entry(0x497c90, 0x52c71e, 0xe, false); }
