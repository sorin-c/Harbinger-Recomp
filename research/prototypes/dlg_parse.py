# Prototype .dlg parser (docs/formats/dlg.md). Usage: dlg_parse.py file.dlg... [-v]
import struct, sys

ID = {0, 1, 5, 6, 7, 8, 9, 10, 11, 12, 14, 22, 24}
BASE = {3, 15, 19, 20, 21}
TEXT = {13, 23}
PAIR = {16, 17, 18}

class R:
    def __init__(s, b): s.b, s.o = b, 0
    def get(s, f):
        n = struct.calcsize(f); v = struct.unpack_from('<' + f, s.b, s.o); s.o += n
        return v if len(v) > 1 else v[0]
    def str(s):
        n = s.get('I'); t = s.b[s.o:s.o + n]; assert s.b[s.o + n] == 0; s.o += n + 1
        return t.decode('latin1')

def actions(r, ver):
    out = []
    for _ in range(r.get('I')):
        t = r.get('H'); who = r.get('H')
        if t in BASE: a = ()
        elif t in ID: a = (r.get('H'),)
        elif t in PAIR: a = r.get('HH')
        elif t in TEXT: a = (r.str(),)
        elif t == 4: a = (r.get('I'),)
        elif t == 2:
            a = [r.get('H')]
            if ver > 1.0: a += list(r.get('HH'))
            if ver > 1.5: a.append(r.get('H'))
            a = tuple(a)
        else: raise ValueError('action %d' % t)
        out.append((t, who) + a)
    return out

def parse(b):
    r = R(b); ver = r.get('d'); nodes = []
    for _ in range(r.get('I')):
        text = r.str(); name = r.str(); resp = []
        for _ in range(r.get('I')):
            rt = r.str(); tgt = r.str(); acts = actions(r, ver)
            cond = r.get('HH') if ver > 1.0 else (0, 0)
            resp.append((rt, tgt, acts, cond))
        nodes.append((text, name, resp))
    extra, root = None, []
    if ver > 1.0: extra = r.get('H'); root = actions(r, ver)
    assert r.o == len(b), (r.o, len(b))
    return ver, nodes, extra, root

if __name__ == '__main__':
    files = [a for a in sys.argv[1:] if a != '-v']
    ok = 0
    for f in files:
        try:
            d = parse(open(f, 'rb').read()); ok += 1
            if '-v' in sys.argv: print(f, d)
        except Exception as e:
            print('FAIL', f, repr(e))
    print('%d/%d parsed to the byte' % (ok, len(files)))
