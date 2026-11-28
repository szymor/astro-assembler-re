/*
 * The list of hand-written core functions (replaced.h): in differential builds
 * the wrappers that check each against its generated version, and in every
 * build the check that the generated core matches the list.
 */
#include "core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef RT_DIFF
#define HAND(addr, name)                                                    \
    void hand_##addr(void);                                                 \
    void f_##addr(void) { rt_diff_check(ref_##addr, hand_##addr, 0x##addr, #name); }
#define HAND_UNCHECKED(addr, name)                                          \
    void hand_##addr(void);                                                 \
    void f_##addr(void) { rt_diff_pass(ref_##addr, hand_##addr, 0x##addr, #name); }
#define HAND_STATE(addr, name)                                              \
    void hand_##addr(void);                                                 \
    void f_##addr(void) { rt_diff_check_state(ref_##addr, hand_##addr, 0x##addr, #name); }
#include "replaced.h"
#undef HAND
#undef HAND_UNCHECKED
#undef HAND_STATE
#endif

static int cmp_u32(const void *a, const void *b)
{
    const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

bool core_check_replaced(void)
{
    uint32_t list[] = {
#define HAND(addr, name) 0x##addr,
#define HAND_UNCHECKED(addr, name) 0x##addr,
#define HAND_STATE(addr, name) 0x##addr,
#include "replaced.h"
#undef HAND
#undef HAND_UNCHECKED
#undef HAND_STATE
        0};
    size_t n = sizeof list / sizeof list[0] - 1;
    qsort(list, n, sizeof list[0], cmp_u32);
    char want[4096] = "", item[16];
    for (size_t i = 0; i < n; i++) {
        snprintf(item, sizeof item, i ? " %x" : "%x", (unsigned)list[i]);
        strncat(want, item, sizeof want - strlen(want) - 1);
    }
    if (strcmp(want, GEN_REPLACED) == 0) return true;
    fprintf(stderr, "The generated core does not match src/core/replaced.h (generated: \"%s\", list: \"%s\"):\n"
                    "regenerate it (re/recomp/recomp.py --replaced src/core/replaced.h, see docs/recompiler.md).\n",
            GEN_REPLACED, want);
    return false;
}
