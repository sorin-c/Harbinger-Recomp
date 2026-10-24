# Recover MSVC RTTI: TypeDescriptor -> CompleteObjectLocator -> vftable -> virtual functions.
import pefile,struct,re,sys
pe=pefile.PE(sys.argv[1]); base=pe.OPTIONAL_HEADER.ImageBase
img=pe.get_memory_mapped_image()
text=[s for s in pe.sections if s.Name.startswith(b'.text')][0]
tlo=base+text.VirtualAddress; thi=tlo+text.Misc_VirtualSize
def u32(va): return struct.unpack_from('<I',img,va-base)[0]
def demangle(n):
    n=n[4:-2] if n.startswith('.?AV') or n.startswith('.?AU') else n
    return n
tds={}
for m in re.finditer(rb'\.\?A[VU][^\x00]+\x00',img):
    tds[base+m.start()-8]=demangle(m.group()[:-1].decode('latin1'))
cols={}
for off in range(0,len(img)-20,4):
    td=struct.unpack_from('<I',img,off+12)[0]
    if td in tds and struct.unpack_from('<I',img,off)[0]==0:
        cols[base+off]=(tds[td],struct.unpack_from('<I',img,off+4)[0])
ptr_to={}
for off in range(0,len(img)-4,4):
    v=struct.unpack_from('<I',img,off)[0]
    if v in cols: ptr_to.setdefault(v,[]).append(base+off+4)
out=open(sys.argv[2],'w'); nv=0; nf=0
for col,(cls,moff) in cols.items():
    for vt in ptr_to.get(col,[]):
        nv+=1; i=0
        while True:
            va=vt+i*4
            if va-base+4>len(img): break
            f=u32(va)
            if not (tlo<=f<thi): break
            if i>0 and (va in [p for l in ptr_to.values() for p in l]): break
            out.write('%s\t%d\t%d\t%08x\t%08x\n'%(cls,moff,i,vt,f)); i+=1; nf+=1
print('typedescs',len(tds),'cols',len(cols),'vtables',nv,'slots',nf)
