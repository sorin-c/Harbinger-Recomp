import glob,struct,collections,sys
def parse(b):
    w,h,pitch,kind,flags,x,y=struct.unpack_from('<HHHBBhh',b,0); p=12
    fmt=flags&0xf; pal=None; extra=None; data=None
    if kind==1 and flags&0x20:
        n=struct.unpack_from('<H',b,p)[0]; p+=2; pal=b[p:p+4*n]; p+=4*n
    if fmt&3:
        extra=b[p:p+8]; p+=8
    if flags&0x10:
        sz=struct.unpack_from('<I',b,p)[0]; p+=4; data=b[p:p+sz]; p+=sz
    return dict(w=w,h=h,pitch=pitch,kind=kind,flags=flags,fmt=fmt,x=x,y=y,pal=pal,extra=extra,data=data,end=p)
if __name__=='__main__':
    ok=bad=0; c=collections.Counter(); ex=collections.Counter()
    for f in glob.glob('ex0/**/*.gim',recursive=True)+glob.glob('ex1/**/*.gim',recursive=True):
        b=open(f,'rb').read(); g=parse(b)
        if g['end']==len(b): ok+=1
        else:
            bad+=1
            if bad<5: print('BAD',f,len(b),g['end'],b[:16].hex(' '))
        k=(g['kind'],g['flags']); c[k]+=1
        if g['data'] is not None: ex[(k, 'raw' if len(g['data'])==g['pitch']*g['h']*(1 if g['kind']==1 else g['kind']) else 'other')]+=1
    print('ok',ok,'bad',bad); print(c); print(ex)
