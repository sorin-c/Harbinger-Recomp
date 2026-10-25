// Tests for the dialogue (.dlg) and quest (.qst) parsers and the story
// logic (world::Story). Synthetic cases always run; with HARBINGER_DATA set,
// every shipped dialogue and every quest the indexes name must parse, and
// the Human's opening plays out as the files say.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/binary.hpp"
#include "formats/dlg.hpp"
#include "formats/mgr.hpp"
#include "formats/qst.hpp"
#include "world/story.hpp"

using namespace harbinger;

static int failures = 0;
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

template <typename F>
static bool throws(F f) {
    try {
        f();
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

struct Bytes {
    std::vector<uint8_t> d;
    Bytes& u8(uint8_t v) { d.push_back(v); return *this; }
    Bytes& u16(uint16_t v) { return u8(uint8_t(v)).u8(uint8_t(v >> 8)); }
    Bytes& u32(uint32_t v) { return u16(uint16_t(v)).u16(uint16_t(v >> 16)); }
    Bytes& f64(double v) {
        uint64_t b;
        static_assert(sizeof b == sizeof v);
        std::memcpy(&b, &v, sizeof b);
        return u32(uint32_t(b)).u32(uint32_t(b >> 32));
    }
    Bytes& str(const std::string& s) {
        u32(uint32_t(s.size()));
        for (char c : s) u8(uint8_t(c));
        return u8(0);
    }
};

static bool endsWith(const std::string& s, const char* suffix) {
    const std::string x(suffix);
    return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}

// The first file of (t1)rescuepho.dlg's shape: two nodes, a response with an
// action and a condition, the portrait word and no root actions.
static std::vector<uint8_t> sampleDialog() {
    Bytes b;
    b.f64(1.75).u32(2);
    b.str("Hello.").str("Start").u32(3);
    b.str("Pay me.").str("paid").u32(1).u16(9).u16(3).u16(250).u16(3).u16(100);  // TakeMoney 250, money >= 100
    b.str("Got the zapper.").str("paid").u32(0).u16(1).u16(52);                   // has item 52
    b.str("Bye.").str(dlg::kEnd).u32(0).u16(0).u16(0);
    b.str("Thanks.").str("paid").u32(1);
    b.str("*Greyed").str(dlg::kEnd).u32(2).u16(15).u16(3).u16(11).u16(3).u16(4).u16(0).u16(0);  // trade, set quest 4
    b.u16(19).u32(0);
    return b.d;
}

static void testDialogFormat() {
    const auto d = dlg::parse(sampleDialog());
    CHECK(d.version == 1.75 && d.nodes.size() == 2 && d.portrait == 19 && d.actions.empty());
    CHECK(d.nodes[0].name == "Start" && d.nodes[0].responses.size() == 3);
    const auto& r = d.nodes[0].responses[0];
    CHECK(r.target == "paid" && r.condition == dlg::Condition::MoneyAtLeast && r.param == 100);
    CHECK(r.actions.size() == 1 && r.actions[0].type == dlg::ActionType::TakeMoney && r.actions[0].id == 250);
    const auto& t = d.nodes[1].responses[0];
    CHECK(t.actions.size() == 2 && t.actions[0].type == dlg::ActionType::LaunchTrade &&
          t.actions[1].type == dlg::ActionType::SetQuest && t.actions[1].id == 4);
    CHECK(d.find("paid") == &d.nodes[1] && d.find("nope") == nullptr);
    auto bad = sampleDialog();
    bad.push_back(0);
    CHECK(throws([&] { dlg::parse(bad); }));
    // An unknown action type.
    Bytes u;
    u.f64(1.75).u32(1).str("x").str("y").u32(1).str("r").str("y").u32(1).u16(25).u16(3).u16(0).u16(0).u16(0).u32(0);
    CHECK(throws([&] { dlg::parse(u.d); }));
    // GiveExp reads a u32, UnlockUmbilical four words at 1.75, text actions a string.
    Bytes a;
    a.u32(3).u16(4).u16(3).u32(70000).u16(2).u16(3).u16(24).u16(1).u16(1).u16(1).u16(23).u16(3).str("Hi");
    ByteReader rd(a.d, "test");
    const auto acts = dlg::readActions(rd, 1.75);
    CHECK(acts.size() == 3 && acts[0].amount == 70000);
    CHECK(acts[1].id == 24 && acts[1].gold && acts[1].entry == 1 && acts[1].announce);
    CHECK(acts[2].text == "Hi");
    CHECK(rd.remaining() == 0);
}

// A quest of the "Begin Game" shape: a pre-quest state, a state where an NPC
// talks, an end state that ends the quest; inputs on a trigger and an item.
static std::vector<uint8_t> sampleQuest() {
    Bytes b;
    b.f64(1.75).u32(3);
    b.str("Pre").u32(0).u32(0);
    b.str("Talk").u32(1).u16(0).u16(1).u16(3).u16(2).u32(1).u16(3).u16(116).u32(1).u16(3).u16(117);
    b.u32(1).u16(23).u16(3).str("Go.");
    b.str("End").u32(0).u32(1).u16(12).u16(3).u16(1);
    b.u32(2);
    b.u32(1).u16(0).u16(1).u16(2).u16(1).u16(0).u16(6).u16(1);  // trigger 1 fired on level 1
    b.u32(1).u16(1).u16(2).u16(3).u16(3).u16(9).u16(9).u16(1);  // picked item 9
    b.u32(0);                                                   // entries
    b.u32(3).u16(0).u16(1).u16(2);                              // words
    b.u32(2).u16(7).u16(50).u32(0).u16(8).u16(50).u32(1).u16(5).u16(1).u16(0);  // next: 7, or 8 unless quest 5 ended 1
    return b.d;
}

static void testQuestFormat() {
    const auto q = qst::parse(sampleQuest());
    CHECK(q.states.size() == 3 && q.inputs.size() == 2 && q.words.size() == 3 && q.next.size() == 2);
    CHECK(q.states[1].entities.size() == 1 && q.states[1].entities[0].instance == 3);
    CHECK(q.states[1].entities[0].phases.size() == 2 && q.states[1].entities[0].phases[1][0].dialog == 117);
    CHECK(q.inputs[0].kind == qst::EntityKind::Trigger && q.inputs[0].event == qst::Fired && q.inputs[0].level == 1);
    CHECK(q.inputs[1].kind == qst::EntityKind::Player && q.inputs[1].word == 9);
    CHECK(q.next[1].adjust.size() == 1 && q.next[1].adjust[0].quest == 5 && q.next[1].adjust[0].delta == 0);
    auto bad = sampleQuest();
    bad.pop_back();
    CHECK(throws([&] { qst::parse(bad); }));
}

static void testStory() {
    std::map<uint16_t, std::shared_ptr<const qst::Quest>> quests;
    quests[1] = std::make_shared<const qst::Quest>(qst::parse(sampleQuest()));
    qst::Quest seven;
    seven.states.push_back({"Seven", {}, {}});
    quests[7] = std::make_shared<const qst::Quest>(seven);
    quests[8] = quests[7];
    const auto dialog = std::make_shared<const dlg::Dialog>(dlg::parse(sampleDialog()));
    std::vector<std::string> ran;
    uint32_t money = 50;
    std::set<uint16_t> items;
    uint32_t rnd = 0;
    world::Story::Host host;
    host.quest = [&](uint16_t id) { return quests.count(id) ? quests[id] : nullptr; };
    host.dialog = [&](uint16_t id) { return id == 116 || id == 117 ? dialog : nullptr; };
    host.run = [&](const dlg::Action& a) { ran.push_back(dlg::name(a.type) + std::string(":") + std::to_string(a.id)); };
    host.hasItem = [&](uint16_t id) { return items.count(id) > 0; };
    host.money = [&] { return money; };
    host.level = [] { return uint16_t(1); };
    host.random = [&] { return rnd; };
    world::Story s(host, 0);
    s.begin(1);
    CHECK(s.questId() == 1 && s.state() == 0);
    CHECK(s.npcDialog(3, false) == 0);
    CHECK(!s.talk(3));  // nothing to say yet
    // The wrong trigger, then the right one.
    s.event(qst::EntityKind::Trigger, qst::Fired, 2);
    CHECK(s.state() == 0);
    s.event(qst::EntityKind::Trigger, qst::Fired, 1);
    CHECK(s.state() == 1 && ran.size() == 1 && ran[0] == "DisplayText:0");
    // First talk gets phase 0's dialog and moves the NPC on.
    CHECK(s.npcDialog(3, false) == 116);
    CHECK(s.talk(3));
    CHECK(s.npcDialog(3, false) == 117);
    CHECK(s.talking() && s.node()->name == "Start" && s.portrait() == 19);
    // 50 money and no item 52: only "Bye." passes its condition.
    CHECK(s.responses().size() == 1 && s.responses()[0]->text == "Bye.");
    s.close();
    money = 200;
    items.insert(52);
    CHECK(s.talk(3));
    CHECK(s.responses().size() == 3);
    s.choose(0);  // pay: TakeMoney 250 runs through the host
    CHECK(s.talking() && s.node()->name == "paid" && ran.back() == "TakeMoney:250");
    s.choose(0);  // trade, set quest 4 (missing: the quest stays), END_DIALOG
    CHECK(!s.talking() && s.questId() == 1);
    CHECK(ran[ran.size() - 1] == "LaunchTrade:0");
    // Picking up item 9 ends the quest: quest 8 is ruled out only if quest 5 ended with 1.
    rnd = 70;
    s.event(qst::EntityKind::Gizmo, qst::Picked, 9);
    CHECK(s.result(1) == std::optional<uint16_t>(1));
    CHECK(s.questId() == 8);
}

static void testRealData(const char* dir) {
    Vfs vfs(dir);
    const auto iniData = vfs.read("Harbinger.ini");
    CHECK(iniData.has_value());
    if (!iniData) return;
    const Ini ini = Ini::fromBytes(*iniData);

    size_t dialogs = 0, actions = 0;
    std::set<std::string> dialogPaths;
    for (const auto& p : vfs.list("res/"))
        if (endsWith(p, ".dlg")) {
            dialogPaths.insert(p);
            try {
                const auto d = dlg::parse(*vfs.read(p));
                ++dialogs;
                for (const auto& n : d.nodes)
                    for (const auto& r : n.responses) actions += r.actions.size();
            } catch (const std::exception& e) {
                std::fprintf(stderr, "%s: %s\n", p.c_str(), e.what());
                ++failures;
            }
        }
    std::printf("%zu dialogs parsed (%zu response actions)\n", dialogs, actions);
    CHECK(dialogs >= 1000);

    for (const char* section : {"Human", "Gladiator", "Culibine"}) {
        world::StoryFiles files(vfs, ini, section);
        const auto qIndex = mgr::parse(*vfs.read(*ini.get(section, "Quests")));
        const auto dIndex = mgr::parse(*vfs.read(*ini.get(section, "Dialog")));
        CHECK(!qIndex.entries.empty() && !dIndex.entries.empty());
        size_t quests = 0, missingDialogs = 0, npcDialogs = 0;
        for (const auto& [id, path] : qIndex.entries) {
            const auto q = files.quest(id);
            if (!q) {
                std::fprintf(stderr, "%s quest %u (%s) does not load\n", section, id, path.c_str());
                ++failures;
                continue;
            }
            ++quests;
            for (const auto& s : q->states)
                for (const auto& e : s.entities)
                    for (const auto& ph : e.phases)
                        for (const auto& c : ph) {
                            ++npcDialogs;
                            if (!files.dialog(c.dialog)) ++missingDialogs;
                        }
        }
        size_t loaded = 0;
        for (const auto& [id, path] : dIndex.entries) loaded += files.dialog(id) != nullptr;
        std::printf("%s: %zu quests, %zu of %zu dialogs load, %zu NPC dialog choices (%zu missing)\n", section, quests,
                    loaded, dIndex.entries.size(), npcDialogs, missingDialogs);
        CHECK(quests == qIndex.entries.size());
        // Holes in the shipped data: each index names (L1)Vantir Commander.dlg
        // (and the Culibine's (T2)Vantir Commander.dlg), which no archive
        // holds, and Act 2 Begin.qst names dialogs 48-50, which no index has.
        CHECK(loaded + 2 >= dIndex.entries.size());
        CHECK(missingDialogs <= 3);
    }

    // The Human's opening (Begin Game.qst): nothing to say until trigger 1
    // of Torvus Junction fires, then Torvus (instance 3) opens dialog 116,
    // and 117 after.
    world::StoryFiles files(vfs, ini, "Human");
    world::Story::Host host;
    files.attach(host);
    std::vector<std::string> ran;
    host.run = [&](const dlg::Action& a) { ran.push_back(dlg::name(a.type)); };
    host.level = [] { return uint16_t(1); };
    world::Story s(host, 0);
    s.begin(files.firstQuest());
    CHECK(files.firstQuest() == 1 && s.quest() && s.quest()->states[0].name == "Pre-Quest");
    CHECK(s.npcDialog(3, false) == 0);
    s.event(qst::EntityKind::Trigger, qst::Fired, 1);
    CHECK(s.state() == 1);
    CHECK(s.npcDialog(3, false) == 116 && s.npcDialog(5, false) == 112);
    CHECK(s.talk(3) && s.talking() && s.node() && !s.responses().empty() && s.portrait() == 19);
    std::printf("Torvus: \"%.60s...\" (%zu responses)\n", s.node()->text.c_str(), s.responses().size());
}

int main() {
    testDialogFormat();
    testQuestFormat();
    testStory();
    if (const char* dir = std::getenv("HARBINGER_DATA"))
        testRealData(dir);
    else
        std::printf("HARBINGER_DATA not set; skipping real-data checks\n");
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
