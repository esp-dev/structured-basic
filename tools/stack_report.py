"""Conservative AVR C call-path estimate using .su records and disassembly.
Library/ISR margins are explicit review assumptions, not measured watermarks.
"""
from pathlib import Path
import json
import re

root=Path(__file__).resolve().parents[1]
folder=root/'build/1.0/avr'
if not folder.exists(): folder=root/'build/avr'
frames={}
rows=[]
for path in folder.glob('*.su'):
    for line in path.read_text().splitlines():
        location,amount,kind=line.split('\t')
        if kind not in ('static','dynamic,bounded'): raise RuntimeError('Unbounded frame: '+line)
        name=location.rsplit(':',1)[-1]
        frames[name]=max(frames.get(name,0),int(amount))
        rows.append(dict(file=path.name,function=name,bytes=int(amount),kind=kind))
disassembly=(folder/'disassembly.txt').read_text()
edges={name:set() for name in frames}
current=None
callbacks={'read_source','output','input','poll','device','nvread','nvwrite'}
for line in disassembly.splitlines():
    match=re.match(r'^([0-9a-f]+) <([^>]+)>:',line)
    if match:
        current=match.group(2) if match.group(2) in frames else None
        continue
    if current is None: continue
    if re.search(r'\b(?:icall|eicall)\b',line): edges[current].update(callbacks & frames.keys())
    match=re.search(r'\b(?:call|rcall|jmp|rjmp)\b.*<([^>+]+)(?:\+[^>]*)?>',line)
    if match and match.group(1) in frames and match.group(1)!=current:
        edges[current].add(match.group(1))

def longest(name,active=()):
    if name in active: raise RuntimeError('C call graph cycle: '+' -> '.join((*active,name)))
    child=max((longest(x,(*active,name)) for x in edges[name]),default=(0,[]),key=lambda x:x[0])
    return frames[name]+child[0],[name,*child[1]]

amount,path=longest('main')
interrupt=max((n for name,n in frames.items() if name.startswith('__vector_')),default=0)
# Includes AVR libc printf/strtod/math helper frames, switch helpers, assembly
# return addresses and conservative headroom; review linked assembly on change.
library_margin=512
interrupt_reserve=max(128,interrupt)
reservation=((amount+library_margin+interrupt_reserve+255)//256)*256
if (folder/'sections.json').exists(): sections=json.loads((folder/'sections.json').read_text())
else: sections=json.loads((root/'build/1.0/report.json').read_text())['avr']['sections']
static=sections.get('.data',0)+sections.get('.bss',0)
result=dict(frames=rows,graph={k:sorted(v) for k,v in edges.items()},maximum_c_path=path,
            c_path_bytes=amount,library_margin=library_margin,largest_isr_frame=interrupt,
            interrupt_reserve=interrupt_reserve,c_stack_reservation=reservation,
            static_sram=static,sram_after_reservation=16384-static-reservation,
            measured_on_hardware=False,
            assumptions=['Stock non-reentrant platform callbacks','Only the supplied Timer0 ISR',
                         'No C recursion; BASIC frames stay in the pool',
                         '512-byte reviewed AVR library/helper margin'])
if result['sram_after_reservation']<0: raise RuntimeError('C stack estimate exceeds SRAM')
(folder/'stack.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k not in ('frames','graph')},indent=2))
