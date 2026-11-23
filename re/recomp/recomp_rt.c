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

void rt_callind(uint32_t target, uint32_t ret)
{
    rt_func fn = rt_lookup(target);
    if (fn) {
        fn();
        if (cpu.ret_addr != ret) rt_bad_return(ret, cpu.ret_addr);
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
