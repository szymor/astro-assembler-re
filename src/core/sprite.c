/*
 * Sprite drawing: RLE sprites into the 320x200 8-bit frame buffer.
 *
 * Sprite data (see docs/formats.md) is a sequence of 16-bit words:
 *
 *   skip, count, <count pixel bytes>   advance `skip` pixels, copy `count` bytes
 *   0xfffe                             next line
 *   0xffff                             end
 *
 * Skips and counts are below 0x8000, so the sign of a word tells a run from a
 * marker. There is no transparency inside a run.
 *
 * Both functions are written as a translation of the original register use,
 * because callers see the registers they leave behind (where the data ended,
 * where the last pixel went), and because the clipping path's 16-bit
 * arithmetic decides which pixels are drawn at the edges.
 */
#include "core.h"

#define G_FRAME_BUFFER   0x55c7f4u   /* address of the 320x200 frame */
#define G_CLIP_X         0x4b3ba4u   /* clipping path: sprite x, current line y, line start */
#define G_CLIP_Y         0x4b3ba6u
#define G_CLIP_LINE      0x4b3ba8u
#define SCREEN_W 320
#define SCREEN_H 200

/* REP MOVSB / MOVSD of ECX bytes from ESI to EDI: the original copies up to 3
 * bytes until EDI is 4-aligned, then dwords, then the rest. Only the
 * registers it leaves differ from a plain copy (EAX = bytes after the head,
 * ECX = 0). */
static void copy_run(regs32 *r)
{
    if (r->ecx >= 4) {
        r->eax = r->ecx;
        const uint32_t head = (4 - r->edi) & 3;
        r->eax -= head;
        const uint32_t n = head + (r->eax & ~3u);       /* head, then whole dwords */
        copy_forward(r->edi, r->esi, n);
        r->edi += n;
        r->esi += n;
        r->ecx = r->eax & 3;
    }
    copy_forward(r->edi, r->esi, r->ecx);
    r->edi += r->ecx;
    r->esi += r->ecx;
    r->ecx = 0;
}

/*
 * Core_DrawSprite (0x442454): sprite data at ESI, top left at (DX, BX), size
 * BP x CX. Nothing is drawn if the sprite lies entirely beyond the right or
 * bottom edge (as 16-bit unsigned sums: a large negative position counts as
 * beyond). A sprite partly outside goes to Core_DrawSpriteClipped; one fully
 * inside is copied run by run.
 */
HAND_FN(442454)
{
    regs32 r = regs_load();
    const uint16_t x = lo16(r.edx), y = lo16(r.ebx), w = lo16(r.ebp), h = lo16(r.ecx);

    r.eax = x;
    r.edi = SCREEN_W;
    set_lo16(&r.eax, (uint16_t)(x + w));
    set_lo16(&r.edi, (uint16_t)(SCREEN_W + w));
    cmp16(lo16(r.eax), lo16(r.edi));
    if (cond_ae()) goto done;                         /* beyond the right edge */
    set_lo16(&r.eax, (uint16_t)(y + h));
    set_lo16(&r.edi, (uint16_t)(SCREEN_H + h));
    cmp16(lo16(r.eax), lo16(r.edi));
    if (cond_ae()) goto done;                         /* below the bottom edge */

    set_lo16(&r.edi, (uint16_t)(SCREEN_W - w));
    cmp16(x, lo16(r.edi));
    if (!cond_a()) {
        set_lo16(&r.edi, (uint16_t)(SCREEN_H - h));
        cmp16(y, lo16(r.edi));
    }
    if (cond_a()) {                                   /* partly outside */
        regs_store(&r);
        f_4424f0();
        return;
    }

    /* fully inside */
    r.ebp = (uint32_t)MEM_RD(G_FRAME_BUFFER, 4);
    set_lo16(&r.edi, (uint16_t)(y * SCREEN_W + x));
    r.ebp += r.edi;                                   /* start of the current line */
    r.edi = r.ebp;
    for (;;) {
        set_lo16(&r.eax, rd16(r.esi));
        r.esi += 2;
        test16(lo16(r.eax));
        if (!flag(R_SF)) {                            /* skip, then a run */
            r.edi += r.eax;
            set_lo16(&r.eax, rd16(r.esi));
            r.esi += 2;
            r.ecx = r.eax;
            copy_run(&r);
            continue;
        }
        set_lo16(&r.eax, inc16(lo16(r.eax)));
        if (flag(R_ZF)) break;                        /* 0xffff: end */
        r.ebp += SCREEN_W;                            /* 0xfffe: next line */
        r.edi = r.ebp;
    }
done:
    regs_store(&r);
    x86_ret();
}

