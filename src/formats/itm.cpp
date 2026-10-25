#include "formats/itm.hpp"

#include <vector>

#include "formats/binary.hpp"

namespace harbinger::itm {

int spriteLayer(uint16_t classId) {
    // The item's kind (vtable slot 5): 1 armour, 9 torso -> layer 0; 5 human
    // weapon, 0xd gladiator blade -> 1; 3 helmet, 0xb gladiator gun -> 2;
    // gauntlets (0xf, 0x11) change no layer.
    switch (classId) {
        case HumanArmor:
        case GladiatorTorso: return 0;
        case HumanWeapon:
        case GladiatorMelee: return 1;
        case HumanHelmet:
        case GladiatorRanged: return 2;
        default: return -1;
    }
}

uint16_t equipKind(const Item& item) {
    switch (item.classId) {
        case HumanWeapon: return 5;
        case HumanArmor: return 1;
        case HumanHelmet: return 3;
        case GladiatorRanged: return 0xb;
        case GladiatorMelee: return 0xd;
        case GladiatorTorso: return 9;
        case LeftGauntlet: return 0xf;
        case RightGauntlet: return 0x11;
        case CameraDrop: return 7;
        case MineDrop: return 0x16;
        case OffensiveAmplifier:
        case DefensiveAmplifier: return 0x13;
        case Injector:
            // GInjectorItem_v5
            if (!item.injector) return 0;
            return item.injector->type == 0 ? 8 : item.injector->type == 2 ? 0x15 : 0;
        case ModChip: {
            // GModChipItem_v5: the target to the kind of the socket it fits.
            static constexpr uint16_t kSocket[] = {0, 2, 4, 6, 10, 0xc, 0xe, 0x12, 0x12};
            if (!item.modChip || item.modChip->target > 8) return 0;
            return kSocket[item.modChip->target];
        }
        default: return 0;
    }
}

namespace {

Item readHeader(ByteReader& r) {
    Item it;
    // Common header (FUN_004d5c10): 14 bytes, a u32, the name. The second
    // word over 256 is the version the class loaders test.
    it.classId = r.u16();
    it.versionWord = r.u16();
    it.unknown2 = r.u16();
    it.image = r.u16();
    it.frameCount = r.u16();
    it.unknown5 = r.u16();
    it.unknown6 = r.u16();
    it.unknown7 = r.u32();
    it.name = r.string();
    // A flag byte; when set, 8 bytes follow (FUN_004d5d10 -> FUN_004d6fc0).
    if (r.u8() != 0) {
        std::vector<uint8_t> b(8);
        for (auto& v : b) v = r.u8();
        it.block = std::move(b);
    }
    return it;
}

}  // namespace

Item parseHeader(const std::vector<uint8_t>& data) {
    ByteReader r(data, "itm");
    return readHeader(r);
}

Item parse(const std::vector<uint8_t>& data) {
    ByteReader r(data, "itm");
    Item it = readHeader(r);
    const double version = it.version();
    // Class body (vtable slot 3).
    if (it.classId == AnimatedItem) {
        Animation a;
        a.loopStart = r.u16();
        a.loopEnd = r.u16();
        a.pressEnd = r.u16();
        it.animation = a;
    } else if (it.classId >= HumanWeapon && it.classId <= RightGauntlet) {
        Socketed s;
        s.word38 = r.u16();
        s.byte3c = r.u8();
        s.animGroup = r.u16();
        const bool armour = it.classId == HumanArmor || it.classId == HumanHelmet || it.classId == GladiatorTorso;
        if (armour && version > 1.1) s.word54 = r.u16();
        if (version > 2.1) {
            s.listWord = r.u16();
            const uint16_t n = r.u16();
            for (uint16_t i = 0; i < n; ++i) {
                const uint16_t a = r.u16();
                s.list.emplace_back(a, r.u16());
            }
        }
        it.socketed = std::move(s);
    } else if (it.classId == ModChip) {
        // GModChipItem_v3: two words, a third from 1.0, a fourth from 1.1.
        ModChipBody m;
        m.target = r.u16();
        m.word38 = r.u16();
        if (version > 1.0) m.word3a = r.u16();
        if (version > 1.1) m.word3c = r.u16();
        it.modChip = m;
    } else if (it.classId == Injector) {
        // GInjectorItem_v3: a byte and a word, another word from 1.1.
        InjectorBody j;
        j.type = r.u8();
        j.word3c = r.u16();
        if (version > 1.1) j.word40 = r.u16();
        it.injector = j;
    } else {
        r.fail("item class not decoded yet");
    }
    if (version > 1.2) it.trailer = r.u16();
    r.expectEnd();
    return it;
}

}  // namespace harbinger::itm
