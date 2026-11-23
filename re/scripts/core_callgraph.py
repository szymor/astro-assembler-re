#!/usr/bin/env python3
"""First-level call graph of the assembly core: functions and shell callbacks
reachable (direct calls only) from each core command.

Usage:
  objdump -d -M intel --no-show-raw-insn --start-address=0x401000 \
      --stop-address=0x46b000 extracted/game/ASTRO.exe > text.asm
  python3 re/scripts/find_seeds.py extracted/game/ASTRO.exe seeds.txt
  python3 re/scripts/core_callgraph.py text.asm seeds.txt
"""
import re,sys,collections,bisect
asm=open(sys.argv[1]).read().splitlines()
seeds=sorted(int(l.split()[1],16) for l in open(sys.argv[2]) if l.startswith('F'))
CMDS=[0x408cc2,0x408d06,0x408d58,0x408daa,0x408dfc,0x408e4e,0x408ea0,0x408ceb,0x4443ae,0x40b8d5,0x40cd0f,0x40dd09,0x408fd2,0x413797,0x446195,0x41103c,0x412249,0x41308d,0x40e24d,0x40f8ba,0x410a70,0x44607a,0x408cb5]
seeds=sorted(set(s for s in seeds if 0x408c44<=s<0x447da7)|set(CMDS))
CB={0x401041:'Sound_PrimeLevel1Set',0x401046:'Sound_PrimeLevel2Set',0x401050:'Sound_PrimeLevel3Set',
0x401055:'Sound_PrimeLevel4Set',0x40105a:'Sound_PrimeLevel5Set',0x40105f:'Sound_PrimeLevel6Set',
0x40107d:'Palette_Set',0x40108c:'Input_PollJoystick',0x401091:'Input_PollKeyboard',0x4010c8:'CDVol_Apply',
0x4010ff:'Video_PresentFrame',0x401104:'CD_Stop',0x401145:'CD_PlayTrack',0x40116d:'Surface_LockPrimary',
0x40119f:'Sound_Stop',0x4011d6:'Surface_UnlockPrimary',0x4011db:'Sound_PrimeMenuSet',0x4011e0:'DD_WaitVBlank',
0x4011ef:'App_PumpMessages',0x401203:'CD_Pause',0x40122b:'Sound_Play',0x401230:'CD_Resume',0x401235:'CDVol_Set'}
calls=collections.defaultdict(set); cbs=collections.defaultdict(set); cur=None
def owner(a):
    i=bisect.bisect_right(seeds,a)-1; return seeds[i] if i>=0 else None
pending=None
for l in asm:
    m=re.match(r'\s*([0-9a-f]+):\s+(\S+)\s*(.*)',l)
    if not m: continue
    a=int(m.group(1),16)
    if not 0x408c44<=a<0x447da7: continue
    o=owner(a); op,arg=m.group(2),m.group(3)
    mm=re.match(r'0x([0-9a-f]+)',arg)
    if op in('call','jmp') and mm:
        t=int(mm.group(1),16)
        if 0x408c44<=t<0x447da7 and (op=='call' or t in seeds) and owner(t)!=o: calls[o].add(owner(t))
    mm=re.search(r'ds:0x46ffb0,0x([0-9a-f]+)',arg)
    if mm: cbs[o].add(CB.get(int(mm.group(1),16),mm.group(1)))
import struct
cmds={1:0x408d06,2:0x408d58,7:0x408ceb,9:0x4443ae,13:0x408fd2,14:0x413797,15:0x446195,22:0x44607a,10:0x40b8d5}
size={s:(seeds[i+1] if i+1<len(seeds) else 0x447da7)-s for i,s in enumerate(seeds)}
for c,e in cmds.items():
    seen=set(); st=[e]
    while st:
        f=st.pop()
        if f in seen: continue
        seen.add(f); st+=calls[f]
    allcb=set().union(*(cbs[f] for f in seen))
    print(f"cmd{c:2d} {e:x}: {len(seen)} funcs, {sum(size[f] for f in seen if f in size)} bytes; callbacks={sorted(allcb)}")
# shared between level 1 and 2 = level engine
def reach(e):
    seen=set(); st=[e]
    while st:
        f=st.pop()
        if f in seen: continue
        seen.add(f); st+=calls[f]
    return seen
L=[reach(x) for x in (0x408d06,0x408d58,0x408daa,0x408dfc,0x408e4e,0x408ea0)]
common=set.intersection(*L); print('common to all levels:',len(common),'bytes',sum(size[f] for f in common))
for i,s in enumerate(L): print(f' level{i+1} specific: {len(s-common)} funcs {sum(size[f] for f in s-common)} bytes; entry calls: {[hex(x) for x in sorted(calls[[0x408d06,0x408d58,0x408daa,0x408dfc,0x408e4e,0x408ea0][i]])]}')
allreach=set().union(*(reach(e) for e in list(cmds.values())+[0x408daa,0x408dfc,0x408e4e,0x408ea0,0x40cd0f,0x40dd09,0x41103c,0x412249,0x41308d,0x40e24d,0x40f8ba,0x410a70]))
print('reached from any command:',len(allreach),'of',len(seeds),'bytes',sum(size[f] for f in allreach),'of',sum(size.values()))