/*
 * Core_DrawSpriteClipped (0x4424f0): as Core_DrawSprite, for a sprite partly
 * outside the screen; DX, BX are signed. Lines above the screen are skipped,
 * drawing stops below it, and every run is cut at the left and right edges.
 * Keeps x, the current line's y and the line's start in memory (0x4b3ba4..).
 */
HAND_FN(4424f0)
{
    regs32 r = regs_load();
    wr16(G_CLIP_X, lo16(r.edx));
    wr16(G_CLIP_Y, lo16(r.ebx));
    r.edx = (uint32_t)(int32_t)(int16_t)lo16(r.edx);
    r.ebx = (uint32_t)(int32_t)(int16_t)lo16(r.ebx);
    set_lo16(&r.ebp, lo16(r.edx));                    /* BP: x of the next pixel */
    r.edi = r.ebx * SCREEN_W;
    r.ecx = 0;
    r.edi += r.edx;
    r.edx = 0;                                        /* DX: data to skip after a run */
    r.edi += (uint32_t)MEM_RD(G_FRAME_BUFFER, 4);
    MEM_WR(G_CLIP_LINE, 4, r.edi);

    for (;;) {
        set_lo16(&r.eax, rd16(r.esi));
        r.esi += 2;
        test16(lo16(r.eax));
        if (!flag(R_SF)) {                            /* AX = skip, then a run of CX */
            set_lo16(&r.ecx, rd16(r.esi));
            r.esi += 2;
            cmp16(rd16(G_CLIP_Y), 0);
            if (!cond_ge()) {                         /* line above the screen */
                r.esi += r.ecx;
                continue;
            }
            r.edi += r.eax;
            set_lo16(&r.eax, add16(lo16(r.eax), lo16(r.ebp)));    /* x of the run */
            if (!cond_ge()) {                         /* starts left of the screen */
                r.esi += r.ecx;
                r.edi += r.ecx;
                set_lo16(&r.eax, add16(lo16(r.eax), lo16(r.ecx)));    /* x after the run */
                set_lo16(&r.ebp, lo16(r.eax));
                if (cond_le()) continue;              /* entirely left of it */
                r.esi -= r.eax;                       /* draw the visible end */
                r.edi -= r.eax;
                set_lo16(&r.ecx, lo16(r.eax));
            } else {
                set_lo16(&r.eax, add16(lo16(r.eax), lo16(r.ecx)));    /* x after the run */
                set_lo16(&r.ebp, lo16(r.eax));
                cmp16(lo16(r.eax), SCREEN_W);
                if (!cond_l()) {                      /* reaches past the right edge */
                    r.esi += r.ecx;
                    set_lo16(&r.eax, sub16(lo16(r.eax), lo16(r.ecx)));
                    set_lo16(&r.eax, sub16(lo16(r.eax), SCREEN_W));
                    if (cond_ge()) continue;          /* entirely right of it */
                    r.esi -= r.ecx;                   /* draw the visible start, skip the rest */
                    set_lo16(&r.eax, add16(lo16(r.eax), lo16(r.ecx)));
                    set_lo16(&r.ecx, sub16(lo16(r.ecx), lo16(r.eax)));
                    set_lo16(&r.edx, lo16(r.eax));
                }
            }
            copy_run(&r);
            r.esi += r.edx;
            r.edx = 0;
            continue;
        }
        set_lo16(&r.eax, inc16(lo16(r.eax)));
        if (flag(R_ZF)) break;                        /* 0xffff: end */
        wr16(G_CLIP_Y, (uint16_t)(rd16(G_CLIP_Y) + 1));   /* 0xfffe: next line */
        cmp16(rd16(G_CLIP_Y), SCREEN_H);
        if (cond_ge()) break;                         /* below the screen */
        set_lo16(&r.ebp, rd16(G_CLIP_X));
        MEM_WR(G_CLIP_LINE, 4, (uint32_t)MEM_RD(G_CLIP_LINE, 4) + SCREEN_W);
        r.edi = (uint32_t)MEM_RD(G_CLIP_LINE, 4);
    }
    regs_store(&r);
    x86_ret();
}

