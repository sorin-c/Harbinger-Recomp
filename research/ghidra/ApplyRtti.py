# Name vtables and virtual functions from rtti.tsv (class, thisOffset, slot, vtable, func).
import os
from ghidra.program.model.symbol import SourceType
out = os.environ.get('GOUT', '/tmp')
rows = [l.rstrip('\n').split('\t') for l in open(os.path.join(out, 'rtti.tsv'))]
vtlen = {}
for c, mo, i, vt, f in rows: vtlen[vt] = max(vtlen.get(vt, 0), int(i) + 1)
owner = {}
for c, mo, i, vt, f in rows:
    k = (vtlen[vt], c)
    if f not in owner or k < owner[f][0]: owner[f] = (k, c, i)
st = currentProgram.getSymbolTable(); fm = currentProgram.getFunctionManager(); n = 0
seen = set()
for c, mo, i, vt, f in rows:
    if vt not in seen:
        seen.add(vt)
        try: st.createLabel(toAddr(vt), '%s_vftable_%s' % (c.replace('::','_').replace('<','_').replace('>','_'), mo), SourceType.ANALYSIS)
        except: pass
for f, (k, c, i) in owner.items():
    fn = fm.getFunctionAt(toAddr(f))
    if fn is None:
        fn = createFunction(toAddr(f), None)
    if fn is not None and (fn.getName().startswith('FUN_') or '_v' in fn.getName()):
        safe = ''.join(ch if ch.isalnum() or ch == '_' else '_' for ch in c)
        fn.setName('%s_v%s' % (safe, i), SourceType.ANALYSIS); n += 1
print('renamed', n)
