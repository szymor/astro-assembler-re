/* Runtime support for generated code (see recomp_rt.h). */
#include "recomp_rt.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

rt_cpu cpu;
uint8_t *mem;

_Noreturn void rt_fail(const char *msg, const char *file, int line)
{
    fprintf(stderr, "recomp: %s (%s:%d) EIP-ish: ret=%08x ESP=%08x\n", msg, file, line, cpu.ret_addr, ESP);
    abort();
}

_Noreturn void rt_bad_access(uint32_t addr, int size)
{
    fprintf(stderr, "recomp: %s of %d bytes outside emulated memory at %08x\n",
            size < 0 ? "write" : "read", size < 0 ? -size : size, addr);
    abort();
}

_Noreturn void rt_divide_error(void)
{
    fprintf(stderr, "recomp: divide error (#DE)\n");
    abort();
}

_Noreturn void rt_bad_return(uint32_t expected, uint32_t got)
{
    fprintf(stderr, "recomp: RET popped %08x, CALL expected %08x (return address manipulated?)\n", got, expected);
    abort();
}

rt_func rt_lookup(uint32_t addr)
{
    unsigned lo = 0, hi = rt_func_count;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (rt_func_table[mid].addr == addr) return rt_func_table[mid].fn;
        if (rt_func_table[mid].addr < addr) lo = mid + 1; else hi = mid;
    }
    return NULL;
}

/* Computed call. As with RT_CALL, a callee returning to an outer frame leaves
 * cpu.ret_addr != ret; the generated caller then unwinds (it checks
 * cpu.ret_addr right after this call). */
void rt_callind(uint32_t target, uint32_t ret)
{
    rt_func fn = rt_lookup(target);
    if (fn) {
        fn();
        return;
    }
    if (target >= 0x401000u && target < 0x401244u) {
        host_shell_call(target);
        WR_REG(R_ESP, 4, ESP + 4);        /* emulate the callee's RET */
        cpu.ret_addr = ret;
        return;
    }
    fprintf(stderr, "recomp: computed call to unknown address %08x\n", target);
    abort();
}

void rt_jumpind(uint32_t target)
{
    fprintf(stderr, "recomp: computed jump to unexpected address %08x\n", target);
    abort();
}

void rt_callother(unsigned kind, uint32_t at, ...)
{
    fprintf(stderr, "recomp: special instruction (CALLOTHER %#x) at %08x\n", kind, at);
    abort();
}

#ifdef RT_WATCH
#if defined(_MSC_VER)
#define RT_NOINLINE __declspec(noinline)
#else
#define RT_NOINLINE __attribute__((noinline))
#endif
uint32_t rt_watch_lo, rt_watch_hi;
int rt_watch_byte = -1;
static struct { void *caller; unsigned long count, first; } watch_tab[128];
static unsigned long watch_hits;
static void watch_report(void)
{
    fprintf(stderr, "watch summary (%lu hits); resolve with: addr2line -f -e <binary> <offset>\n", watch_hits);
    for (int i = 0; i < 128 && watch_tab[i].caller; i++)
        fprintf(stderr, "  caller offset %#lx: %lu writes, first at hit %lu\n",
                (unsigned long)((char *)watch_tab[i].caller - (char *)rt_watch) , watch_tab[i].count, watch_tab[i].first);
}
RT_NOINLINE void rt_watch(uint32_t addr, uint64_t value, int size)
{
    void *caller = __builtin_return_address(0);
    if (!watch_hits++) atexit(watch_report);
    for (int i = 0; i < 128; i++) {
        if (!watch_tab[i].caller) { watch_tab[i].caller = caller; watch_tab[i].first = watch_hits; }
        if (watch_tab[i].caller == caller) { watch_tab[i].count++; break; }
    }
}
#endif