#define G_SILHOUETTE_FILL 0x4b3bb0u  /* dword: the fill pattern of the silhouette */

/* REP STOSB / STOSD of ECX bytes at EDI: AL until EDI is 4-aligned, then EAX
 * as dwords, then AL again. EBX is the original's scratch register. */
static void fill_run(regs32 *r)
{
    if (r->ecx >= 4) {
        r->ebx = r->ecx;
        const uint32_t head = (4 - r->edi) & 3;
        r->ebx -= head;
        fill_bytes(r->edi, (uint8_t)r->eax, head);
        r->edi += head;
        fill_dwords(r->edi, r->eax, r->ebx >> 2);
        r->edi += r->ebx & ~3u;
        r->ecx = r->ebx & 3;
    }
    fill_bytes(r->edi, (uint8_t)r->eax, r->ecx);
    r->edi += r->ecx;
    r->ecx = 0;
}

/*
 * Core_DrawSilhouette (0x4425dc): the shape of the sprite at ESI, at (DX, BX),
 * size BP x CX, filled with the pattern at 0x4b3bb0 instead of its pixels
 * (the pixel data is skipped). The same visibility tests and the same
 * clipping as Core_DrawSprite and Core_DrawSpriteClipped, written out in one
 * function by the original; the skip is kept in DX, EBX is scratch for the
 * fill, and the upper halves of EAX and EDI are left as they came until the
 * drawing starts.
 */
