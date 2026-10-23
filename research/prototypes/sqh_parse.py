import struct,sys,collections,os,json
f=open(sys.argv[1],'rb'); d=f.read(4); dsz=struct.unpack('<I',d)[0]; T=d+f.read(dsz-4)
out=[]; ndirs=0; fv=collections.Counter()
def node(p,bound,path):
    global ndirs
    while p<bound:
        n=T[p]; name=T[p+1:p+n].decode('latin1'); assert T[p+n]==0,hex(p)
        p+=1+n; v=struct.unpack('>I',T[p:p+4])[0]; p+=4; q=p
        if '.' in name:
            fv[v]+=1
            off,sz=struct.unpack('>II',T[p:p+8]); p+=8; out.append((path+name,off,sz))
        else:
            ndirs+=1; e=bound if v==0xffffffff else q+v
            p=node(p,e,path+name+'/'); assert p==e
    return p
end=node(4,dsz,'')
print(sys.argv[1].split('/')[-1],'dirsize',dsz,'end',end,'dirs',ndirs,'files',len(out),'filev',fv)
ext=collections.Counter(o[0].rsplit('.',1)[-1].lower() for o in out); print(ext.most_common(50))
o2=sorted(out,key=lambda x:x[1]); gaps=sum(1 for a,b in zip(o2,o2[1:]) if a[1]+a[2]!=b[1]); print('gaps',gaps,'first',o2[0][1],'end',o2[-1][1]+o2[-1][2],'filesize',os.path.getsize(sys.argv[1]))
json.dump(out,open(sys.argv[1].split('/')[-1]+'.json','w'))
