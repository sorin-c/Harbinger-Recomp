# Prototype .qst parser (docs/formats/qst.md). Usage: qst_parse.py file.qst... [-v]
import sys
sys.path.insert(0, __import__('os').path.dirname(__file__))
from dlg_parse import R, actions

def entities(r):
    out = []
    for _ in range(r.get('I')):
        ent, level, inst = r.get('HHH')
        phases = []
        for _ in range(r.get('H')):
            phases.append([r.get('HH') for _ in range(r.get('I'))])
        out.append((ent, level, inst, phases))
    return out

def parse(b):
    r = R(b); ver = r.get('d')
    states = []
    for _ in range(r.get('I')):
        name = r.str(); ents = entities(r); acts = actions(r, ver)
        states.append((name, ents, acts))
    inputs = []
    for _ in range(r.get('I')):
        src = [r.get('H') for _ in range(r.get('I'))]
        dst = r.get('H'); kind = r.get('H')
        mon = r.get('HHH')
        if kind in (0, 1, 2, 3): mon = mon + (r.get('H'),)
        elif kind not in (4, 5, 6): raise ValueError('monitor %d' % kind)
        inputs.append((src, dst, kind, mon))
    strings = []
    for _ in range(r.get('I')):
        sid = r.get('H'); text = r.str()
        strings.append((sid, text, [r.get('HH') for _ in range(r.get('I'))]))
    words = [r.get('H') for _ in range(r.get('I'))]
    nexts = []
    if ver > 1.25:
        for _ in range(r.get('I')):
            q, w = r.get('HH')
            nexts.append((q, w, [r.get('HHh') for _ in range(r.get('I'))]))
    assert r.o == len(b), (r.o, len(b))
    return ver, states, inputs, strings, words, nexts

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