HAND_FN(4425dc)
{
    regs32 r = regs_load();
    const uint16_t x = lo16(r.edx), y = lo16(r.ebx), w = lo16(r.ebp), h = lo16(r.ecx);

    set_lo16(&r.eax, (uint16_t)(x + w));
    set_lo16(&r.edi, (uint16_t)(SCREEN_W + w));
    cmp16(lo16(r.eax), lo16(r.edi));
    if (cond_ae()) goto done;                         /* beyond the right edge */
    set_lo16(&r.eax, (uint16_t)(y + h));
    set_lo16(&r.edi, (uint16_t)(SCREEN_H + h));
    cmp16(lo16(r.eax), lo16(r.edi));
    if (cond_ae()) goto done;                         /* below the bottom edge */

    set_lo16(&r.edi, (uint16_t)(SCREEN_W - w));
    cmp16(x, lo16(r.edi));
    if (!cond_a()) {
        set_lo16(&r.edi, (uint16_t)(SCREEN_H - h));
        cmp16(y, lo16(r.edi));
    }
    if (cond_a()) goto clipped;                       /* partly outside */

    /* fully inside */
    r.ecx = (uint16_t)(y * SCREEN_W + x);
    r.edi = (uint32_t)MEM_RD(G_FRAME_BUFFER, 4);
    r.eax = (uint32_t)MEM_RD(G_SILHOUETTE_FILL, 4);
    r.edx = 0;
    r.edi += r.ecx;
    r.ebp = r.edi;                                    /* start of the current line */
    for (;;) {
        set_lo16(&r.edx, rd16(r.esi));
        r.esi += 2;
        test16(lo16(r.edx));
        if (!flag(R_SF)) {                            /* DX = skip, then a run */
            set_lo16(&r.ecx, rd16(r.esi));
            r.edi += r.edx;
            r.esi += 2;
            r.esi += r.ecx;                           /* the pixels are not used */
            fill_run(&r);
            continue;
        }
        set_lo16(&r.edx, inc16(lo16(r.edx)));
        if (flag(R_ZF)) goto done;                    /* 0xffff: end */
        r.ebp += SCREEN_W;                            /* 0xfffe: next line */
        r.edi = r.ebp;
    }

clipped:
    wr16(G_CLIP_X, x);
    wr16(G_CLIP_Y, y);
    r.edx = (uint32_t)(int32_t)(int16_t)x;
    r.ebx = (uint32_t)(int32_t)(int16_t)y;
    set_lo16(&r.ebp, x);                              /* BP: x of the next pixel */
    r.edi = r.ebx * SCREEN_W;
    r.ecx = 0;
    r.edi += r.edx;
    r.edx = 0;
    r.edi += (uint32_t)MEM_RD(G_FRAME_BUFFER, 4);
    MEM_WR(G_CLIP_LINE, 4, r.edi);
    r.eax = (uint32_t)MEM_RD(G_SILHOUETTE_FILL, 4);
    for (;;) {
        set_lo16(&r.edx, rd16(r.esi));
        r.esi += 2;
        test16(lo16(r.edx));
        if (!flag(R_SF)) {                            /* DX = skip, then a run of CX */
            set_lo16(&r.ecx, rd16(r.esi));
            r.esi += 2;
            r.esi += r.ecx;                           /* the pixels are not used */
            cmp16(rd16(G_CLIP_Y), 0);
            if (cond_l()) continue;                   /* line above the screen */
            r.edi += r.edx;
            set_lo16(&r.edx, add16(lo16(r.edx), lo16(r.ebp)));    /* x of the run */
            if (!cond_ge()) {                         /* starts left of the screen */
                r.edi += r.ecx;
                set_lo16(&r.edx, add16(lo16(r.edx), lo16(r.ecx)));    /* x after the run */
                set_lo16(&r.ebp, lo16(r.edx));
                if (cond_le()) continue;              /* entirely left of it */
                r.edi -= r.edx;                       /* fill the visible end */
                set_lo16(&r.ecx, lo16(r.edx));
            } else {
                set_lo16(&r.edx, add16(lo16(r.edx), lo16(r.ecx)));    /* x after the run */
                set_lo16(&r.ebp, lo16(r.edx));
                cmp16(lo16(r.edx), SCREEN_W);
                if (!cond_l()) {                      /* reaches past the right edge */
                    set_lo16(&r.edx, sub16(lo16(r.edx), lo16(r.ecx)));
                    set_lo16(&r.edx, sub16(lo16(r.edx), SCREEN_W));
                    if (cond_ge()) continue;          /* entirely right of it */
                    set_lo16(&r.edx, add16(lo16(r.edx), lo16(r.ecx)));
                    set_lo16(&r.ecx, sub16(lo16(r.ecx), lo16(r.edx)));    /* fill the visible start */
                }
            }
            fill_run(&r);
            continue;
        }
        set_lo16(&r.edx, inc16(lo16(r.edx)));
        if (flag(R_ZF)) break;                        /* 0xffff: end */
        wr16(G_CLIP_Y, (uint16_t)(rd16(G_CLIP_Y) + 1));   /* 0xfffe: next line */
        cmp16(rd16(G_CLIP_Y), SCREEN_H);
        if (cond_ge()) break;                         /* below the screen */
        set_lo16(&r.ebp, rd16(G_CLIP_X));
        MEM_WR(G_CLIP_LINE, 4, (uint32_t)MEM_RD(G_CLIP_LINE, 4) + SCREEN_W);
        r.edi = (uint32_t)MEM_RD(G_CLIP_LINE, 4);
    }
done:
    regs_store(&r);
    x86_ret();
}

