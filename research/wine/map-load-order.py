# Turn an strace log of the original game into the order in which it reads
# archive entries and loose files.
#   map-load-order.py <outdir> <strace.log> <dir with res0.sqh.json/res1.sqh.json>
# The .json listings come from research/prototypes/sqh_parse.py.
import re,sys,json,bisect
S=sys.argv[1]
R=sys.argv[3]
idx={}
for a in ['res0','res1']:
    L=sorted(json.load(open(R+'/'+a+'.sqh.json')),key=lambda x:x[1]); idx[a]=( [x[1] for x in L], L)
def entry(a,off):
    offs,L=idx[a]; i=bisect.bisect_right(offs,off)-1
    if i>=0 and L[i][1]<=off<L[i][1]+L[i][2]: return L[i][0]
    return None
pos={}; order=[]; seen=set(); loose=[]
rx=re.compile(r'^(\d+) (\S+) (\w+)\((\d+)<([^>]*)>(.*)$')
for line in open(sys.argv[2]):
    m=rx.match(line)
    if not m: 
        mo=re.search(r'openat\(.*?"([^"]*/game/[^"]*)".*= \d+',line)
        if mo and 'ENOENT' not in line:
            p=mo.group(1).split('/game/',1)[1]
            if p and p not in seen and not p.endswith('.sqh'): seen.add(p); loose.append((line.split()[1],p))
        continue
    pid,t,call,fd,path,rest=m.groups()
    a='res0' if path.endswith('res0.sqh') else 'res1' if path.endswith('res1.sqh') else None
    if not a: continue
    key=(pid,fd)
    if call=='_llseek':
        mm=re.match(r', (-?\d+), \[(\d+)\], SEEK_(\w+)',rest)
        if mm: pos[key]=int(mm.group(2))
    elif call in('read','pread64'):
        mm=re.search(r'= (\d+)$',rest.strip())
        if not mm: continue
        n=int(mm.group(1))
        if call=='pread64':
            off=int(re.search(r', (\d+)\) = ',rest).group(1))
        else:
            off=pos.get(key,0); pos[key]=off+n
        e=entry(a,off)
        if e and (a,e) not in seen:
            seen.add((a,e)); order.append((t,a,e))
json.dump({'archive':order,'loose':loose},open(S+'/loadorder.json','w'),indent=0)
print(len(order),'archive entries;',len(loose),'loose files')
import collections
c=collections.Counter(e.rsplit('.',1)[-1] for _,_,e in order); print(c.most_common())
for x in order[:40]: print(*x)
print('...'); 
for x in loose[:20]: print(*x)
