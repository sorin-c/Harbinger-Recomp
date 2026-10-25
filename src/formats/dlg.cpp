#include "formats/dlg.hpp"

#include <cstring>

#include "formats/binary.hpp"

namespace harbinger::dlg {

namespace {

constexpr const char* kNames[] = {
    "GiveItem",       "TakeItem",      "UnlockUmbilical", "ClearUmbilicals",  "GiveExp",
    "LaunchScript",   "UnlockGizmo",   "ActivateGizmo",   "GiveMoney",        "TakeMoney",
    "GiveHealth",     "SetQuest",      "EndQuest",        "QuestString",      "SetDialog",
    "LaunchTrade",    "StatMod",       "AddItemToAccount", "TakeItemFromAccount", "PrevQuestDialog",
    "NextQuestDialog", "PopDialogGroup", "SetLevel",      "DisplayText",      "ClearAccount",
};
static_assert(sizeof kNames / sizeof *kNames == size_t(ActionType::Count));

}  // namespace

const char* name(ActionType t) { return t < ActionType::Count ? kNames[size_t(t)] : "?"; }

double readF64(ByteReader& r) {
    const uint64_t lo = r.u32();
    const uint64_t bits = lo | (uint64_t(r.u32()) << 32);
    double v;
    std::memcpy(&v, &bits, sizeof v);
    return v;
}

std::vector<Action> readActions(ByteReader& r, double version) {
    const uint32_t n = r.u32();
    if (n > r.remaining() / 4) r.fail("implausible action count");
    std::vector<Action> out;
    for (uint32_t i = 0; i < n; ++i) {
        Action a;
        const uint16_t type = r.u16();
        if (type >= uint16_t(ActionType::Count)) r.fail("unknown action type");
        a.type = ActionType(type);
        a.character = r.u16();
        switch (a.type) {
            case ActionType::ClearUmbilicals: case ActionType::LaunchTrade: case ActionType::PrevQuestDialog:
            case ActionType::NextQuestDialog: case ActionType::PopDialogGroup:
                break;
            case ActionType::StatMod: case ActionType::AddItemToAccount: case ActionType::TakeItemFromAccount:
                a.id = r.u16();
                a.value = r.u16();
                break;
            case ActionType::QuestString: case ActionType::DisplayText:
                a.text = r.string();
                break;
            case ActionType::GiveExp:
                a.amount = r.u32();
                a.id = uint16_t(a.amount);
                break;
            case ActionType::UnlockUmbilical:
                // GUnlockUmbEntityAction_v5: the later fields by version
                // (DAT_005492c0 = 1.0, DAT_0054a048 = 1.5).
                a.id = r.u16();
                if (version > 1.0) {
                    a.gold = r.u16() != 0;
                    a.entry = r.u16();
                }
                if (version > 1.5) a.announce = r.u16() != 0;
                break;
            default:
                a.id = r.u16();
                break;
        }
        out.push_back(std::move(a));
    }
    return out;
}

const Node* Dialog::find(const std::string& name) const {
    for (const auto& n : nodes)
        if (n.name == name) return &n;
    return nullptr;
}

Dialog parse(const std::vector<uint8_t>& data) {
    ByteReader r(data, "dlg");
    Dialog d;
    d.version = readF64(r);
    // FUN_0047f5c0: the nodes; FUN_0047f3a0: each node's responses.
    const uint32_t nodes = r.u32();
    if (nodes > r.remaining() / 12) r.fail("implausible node count");
    for (uint32_t i = 0; i < nodes; ++i) {
        Node n;
        n.text = r.string();
        n.name = r.string();
        const uint32_t responses = r.u32();
        if (responses > r.remaining() / 14) r.fail("implausible response count");
        for (uint32_t j = 0; j < responses; ++j) {
            Response s;
            s.text = r.string();
            s.target = r.string();
            s.actions = readActions(r, d.version);
            if (d.version > 1.0) {
                const uint16_t c = r.u16();
                if (c > uint16_t(Condition::MoneyBelow)) r.fail("unknown response condition");
                s.condition = Condition(c);
                s.param = r.u16();
            }
            n.responses.push_back(std::move(s));
        }
        d.nodes.push_back(std::move(n));
    }
    // FUN_0047ed70: from version 1.0 the portrait and the actions.
    if (d.version > 1.0) {
        d.portrait = r.u16();
        d.actions = readActions(r, d.version);
    }
    r.expectEnd();
    return d;
}

}  // namespace harbinger::dlg