/*
 * Pixels through a colour table: for each source pixel (AL) and the screen pixel
 * it lands on (AH) the new pixel is table[AH << 8 | AL]. ECX pixels from ESI to
 * EDI (ECX is not zero on entry). EAX ends as the last (dest, new) pair. The
 * flags are those of the last DEC ECX.
 */
static void remap_run(regs32 *r, uint32_t table)
{
    do {
        const uint32_t pair = (uint32_t)rd8(r->edi) << 8 | rd8(r->esi);
        const uint8_t out = rd8(((r->eax & 0xffff0000u) | pair) + table);
        r->eax = (r->eax & 0xffff0000u) | (pair & 0xff00u) | out;
        wr8(r->edi, out);
        r->esi++;
        r->edi++;
    } while (--r->ecx != 0);
    flags_szp32(0);                                    /* DEC ECX reaching zero: CF is left alone */
    set_flag(R_OF, false);
}

/*
 * Core_DrawSpriteRemap (0x442758): Core_DrawSprite, but every pixel goes through
 * a colour table (a blend with what is already on the screen). EDI selects the
 * table: its address is the dword at 0x46ffd4 + EDI, which the original
 * patches into two instructions of its own code (0x4427ce and 0x442882), so
 * those are stored here too. Same visibility tests and clipping as the other
 * sprite blitters (written out in one function, no call to the clipped one).
 */
