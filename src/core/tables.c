/*
 * Small updates of counters and animation tables, written as translations of
 * the original register use (see core.h); the flags the callers may test are
 * those of the last flag-setting instruction, which the helpers reproduce.
 */
#include "core.h"

/*
 * Core_ClearFinishedA (0x41a93d): the word at 0x4b0c60 counts the live entries
 * (12 bytes, from 0x51e700; a zero first word = unused). Each live entry loses
 * 10 from its word (at 0 or below it becomes 0 and the count drops, which
 * stops the scan at 0), and, in even frames (bit 0 of the level time at
 * 0x46ffa0 clear), 4 from the dword at +4 if that is not 0. CX = the entries
 * left to visit.
 */
HAND_FN(41a93d)
{
    uint16_t cx = rd16(0x4b0c60u);
    test16(cx);
    if (flag(R_ZF)) { WR_REG(R_ECX, 2, cx); x86_ret(); return; }
    uint32_t edi = 0x51e6f4u;
    for (;;) {
        do { edi = add32(edi, 0xc); cmp16(rd16(edi), 0); } while (flag(R_ZF));
        test32(rd32(0x46ffa0u), 1);
        if (flag(R_ZF)) {
            cmp32(rd32(edi + 4), 0);
            if (!flag(R_ZF)) wr32(edi + 4, sub32(rd32(edi + 4), 4));
        }
        const uint16_t w = sub16(rd16(edi), 0xa);
        wr16(edi, w);
        bool counted = flag(R_ZF);
        if (!counted && !flag(R_SF)) goto next;       /* JNS: still positive */
        if (!counted) wr16(edi, 0);
        wr16(0x4b0c60u, dec16(rd16(0x4b0c60u)));
        if (flag(R_ZF)) break;
    next:
        cx = dec16(cx);
        if (flag(R_ZF)) break;
    }
    WR_REG(R_ECX, 2, cx);
    WR_REG(R_EDI, 4, edi);
    x86_ret();
}

/*
 * Core_ClearFinishedB (0x41b026): the word at 0x4b0dc8 counts the live entries
 * (14 bytes, from 0x51f354; a zero first word = unused). A live entry's
 * countdown word goes down by 1; at 0 the entry is freed (word cleared, count
 * down). Above 10 it also moves: the word at +4 drops by 2 (freed when below
 * 0xff74 as a signed word) and, in even frames, the dword at +6 grows by 4,
 * wrapping at 0x38. ECX = the entries left to visit.
 */
HAND_FN(41b026)
{
    uint32_t ecx = rd16(0x4b0dc8u);
    test16((uint16_t)ecx);
    if (flag(R_ZF)) { WR_REG(R_ECX, 4, ecx); x86_ret(); return; }
    uint32_t edi = 0x51f346u;
    for (;;) {
        do { edi = add32(edi, 0xe); cmp16(rd16(edi), 0); } while (flag(R_ZF));
        bool free_it;
        wr16(edi, dec16(rd16(edi)));
        free_it = flag(R_ZF);
        if (!free_it) {
            cmp16(rd16(edi), 0xa);
            if (!cond_le()) {
                wr16(edi + 4, sub16(rd16(edi + 4), 2));
                cmp16(rd16(edi + 4), 0xff74);
                if (cond_l()) {
                    free_it = true;
                } else {
                    test32(rd32(0x46ffa0u), 1);
                    if (flag(R_ZF)) {
                        wr32(edi + 6, add32(rd32(edi + 6), 4));
                        cmp32(rd32(edi + 6), 0x38);
                        if (!cond_l()) wr32(edi + 6, sub32(rd32(edi + 6), 0x38));
                    }
                }
            }
        }
        if (free_it) {
            wr16(edi, 0);
            wr16(0x4b0dc8u, dec16(rd16(0x4b0dc8u)));
        }
        ecx = dec32(ecx);
        if (flag(R_ZF)) break;
    }
    WR_REG(R_ECX, 4, ecx);
    WR_REG(R_EDI, 4, edi);
    x86_ret();
}

/*
 * Core_FindEntryOrFree (0x415e97): the word at 0x4ba740 counts the used
 * entries of a table of 10 (48 bytes each, from 0x519a60; a zero first word =
 * free). With the table full, CF = 1. Otherwise the used entries are searched
 * for one whose dword at +0xe equals EDI: found, CF = 1 and ESI is that entry.
 * Not found: ESI = the first free entry and CF = 0. CX = the entries left.
 */
HAND_FN(415e97)
{
    uint16_t cx = rd16(0x4ba740u);
    uint32_t esi = 0x519a30u;
    cmp16(cx, 0xa);
    if (flag(R_ZF)) goto found;
    test16(cx);
    if (!flag(R_ZF)) {
        for (;;) {
            do { esi = add32(esi, 0x30); cmp16(rd16(esi), 0); } while (flag(R_ZF));
            cmp32(rd32(esi + 0xe), EDI);
            if (flag(R_ZF)) goto found;
            cx = dec16(cx);
            if (flag(R_ZF)) break;
        }
        esi = 0x519a30u;
    }
    do { esi = add32(esi, 0x30); cmp16(rd16(esi), 0); } while (!flag(R_ZF));
    set_flag(R_CF, false);
    goto done;
found:
    set_flag(R_CF, true);
done:
    WR_REG(R_ECX, 2, cx);
    WR_REG(R_ESI, 4, esi);
    x86_ret();
}

/*
 * Core_NextFrames (0x413849): 16 animation records of 8 bytes at the pointer in
 * 0x4bcb10 (dword = frame data, word = frame counter and count, word = frame
 * size): each steps its frame (wrapping to 0 at the count) and copies the
 * frame's pixels (size bytes) to the buffer from the pointer at 0x4be944 + 0x2d0.
 */
HAND_FN(413849)
{
    uint32_t edi = rd32(0x4be944u) + 0x2d0u, ebp = rd32(0x4bcb10u);
    uint32_t esi = 0, eax = EAX, ecx = ECX;
    uint16_t bx = 0x10;
    do {
        esi = rd32(ebp);
        const uint16_t w = rd16(ebp + 4);
        uint8_t al = (uint8_t)w, ah = (uint8_t)(w >> 8);
        ah++;
        if (al == ah) ah = 0;
        wr16(ebp + 4, (uint16_t)(al | ah << 8));
        ecx = rd16(ebp + 6);
        eax = (uint32_t)(int32_t)(int16_t)(uint16_t)(ah * (ecx & 0xff));
        esi = add32(esi, eax);
        copy_forward(edi, esi, ecx);
        edi += ecx;
        esi += ecx;
        ecx = 0;
        ebp = add32(ebp, 8);
        bx = dec16(bx);
    } while (!flag(R_ZF));
    WR_REG(R_EAX, 4, eax); WR_REG(R_ECX, 4, ecx); WR_REG(R_EBX, 2, bx);
    WR_REG(R_ESI, 4, esi); WR_REG(R_EDI, 4, edi); WR_REG(R_EBP, 4, ebp);
    x86_ret();
}
