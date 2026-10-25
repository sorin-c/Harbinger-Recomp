#include "formats/qst.hpp"

#include "formats/binary.hpp"

namespace harbinger::qst {

Quest parse(const std::vector<uint8_t>& data) {
    ByteReader r(data, "qst");
    Quest q;
    q.version = dlg::readF64(r);

    // GStateMachine_v4: the states (GEntityState_v3: name, FUN_0048af00's
    // entity dialogs, the entry actions)...
    const uint32_t states = r.u32();
    if (states > r.remaining() / 13) r.fail("implausible state count");
    for (uint32_t i = 0; i < states; ++i) {
        State s;
        s.name = r.string();
        const uint32_t entities = r.u32();
        if (entities > r.remaining() / 8) r.fail("implausible entity count");
        for (uint32_t j = 0; j < entities; ++j) {
            EntityDialogs e;
            e.kind = r.u16();
            e.level = r.u16();
            e.instance = r.u16();
            const uint16_t phases = r.u16();
            for (uint16_t p = 0; p < phases; ++p) {
                const uint32_t n = r.u32();
                if (n > r.remaining() / 4) r.fail("implausible dialog count");
                std::vector<EntityDialogs::Choice> choices;
                for (uint32_t k = 0; k < n; ++k) {
                    EntityDialogs::Choice c;
                    c.character = r.u16();
                    c.dialog = r.u16();
                    choices.push_back(c);
                }
                e.phases.push_back(std::move(choices));
            }
            s.entities.push_back(std::move(e));
        }
        s.actions = dlg::readActions(r, q.version);
        q.states.push_back(std::move(s));
    }
    // ...then the inputs (GMachineInput_v2: source states and target;
    // GEntityStateInput_v2: the monitor kind, FUN_004bf910 makes it, and
    // its slot 8 reads its words).
    const uint32_t inputs = r.u32();
    if (inputs > r.remaining() / 14) r.fail("implausible input count");
    for (uint32_t i = 0; i < inputs; ++i) {
        Input in;
        const uint32_t from = r.u32();
        if (from > r.remaining() / 2) r.fail("implausible source count");
        for (uint32_t j = 0; j < from; ++j) in.from.push_back(r.u16());
        in.to = r.u16();
        const uint16_t kind = r.u16();
        if (kind > uint16_t(EntityKind::Kit)) r.fail("unknown monitor kind");
        in.kind = EntityKind(kind);
        in.instance = r.u16();
        in.word = r.u16();
        in.event = r.u16();
        if (kind <= uint16_t(EntityKind::Player)) in.level = r.u16();  // GAgentMonitor_v8
        q.inputs.push_back(std::move(in));
    }

    // GQuest_v4: FUN_004f62a0's entries, a word list, and from 1.25 the
    // quests that may follow (FUN_004f64d0, GQuestNode_v2, FUN_004f8eb0).
    const uint32_t entries = r.u32();
    if (entries > r.remaining() / 11) r.fail("implausible entry count");
    for (uint32_t i = 0; i < entries; ++i) {
        Entry e;
        e.id = r.u16();
        e.text = r.string();
        const uint32_t n = r.u32();
        if (n > r.remaining() / 4) r.fail("implausible pair count");
        for (uint32_t j = 0; j < n; ++j) {
            const uint16_t a = r.u16();
            e.pairs.emplace_back(a, r.u16());
        }
        q.entries.push_back(std::move(e));
    }
    const uint32_t words = r.u32();
    if (words > r.remaining() / 2) r.fail("implausible word count");
    for (uint32_t i = 0; i < words; ++i) q.words.push_back(r.u16());
    if (q.version > 1.25) {
        const uint32_t next = r.u32();
        if (next > r.remaining() / 8) r.fail("implausible next-quest count");
        for (uint32_t i = 0; i < next; ++i) {
            Next n;
            n.quest = r.u16();
            n.weight = r.u16();
            const uint32_t adj = r.u32();
            if (adj > r.remaining() / 6) r.fail("implausible adjustment count");
            for (uint32_t j = 0; j < adj; ++j) {
                Next::Adjust a;
                a.quest = r.u16();
                a.result = r.u16();
                a.delta = r.i16();
                n.adjust.push_back(a);
            }
            q.next.push_back(std::move(n));
        }
    }
    r.expectEnd();
    return q;
}

}  // namespace harbinger::qst
