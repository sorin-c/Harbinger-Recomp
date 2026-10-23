# Prototype .fnt (bitmap font) parser, following the engine's loader FUN_00491580.
# Usage: fnt_parse.py <file.fnt>... [--render "text" out.png]
import struct, sys

def parse(b):
    p = 0
    glyphs = []
    for i in range(96):  # characters 0x20..0x7f
        w, h, pitch, top, adv, off = struct.unpack_from('<5hI', b, p); p += 14
        glyphs.append(dict(ch=chr(0x20 + i), w=w, h=h, pitch=pitch, top=top, adv=adv, off=off))
    size = struct.unpack_from('<I', b, p)[0]; p += 4
    pixels = b[p:p + size]; p += size
    ncol = struct.unpack_from('<h', b, p)[0]; p += 2
    palette = [tuple(b[p + 4 * i:p + 4 * i + 4]) for i in range(ncol)]; p += 4 * ncol
    kern = struct.unpack_from('<9216b', b, p); p += 0x2400
    space = struct.unpack_from('<h', b, p)[0]; p += 2
    kdefault, rle = struct.unpack_from('<bB', b, p); p += 2
    return dict(glyphs=glyphs, pixels=pixels, palette=palette, kern=kern, space=space,
                kern_default=kdefault, rle=rle, rest=len(b) - p)

if __name__ == '__main__':
    for f in [a for a in sys.argv[1:] if a.endswith('.fnt')]:
        d = parse(open(f, 'rb').read())
        g = d['glyphs']
        print('%-18s rest %d pixels %d colours %d space %d kdef %d rle %d A=%s' % (
            f.split('/')[-1], d['rest'], len(d['pixels']), len(d['palette']), d['space'],
            d['kern_default'], d['rle'], {k: g[0x21][k] for k in ('w', 'h', 'pitch', 'top', 'adv')}))
