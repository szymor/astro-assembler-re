#!/usr/bin/env python3
"""Find function seeds in ASTRO.exe for Ghidra: incremental-link table targets,
direct call targets (linear sweep with objdump) and code pointers found
through base relocations. Feed the output to re/ghidra_scripts/SeedFunctions.java.

Usage: find_seeds.py ASTRO.exe seeds.txt
Output lines: "F <hex>" create function, "D <hex>" disassemble only.
"""
import struct,sys,re,subprocess,collections
path=sys.argv[1]; exe=open(path,'rb').read()
pe=struct.unpack_from('<I',exe,0x3c)[0]; ns=struct.unpack_from('<H',exe,pe+6)[0]; osz=struct.unpack_from('<H',exe,pe+20)[0]
opt=pe+24; base=struct.unpack_from('<I',exe,opt+28)[0]
secs=[]
for i in range(ns):
    n,vs,va,rs,rp=struct.unpack_from('<8sIIII',exe,opt+osz+i*40); secs.append((n.rstrip(b'\0').decode(),base+va,max(vs,rs),rp,rs))
def sec(a):
    for n,va,sz,rp,rs in secs:
        if va<=a<va+sz: return n
def rd(a):
    for n,va,sz,rp,rs in secs:
        if va<=a<va+rs: return struct.unpack_from('<I',exe,rp+a-va)[0]
relrva,relsz=struct.unpack_from('<II',exe,opt+96+5*8)
r=[s for s in secs if s[0]=='.reloc'][0]; p=r[3]; end=p+relsz; fix=[]
while p<end:
    page,blk=struct.unpack_from('<II',exe,p)
    for i in range((blk-8)//2):
        e=struct.unpack_from('<H',exe,p+8+2*i)[0]
        if e>>12==3: fix.append(base+page+(e&0xfff))
    p+=blk
c=collections.Counter(); textptr=collections.defaultdict(set)
for f in fix:
    v=rd(f); s,t=sec(f),sec(v); c[(s,t)]+=1
    if t=='.text': textptr[s].add(v)
for k,v in sorted(c.items(),key=lambda x:-x[1]): print(k,v)
# objdump sweep for direct calls
out=subprocess.run(['objdump','-d','--no-show-raw-insn','-M','intel','--start-address=0x401000','--stop-address=0x46b000',path],capture_output=True,text=True).stdout
calls=set(int(m.group(1),16) for m in re.finditer(r'\scall\s+0x([0-9a-f]+)',out))
calls={a for a in calls if 0x401000<=a<0x46b000}
print('direct call targets',len(calls))
print('text ptrs from .data',len(textptr['.data']),'from .text',len(textptr['.text']),'from .rdata',len(textptr['.rdata']))
# incremental-link table: run of 'jmp rel32' at the start of .text, one per function
t=[s for s in secs if s[0]=='.text'][0]; txt=exe[t[3]:t[3]+t[4]]; ilt=set(); p=0
while txt[p]==0xE9:
    ilt.add(t[1]+p+5+struct.unpack_from('<i',txt,p+1)[0]); p+=5
print('ILT entries',len(ilt))
lines=['F %x'%a for a in sorted(calls|ilt|textptr['.data']|textptr['.rdata'])]+['D %x'%a for a in sorted(textptr['.text']-calls)]
open(sys.argv[2],'w').write('\n'.join(lines)+'\n')
# regions: game asm block boundaries
t=[s for s in secs if s[0]=='.text'][0]; txt=exe[t[3]:t[3]+t[4]]
for m in re.finditer(rb'\xcc{256,}',txt): print('CC run %x-%x'%(t[1]+m.start(),t[1]+m.end()))
