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
#ifdef RT_DIFF
        if (rt_diff_active) {
            fprintf(stderr, "recomp: shell callback %08x during a differential check\n", target);
            abort();
        }
#endif
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

#ifdef RT_DIFF
/* ---- differential checking of hand-written functions --------------------- */
int rt_diff_active;
static uint8_t *diff_seen;                  /* bitmap of bytes written so far */
static struct diff_byte { uint32_t addr; uint8_t old, ref; } *diff_log;
static size_t diff_len, diff_cap;

void rt_diff_note(uint32_t a, int size)
{
    for (int i = 0; i < size; i++, a++) {
        if (diff_seen[a >> 3] & (1u << (a & 7))) continue;
        diff_seen[a >> 3] |= (uint8_t)(1u << (a & 7));
        if (diff_len == diff_cap) {
            diff_cap = diff_cap ? diff_cap * 2 : 4096;
            diff_log = realloc(diff_log, diff_cap * sizeof *diff_log);
            if (!diff_log) { fprintf(stderr, "recomp: out of memory\n"); abort(); }
        }
        diff_log[diff_len++] = (struct diff_byte){a, mem[a], 0};
    }
}

static void diff_forget(size_t from)
{
    for (size_t i = from; i < diff_len; i++) diff_seen[diff_log[i].addr >> 3] = 0;
    diff_len = from;
}

static struct diff_stat { uint32_t addr; const char *name; unsigned long calls; } diff_stats[256];

static void diff_report(void)
{
    for (int i = 0; i < 256 && diff_stats[i].name; i++)
        fprintf(stderr, "diff: %s (%06x): %lu calls checked, identical\n", diff_stats[i].name,
                diff_stats[i].addr, diff_stats[i].calls);
}

static const char *reg_name(unsigned off)
{
    static const struct { unsigned off; const char *name; } k[] = {
        {0x00, "EAX"}, {0x04, "ECX"}, {0x08, "EDX"}, {0x0c, "EBX"}, {0x10, "ESP"}, {0x14, "EBP"},
        {0x18, "ESI"}, {0x1c, "EDI"}, {0x200, "CF"}, {0x202, "PF"}, {0x204, "AF"}, {0x206, "ZF"},
        {0x207, "SF"}, {0x20a, "DF"}, {0x20b, "OF"}, {0x284, "EIP"}};
    for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++)
        if (off >= k[i].off && off < k[i].off + (k[i].off < 0x200 ? 4u : k[i].off == 0x284 ? 4u : 1u)) return k[i].name;
    return "?";
}

void rt_diff_check(rt_func ref, rt_func hand, uint32_t addr, const char *name)
{
    if (rt_diff_active) { hand(); return; }  /* nested: checked on its own */
    if (!diff_seen) {
        diff_seen = calloc(RT_MEM_SIZE / 8, 1);
        if (!diff_seen) { fprintf(stderr, "recomp: out of memory\n"); abort(); }
        atexit(diff_report);
    }
    struct diff_stat *st = NULL;
    for (int i = 0; i < 256; i++)
        if (!diff_stats[i].name || diff_stats[i].addr == addr) { st = &diff_stats[i]; break; }
    if (st && !st->name) { st->addr = addr; st->name = name; }

    static rt_cpu before, after_ref;
    before = cpu;
    rt_diff_active = 1;
    ref();
    after_ref = cpu;
    size_t nref = diff_len;
    for (size_t i = 0; i < nref; i++) {      /* remember the results, then undo */
        diff_log[i].ref = mem[diff_log[i].addr];
    }
    for (size_t i = nref; i-- > 0;) mem[diff_log[i].addr] = diff_log[i].old;
    cpu = before;
    hand();
    rt_diff_active = 0;

    unsigned long call = st ? ++st->calls : 0;
    int bad = 0;
    for (unsigned off = 0; off < sizeof cpu.r; off++) {
        if (cpu.r[off] == after_ref.r[off]) continue;
        if (bad++ < 16)
            fprintf(stderr, "diff: %s call %lu: register byte %#x (%s): generated %02x, hand-written %02x\n",
                    name, call, off, reg_name(off), after_ref.r[off], cpu.r[off]);
    }
    if (cpu.ret_addr != after_ref.ret_addr) {
        bad++;
        fprintf(stderr, "diff: %s call %lu: return address generated %08x, hand-written %08x\n", name, call,
                after_ref.ret_addr, cpu.ret_addr);
    }
    const uint32_t esp = (uint32_t)RD_REG(R_ESP, 4);
    for (size_t i = 0; i < diff_len; i++) {
        const uint32_t a = diff_log[i].addr;
        if (a < esp && a >= esp - 0x10000u) continue;        /* dead stack */
        const uint8_t want = i < nref ? diff_log[i].ref : diff_log[i].old;
        if (mem[a] == want) continue;
        if (bad++ < 32)
            fprintf(stderr, "diff: %s call %lu: memory %08x: generated %02x, hand-written %02x\n", name, call, a,
                    want, mem[a]);
    }
    /* Entries before nref were written by the generated version (and maybe
     * by the hand-written one): the result must be the generated value. Later
     * entries were written by the hand-written version only: they must still
     * hold their old value. */
    diff_forget(0);
    if (bad) {
        fprintf(stderr, "diff: %s (%06x) differs from the generated version (call %lu)\n", name, addr, call);
        abort();
    }
}
#endif
