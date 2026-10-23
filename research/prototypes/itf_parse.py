# Prototype .itf (interface layout) parser; see docs/formats/itf.md.
# Usage: itf_parse.py <file.itf>...   (prints a summary, or where parsing stopped)
import struct, sys

CLASS_NAMES = {
    0x00: 'Backgd', 0x01: 'HealthBtn', 0x02: 'InjectorBtn', 0x03: 'Button', 0x04: 'ThreeStateBtn',
    0x05: 'DropItemBtn', 0x06: 'HealthBar', 0x07: 'EnergyBar', 0x08: 'WarningLight',
    0x09: 'GladHealthBtn', 0x0a: 'GladHealthBar', 0x0b: 'ScrollButton', 0x0c: 'ScrollButton',
    0x0d: 'StaticTextBox', 0x0e: 'ScanTextBox', 0x0f: 'BlinkBtn', 0x10: 'InventoryGrid',
    0x17: 'HumDropItemEquipSlot', 0x24: 'InvThreeStateBtn', 0x26: 'InventoryEquipMask',
    0x27: 'CulHealthBtn', 0x28: 'CulHealthBar', 0x29: 'CulAmplifierBtn', 0x2a: 'EnergyBar',
    0x2b: 'EnemyHealthBar', 0x2c: 'CameraHealthBar', 0x2d: 'GladiatorToggleBtn',
    0x2e: 'StaticTextBox', 0x2f: 'StaticTextBox', 0x30: 'GladDropItemEquipSlot',
    0x31: 'GladDropItemBtn', 0x32: 'WeaponModBtn', 0x33: 'Map', 0x34: 'TradeTabBtn',
    0x35: 'TradeGrid', 0x36: 'TradePrevPageBtn', 0x37: 'TradeNextPageBtn', 0x38: 'TradeCloseBtn',
    0x39: 'PictureSlot', 0x3a: 'TextBtn', 0x3b: 'KeyField', 0x3c: 'CheckBox', 0x3d: 'SliderBar',
    0x3e: 'StatsName', 0x47: 'StatsLevel', 0x48: 'StatsBonus', 0x49: 'PlayerIcon',
    0x4a: 'StatsNextLevel', 0x4b: 'StatsThreeStateBtn', 0x50: 'AnimatedBtn', 0x51: 'Icon',
    0x52: 'StashGrid', 0x53: 'StashPrevPageBtn', 0x54: 'StashNextPageBtn', 0x55: 'TradeCloseBtn',
    0x56: 'QuestLogButton',
}
for c in range(0x11, 0x26):
    CLASS_NAMES.setdefault(c, 'EquipSlot')
for c in range(0x3f, 0x43): CLASS_NAMES[c] = 'StatsSkillName'
for c in range(0x43, 0x47): CLASS_NAMES[c] = 'StatsSkillValue'
for c in range(0x4c, 0x50): CLASS_NAMES[c] = 'StatsPlusBtn'

# Class-specific fields read by each class's load method (vtable slot 5).
# Layout codes: H = u16, I = u32, T = 16-byte text-box block (8 x u16), S = string.
EQUIP = set(range(0x11, 0x26)) - {0x24} | {0x30}
LAYOUT = {}
for c in (0x03, 0x04, 0x0b, 0x0c, 0x0f, 0x24, 0x36, 0x37, 0x38, 0x55, 0x3c, 0x3d, 0x4b,
          0x4c, 0x4d, 0x4e, 0x4f, 0x53, 0x54, 0x56):
    LAYOUT[c] = 'IS'
for c in (0x0d, 0x2e, 0x2f, *range(0x3e, 0x49)):
    LAYOUT[c] = 'TS'
for c in EQUIP: LAYOUT[c] = 'IHH'
LAYOUT.update({0x0e: 'I', 0x35: 'HH', 0x52: 'HH', 0x39: 'I', 0x3a: 'IIS', 0x3b: 'IIS',
               0x49: 'H', 0x51: 'H', 0x50: 'HIS'})

class R:
    def __init__(s, b): s.b, s.p = b, 0
    def take(s, n):
        if s.p + n > len(s.b): raise EOFError('truncated at %d' % s.p)
        v = s.b[s.p:s.p + n]; s.p += n; return v
    def u8(s): return s.take(1)[0]
    def u16(s): return struct.unpack('<H', s.take(2))[0]
    def i16(s): return struct.unpack('<h', s.take(2))[0]
    def u32(s): return struct.unpack('<I', s.take(4))[0]
    def str(s):
        n = s.u32(); raw = s.take(n + 1)
        if raw[-1] != 0: raise ValueError('string not NUL-terminated at %d' % s.p)
        return raw[:-1].decode('latin1')

def specific(r, cls):
    if cls not in CLASS_NAMES: raise NotImplementedError('class 0x%x' % cls)
    out = []
    for code in LAYOUT.get(cls, ''):
        out.append({'H': r.u16, 'I': r.u32, 'S': r.str,
                    'T': lambda: struct.unpack('<8H', r.take(16))}[code]())
    return {'fields': out}

def parse(b):
    r = R(b)
    hdr = struct.unpack('<3i', r.take(12))
    count, kind = r.u16(), r.u16()
    ctrls = []
    for _ in range(count):
        cls = r.u16(); x, y, z = r.i16(), r.i16(), r.i16()
        name = r.str(); images = [r.str() for _ in range(r.u32())]; extra = r.str()
        ctrls.append(dict(cls=cls, x=x, y=y, z=z, name=name, images=images, extra=extra, **specific(r, cls)))
    sounds = None
    if count:
        if r.u8() == 1:
            n = r.u16(); sounds = list(struct.unpack('<%dH' % n, r.take(2 * n)))
    return dict(header=hdr, kind=kind, controls=ctrls, sounds=sounds, rest=len(b) - r.p)

if __name__ == '__main__':
    for f in sys.argv[1:]:
        try:
            d = parse(open(f, 'rb').read())
            print('OK  ', f.split('res/')[-1], 'kind', d['kind'], 'hdr', d['header'], 'ctrls', len(d['controls']), 'rest', d['rest'], 'sounds', d['sounds'])
        except Exception as e:
            print('STOP', f.split('res/')[-1], type(e).__name__, e)
