/*
 * The objects' paths: functions that the recompiler reports as recursive,
 * because the path step and the path pick fall into each other. (The level's
 * scrolling, also recursive through the level script, is in background.c.)
 */
#include "core.h"

/*
 * Obj_PickPath_q (0x427312) and Obj_NextPathPoint_q (0x4273e3): an object
 * (EBP) following a path of (x, y) word pairs at ESI. The next pair goes to
 * +0x14 / +0x12 (ESI moves on by 4); at the end marker (x = -1) a new path
 * is picked: a start offset from the random seed and the game time (+4, +6),
 * then one of eight paths (the table at 0x497938, never the last one taken,
 * 0x49790c) whose start (+8, +0xc, 17.15 fixed point) keeps a distance from
 * the player Core_PickNearerPlayer picks; its third word goes to +0x10 and the
 * object continues with its first point.
 */
static void pick_path(void);

HAND_FN(4273e3)
{
    const uint32_t esi = ESI, ebp = EBP;
    const uint16_t x = rd16(esi);
    WR_REG(R_EAX, 2, x);
    cmp16(x, 0xffff);
    if (flag(R_ZF)) { pick_path(); return; }                       /* JZ 0x427312 */
    wr16(ebp + 0x14, x);
    const uint16_t y = rd16(esi + 2);
    WR_REG(R_EAX, 2, y);
    wr16(ebp + 0x12, y);
    WR_REG(R_ESI, 4, add32(esi, 4));
    x86_ret();
}

HAND_FN(427312) { pick_path(); }

/* PUSH r16 / POP r16. */
static void push16(uint16_t v) { WR_REG(R_ESP, 4, ESP - 2); MEM_WR(ESP, 2, v); }
static uint16_t pop16(void) { const uint16_t v = (uint16_t)MEM_RD(ESP, 2); WR_REG(R_ESP, 4, ESP + 2); return v; }

static void pick_path(void)
{
    regs32 r = regs_load();
    for (;;) {                                                     /* 0x427312, and back from the end marker */
        uint16_t ax = add16(rd16(G_RANDOM_SEED), rd16(G_GAME_TIME));
        uint16_t bx = ax;
        bx = (uint16_t)(bx >> 3);                                  /* SHR BX, 3 (flags overwritten) */
        ax &= 0xf; bx &= 0xf;
        ax = (uint16_t)(ax << 8); bx = (uint16_t)(bx << 7);
        ax = add16(ax, 0x251c);
        bx = add16(bx, 0x7d0);
        set_lo16(&r.eax, ax); set_lo16(&r.ebx, bx);
        wr16(r.ebp + 4, ax);
        wr16(r.ebp + 6, bx);
        push16(lo16(r.edi));
        uint16_t di = rd16(G_RANDOM_SEED);
        for (;;) {                                                 /* 0x427350: the next path */
            di = inc16(di);
            uint16_t si = (uint16_t)(7 & di);
            test16(si);                                            /* AND SI, DI */
            cmp16(si, rd16(0x49790cu));
            set_lo16(&r.edi, di);
            set_lo16(&r.esi, si);
            if (flag(R_ZF)) continue;
            wr16(0x49790cu, si);
            si = (uint16_t)(si << 2);
            r.esi = rd32(si + 0x497938u);
            r.esi = add32(r.esi, rd32(G_ARENA_BASE));
            r.ebx = rd32(r.esi);
            r.eax = rd32(r.esi + 4);
            wr32(r.ebp + 8, r.ebx);
            wr32(r.ebp + 0xc, r.eax);
            r.ebx = sar32(r.ebx, 0xf);
            r.eax = sar32(r.eax, 0xf);
            regs_store(&r);
            HAND_CALL(f_4422d6, 0x427392u);
            r = regs_load();
            { const uint32_t t = r.eax; r.eax = r.ebx; r.ebx = t; }  /* XCHG EAX, EBX */
            const uint16_t cx = rd16(G_TARGET_X), dx = rd16(G_TARGET_Y);
            set_lo16(&r.ecx, cx); set_lo16(&r.edx, dx);
            test16(lo16(r.ebx));
            if (flag(R_SF)) { cmp16(dx, 0x54); if (cond_l()) continue; }
            cmp16(lo16(r.ebx), 0x190);
            if (cond_ge()) { cmp16(dx, 0x108); if (!flag(R_ZF) && cond_ge()) continue; }
            test16(lo16(r.eax));
            if (flag(R_SF)) { cmp16(cx, 0x58); if (cond_l()) continue; }
            cmp16(lo16(r.eax), 0x280);
            if (cond_ge()) { cmp16(cx, 0x1e8); if (!flag(R_ZF) && cond_ge()) continue; }
            break;
        }
        set_lo16(&r.edi, pop16());
        const uint16_t w = rd16(r.esi + 8);
        set_lo16(&r.eax, w);
        wr16(r.ebp + 0x10, w);
        r.esi = add32(r.esi, 0xa);
        /* falls into Obj_NextPathPoint_q */
        const uint16_t x = rd16(r.esi);
        set_lo16(&r.eax, x);
        cmp16(x, 0xffff);
        if (flag(R_ZF)) continue;
        wr16(r.ebp + 0x14, x);
        const uint16_t y = rd16(r.esi + 2);
        set_lo16(&r.eax, y);
        wr16(r.ebp + 0x12, y);
        r.esi = add32(r.esi, 4);
        regs_store(&r);
        x86_ret();
        return;
    }
}
