#!/usr/bin/env python3
"""Survey the assembly core (0x408c44-0x447da7) for constructs that matter to a
static asm-to-C translation: indirect jumps/calls, writes into code, FPU,
flag tricks, unusual instructions. Input: objdump -d -M intel output.

Usage: python3 re/scripts/core_hazards.py text.asm
"""
import re,sys,collections
lines=[]
for l in open(sys.argv[1]):
    m=re.match(r'\s+([0-9a-f]+):\t(\S+)\s*(.*)',l.rstrip('\n'))
    if m:
        a=int(m.group(1),16)
        if 0x408c44<=a<0x447da7: lines.append((a,m.group(2),m.group(3)))
print('instructions in core:',len(lines))
mn=collections.Counter(op for a,op,arg in lines)
cat=collections.defaultdict(list)
CODE=(0x408c44,0x447da7)
for a,op,arg in lines:
    if op in('jmp','call') and not re.match(r'0x[0-9a-f]+$',arg): cat['indirect '+op].append((a,arg))
    if op.startswith('f') and op not in ('fs',) and re.match(r'f[a-z]+$',op) and op not in('fwait',): cat['x87'].append((a,op))
    if op in('rdtsc','in','out','int','int3','hlt','cpuid','sti','cli','into','bound','lds','les','enter','leave'): cat['special '+op].append((a,arg))
    if re.search(r'\besp\b',arg) and op not in('push','pop'): cat['esp use'].append((a,op,arg))
    if op=='ret' and arg: cat['ret imm'].append((a,arg))
    for m in re.finditer(r'ds:0x([0-9a-f]+)',arg):
        t=int(m.group(1),16)
        if CODE[0]<=t<CODE[1] and re.match(r'(mov|add|sub|xor|or|and|inc|dec|not|neg|stos|rol|ror|shl|shr)',op) and arg.split(',')[0].find('ds:0x')>=0:
            cat['write into code'].append((a,op,arg))
    if op in('pushf','pushfw','pushfd','popf','popfw','popfd','lahf','sahf'): cat['flags save'].append((a,op))
    if op in ('movs','stos','lods','scas','cmps') or op.startswith('rep'): cat['string ops'].append(a)
    if op in('loop','loope','loopne','jcxz','jecxz'): cat['loop/jcxz'].append(a)
    if op in('adc','sbb','rcl','rcr'): cat['carry-dependent'].append(a)
    if op in('bswap','xchg','cmpxchg','xadd'): cat['xchg etc'].append(a)
for k in sorted(cat): print(f'{k:20s} {len(cat[k]):6d}  e.g. {cat[k][:4]}')
print('distinct mnemonics:',len(mn)); print(' '.join(f'{k}:{v}' for k,v in mn.most_common(60)))
