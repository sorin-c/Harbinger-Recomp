# Prototype .mgr (resource index) parser; see docs/formats/mgr.md.
# Usage: mgr_parse.py <file.mgr>... [--show ID]
import struct, sys

def parse(b):
    p = 0
    def u32():
        nonlocal p; v = struct.unpack_from('<I', b, p)[0]; p += 4; return v
    def string(nul_counted):
        # Paths store a length that counts their NUL; the trailing name uses the
        # engine's usual string encoding (length without the NUL, then NUL).
        nonlocal p; n = u32() + (0 if nul_counted else 1); raw = b[p:p + n]; p += n
        if len(raw) != n or not n or raw[-1] != 0: raise ValueError('bad string at %d' % p)
        return raw[:-1].decode('latin1')
    count = u32()
    ids = list(struct.unpack_from('<%dH' % count, b, p)); p += 2 * count
    paths = [string(True) for _ in range(count)]
    name = string(False)
    return dict(entries=dict(zip(ids, paths)), ids=ids, name=name, rest=len(b) - p)

if __name__ == '__main__':
    args = sys.argv[1:]
    show = [int(a) for a in args[args.index('--show') + 1:]] if '--show' in args else []
    files = args[:args.index('--show')] if '--show' in args else args
    for f in files:
        try:
            d = parse(open(f, 'rb').read())
            print('OK  ', f.split('res/')[-1], repr(d['name']), len(d['ids']), 'rest', d['rest'],
                  'sorted' if d['ids'] == sorted(d['ids']) else 'unsorted')
            for i in show:
                if i in d['entries']: print('     %d -> %s' % (i, d['entries'][i]))
        except Exception as e:
            print('STOP', f.split('res/')[-1], type(e).__name__, e)
