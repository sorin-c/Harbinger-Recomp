// The story: the one quest that is active (GQuestMgr's current GQuest, a
// state machine), which dialog each NPC opens, the conversation on screen
// (a stack of dialogs, each at one node), and the entity actions both of
// them fire. Free of drawing and of the world: what the actions do and
// what the responses' conditions ask about the player comes through Host.
// Notes and evidence: docs/formats/qst.md, docs/formats/dlg.md, docs/dialogue.md
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/dlg.hpp"
#include "formats/mgr.hpp"
#include "formats/qst.hpp"

namespace harbinger::world {

class Story {
public:
    struct Host {
        // The quest and dialog files by id (the character's [Paths] Quests
        // and Dialog indexes); nullptr when missing.
        std::function<std::shared_ptr<const qst::Quest>(uint16_t id)> quest;
        std::function<std::shared_ptr<const dlg::Dialog>(uint16_t id)> dialog;
        // Runs one action that is not the story's own (items, money,
        // experience, scripts, gizmos, messages...).
        std::function<void(const dlg::Action&)> run;
        // For the responses' conditions.
        std::function<bool(uint16_t item)> hasItem;
        std::function<uint32_t()> money;
        // The current level (FUN_004a5300), for entity dialogs and monitors.
        std::function<uint16_t()> level;
        // A random number for picking the next quest (FUN_004f9050).
        std::function<uint32_t()> random;
    };

    // `character`: 0 Human, 1 Gladiator, 2 Culibine (DAT_0058437c+0x5848c).
    Story(Host host, uint16_t character);
    // A new level's world takes over the story (it is carried along).
    void setHost(Host host) { host_ = std::move(host); }
    bool begun() const { return quest_ != nullptr; }

    // FUN_004f6a10: starts the game at `quest`, the first entry of the
    // quest index.
    void begin(uint16_t quest);
    // GSetQuestEntityAction: makes `quest` the active one, in its state 0.
    void setQuest(uint16_t quest);
    // GEndQuestEntityAction (FUN_004f8160): the active quest ends with
    // `result`; it is recorded and one of the quests it names follows
    // (FUN_004f60a0's weighted pick).
    void endQuest(uint16_t result);
    uint16_t questId() const { return questId_; }
    const qst::Quest* quest() const { return quest_.get(); }
    size_t state() const { return state_; }
    // How an ended quest ended (FUN_004f82f0); nullopt if it has not.
    std::optional<uint16_t> result(uint16_t quest) const;
    // The quest log lines QuestString actions added, oldest first.
    const std::vector<std::string>& log() const { return log_; }

    // FUN_004f7260 / FUN_004f5eb0: something happened in the world.
    // `instance` is the entity's (an agent or gizmo instance, a trigger id,
    // an item id for Picked).
    void event(qst::EntityKind kind, uint16_t code, uint16_t instance);

    // FUN_004f7220 / FUN_0048a6c0: the dialog the NPC with this level
    // instance opens now, 0 for none. With `advance` a first conversation
    // moves that NPC on to its later dialogs (phase 1).
    uint16_t npcDialog(uint16_t instance, bool advance);

    // GCulibineNPCPlug_v76: the player talks to an NPC. Opens its dialog and
    // returns true, or fires the Talked event and returns false when it has
    // nothing to say.
    bool talk(uint16_t instance);
    // FUN_004f7ca0: opens dialog `id` on top of the conversation (0: the
    // quest index's default dialog is not used by NPCs, so nothing).
    bool openDialog(uint16_t id, std::optional<uint16_t> npc = {});

    // The conversation. Open while a dialog on the stack is at a node.
    bool talking() const;
    std::optional<uint16_t> npc() const { return talking() ? npc_ : std::nullopt; }
    // What the NPC says now, and the responses offered (FUN_0047ea50: the
    // first kMaxResponses whose condition holds).
    const dlg::Node* node() const;
    const std::vector<const dlg::Response*>& responses() const { return offered_; }
    // The portrait beside the words (the top dialog's, FUN_004f7f40 ->
    // FUN_0049f710): an item.mgr id, 0 for none.
    uint16_t portrait() const;
    // FUN_004f8000: the player picks offered response `i`: its actions run,
    // then the conversation goes to its target node, or ends.
    void choose(size_t i);
    // Ends the conversation without a response (the close button).
    void close();
    // Bumped whenever the conversation's node changes (for the screen).
    uint64_t revision() const { return revision_; }

    // What the story did, for tests and HARBINGER_STORY_LOG.
    std::function<void(const std::string&)> trace;

private:
    struct Open {
        std::shared_ptr<const dlg::Dialog> dialog;
        const dlg::Node* node = nullptr;
        uint16_t id = 0;
    };
    void enter(size_t state);
    void runActions(const std::vector<dlg::Action>& actions);
    void runAction(const dlg::Action& a);
    void refresh();
    void settle();
    void say(const std::string& s) {
        if (trace) trace(s);
    }

    Host host_;
    uint16_t character_;
    std::shared_ptr<const qst::Quest> quest_;
    uint16_t questId_ = 0;
    size_t state_ = 0;
    // Per state of the active quest, per entity entry: its phase (the int
    // at the start of FUN_0048a540's object).
    std::map<std::pair<size_t, size_t>, uint8_t> phases_;
    std::vector<std::pair<uint16_t, uint16_t>> ended_;  // (quest, result), in order
    std::vector<std::string> log_;

    std::vector<Open> stack_;   // DAT_005846a4+0x5c, top last
    std::optional<uint16_t> npc_;  // +0x68
    std::vector<const dlg::Response*> offered_;
    bool pop_ = false;          // +0x30, PopDialogGroup
    bool questEnded_ = false;   // +0x31
    // FUN_004f7260's guard (+100): an EndQuest while inputs are being
    // checked waits until they are.
    int dispatching_ = 0;
    std::optional<uint16_t> pendingEnd_;
    uint64_t revision_ = 0;
};

// The character's quest and dialog files ([Human|Gladiator|Culibine]
// Quests and Dialog in Harbinger.ini), read on first use and kept.
class StoryFiles {
public:
    // `section` is the character's Harbinger.ini section.
    StoryFiles(Vfs& vfs, const Ini& ini, const std::string& section);
    std::shared_ptr<const qst::Quest> quest(uint16_t id);
    std::shared_ptr<const dlg::Dialog> dialog(uint16_t id);
    // FUN_004f6a10: the game starts with the quest index's first entry.
    uint16_t firstQuest() const { return quests_.entries.empty() ? 0 : quests_.entries.front().first; }
    // Fills a Story::Host's quest and dialog loaders.
    void attach(Story::Host& host);

private:
    Vfs& vfs_;
    mgr::Index quests_, dialogs_;
    std::map<uint16_t, std::shared_ptr<const qst::Quest>> questCache_;
    std::map<uint16_t, std::shared_ptr<const dlg::Dialog>> dialogCache_;
};

}  // namespace harbinger::world