HAND_FN(442758)
{
    regs32 r = regs_load();
    const uint32_t table = rd32(r.edi + 0x46ffd4u);
    wr32(0x4427ceu, table);
    wr32(0x442882u, table);

    r.eax = (uint16_t)r.edx;
    r.edi = SCREEN_W;
    set_lo16(&r.eax, add16(lo16(r.eax), lo16(r.ebp)));
    set_lo16(&r.edi, add16(lo16(r.edi), lo16(r.ebp)));
    cmp16(lo16(r.eax), lo16(r.edi));
    if (cond_ae()) goto done;                         /* beyond the right edge */
    set_lo16(&r.eax, lo16(r.ebx));
    set_lo16(&r.edi, SCREEN_H);
    set_lo16(&r.eax, add16(lo16(r.eax), lo16(r.ecx)));
    set_lo16(&r.edi, add16(lo16(r.edi), lo16(r.ecx)));
    cmp16(lo16(r.eax), lo16(r.edi));
    if (cond_ae()) goto done;                         /* below the bottom edge */
    set_lo16(&r.edi, SCREEN_W);
    set_lo16(&r.edi, sub16(lo16(r.edi), lo16(r.ebp)));
    cmp16(lo16(r.edx), lo16(r.edi));
    if (cond_a()) goto clipped;                       /* partly outside */
    set_lo16(&r.edi, SCREEN_H);
    set_lo16(&r.edi, sub16(lo16(r.edi), lo16(r.ecx)));
    cmp16(lo16(r.ebx), lo16(r.edi));
    if (cond_a()) goto clipped;

    /* fully inside */
    r.ebp = (uint32_t)MEM_RD(G_FRAME_BUFFER, 4);
    set_lo16(&r.edi, (uint16_t)(lo16(r.ebx) * SCREEN_W));
    set_lo16(&r.edi, add16(lo16(r.edi), lo16(r.edx)));
    r.ebp += r.edi;                                   /* start of the current line */
    r.edi = r.ebp;
    for (;;) {
        set_lo16(&r.eax, rd16(r.esi));
        r.esi += 2;
        test16(lo16(r.eax));
        if (!flag(R_SF)) {                            /* skip, then a run */
            r.edi += r.eax;
            set_lo16(&r.eax, rd16(r.esi));
            r.esi += 2;
            test32(r.eax, 0xffffffffu);
            if (flag(R_ZF)) continue;
            r.ecx = r.eax;
            remap_run(&r, table);
            continue;
        }
        set_lo16(&r.eax, inc16(lo16(r.eax)));
        if (flag(R_ZF)) goto done;                    /* 0xffff: end */
        r.ebp = add32(r.ebp, SCREEN_W);               /* 0xfffe: next line */
        r.edi = r.ebp;
    }

clipped:
    wr16(G_CLIP_X, lo16(r.edx));
    wr16(G_CLIP_Y, lo16(r.ebx));
    r.edx = (uint32_t)(int32_t)(int16_t)lo16(r.edx);
    r.ebx = (uint32_t)(int32_t)(int16_t)lo16(r.ebx);
    set_lo16(&r.ebp, lo16(r.edx));                    /* BP: x of the next pixel */
    r.edi = r.ebx * SCREEN_W;
    r.ecx = 0;
    r.edi += r.edx;
    r.edx = 0;                                        /* DX: data to skip after a run */
    r.edi += (uint32_t)MEM_RD(G_FRAME_BUFFER, 4);
    MEM_WR(G_CLIP_LINE, 4, r.edi);
    for (;;) {
        set_lo16(&r.eax, rd16(r.esi));
        r.esi += 2;
        test16(lo16(r.eax));
        if (!flag(R_SF)) {                            /* AX = skip, then a run of CX */
            set_lo16(&r.ecx, rd16(r.esi));
            r.esi += 2;
            cmp16(rd16(G_CLIP_Y), 0);
            if (!cond_ge()) {                         /* line above the screen */
                r.esi += r.ecx;
                continue;
            }
            r.edi += r.eax;
            set_lo16(&r.eax, add16(lo16(r.eax), lo16(r.ebp)));    /* x of the run */
            if (!cond_ge()) {                         /* starts left of the screen */
                r.esi += r.ecx;
                r.edi += r.ecx;
                set_lo16(&r.eax, add16(lo16(r.eax), lo16(r.ecx)));    /* x after the run */
                set_lo16(&r.ebp, lo16(r.eax));
                if (cond_le()) continue;              /* entirely left of it */
                r.esi -= r.eax;                       /* draw the visible end */
                r.edi -= r.eax;
                set_lo16(&r.ecx, lo16(r.eax));
            } else {
                set_lo16(&r.eax, add16(lo16(r.eax), lo16(r.ecx)));    /* x after the run */
                set_lo16(&r.ebp, lo16(r.eax));
                cmp16(lo16(r.eax), SCREEN_W);
                if (!cond_l()) {                      /* reaches past the right edge */
                    r.esi += r.ecx;
                    set_lo16(&r.eax, sub16(lo16(r.eax), lo16(r.ecx)));
                    set_lo16(&r.eax, sub16(lo16(r.eax), SCREEN_W));
                    if (cond_ge()) continue;          /* entirely right of it */
                    r.esi -= r.ecx;                   /* draw the visible start, skip the rest */
                    set_lo16(&r.eax, add16(lo16(r.eax), lo16(r.ecx)));
                    set_lo16(&r.ecx, sub16(lo16(r.ecx), lo16(r.eax)));
                    set_lo16(&r.edx, lo16(r.eax));
                }
            }
            test32(r.ecx, 0xffffffffu);
            if (!flag(R_ZF)) remap_run(&r, table);
            r.esi += r.edx;
            r.edx = 0;
            flags_szp32(0);                           /* XOR EDX, EDX */
            set_flag(R_CF, false);
            set_flag(R_OF, false);
            continue;
        }
        set_lo16(&r.eax, inc16(lo16(r.eax)));
        if (flag(R_ZF)) break;                        /* 0xffff: end */
        wr16(G_CLIP_Y, inc16(rd16(G_CLIP_Y)));        /* 0xfffe: next line */
        cmp16(rd16(G_CLIP_Y), SCREEN_H);
        if (cond_ge()) break;                         /* below the screen */
        set_lo16(&r.ebp, rd16(G_CLIP_X));
        MEM_WR(G_CLIP_LINE, 4, add32((uint32_t)MEM_RD(G_CLIP_LINE, 4), SCREEN_W));
        r.edi = (uint32_t)MEM_RD(G_CLIP_LINE, 4);
    }
done:
    regs_store(&r);
    x86_ret();
}
