// Quests (.qst): each one a state machine (GQuest, a GStateMachine) whose
// states say which dialog each NPC opens and run actions on entry, and
// whose inputs move it on when the player does something in the world.
// Format notes and evidence: docs/formats/qst.md
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "formats/dlg.hpp"

namespace harbinger::qst {

// The entity kinds a monitor watches and an event names (each monitor
// class's slot 1).
enum class EntityKind : uint16_t { Agent = 0, Gizmo = 1, Trigger = 2, Player = 3, Group = 4, Script = 5, Kit = 6 };

// What happened to an entity (the code FUN_004c2c50 passes on). Named
// from the places that fire them.
enum Event : uint16_t {
    Died = 0,          // GAgentPlug_v98 (an agent's death)
    Talked = 2,        // GCulibineNPCPlug_v76/_v77 (the player talked to an NPC)
    Fired = 6,         // GEventTrigger_v3, and consoles, breakers and chests when used
    Picked = 9,        // GInventory_v1: an item went into the inventory; the instance is the item id
};

// One NPC's dialogs while a state is current (FUN_0048a540): the agent is
// named by its level and its instance there. A phase holds (character,
// dialog id) pairs; phase 0 is the first conversation, phase 1 every one
// after (FUN_0048a6c0).
struct EntityDialogs {
    uint16_t kind = 0;      // +4: 0, an agent, in every shipped file
    uint16_t level = 0;     // +8: level.mgr id
    uint16_t instance = 0;  // +10: the agent record's instance
    struct Choice {
        uint16_t character = dlg::kAnyCharacter;  // 0 Human, 1 Gladiator, 2 Culibine, 3 any
        uint16_t dialog = 0;  // id in the character's [Paths] Dialog index
    };
    std::vector<std::vector<Choice>> phases;
};

// GEntityState: a named state, the NPC dialogs that hold while it is
// current, and the actions it runs when entered.
struct State {
    std::string name;
    std::vector<EntityDialogs> entities;
    std::vector<dlg::Action> actions;
};

// GEntityStateInput: moves the machine from any of `from` to `to` when its
// monitor sees a matching event. The monitor's words (GAgentMonitor_v8 /
// GGroupMonitor_v8) and how each kind matches an event (slot 5):
//   Agent, Gizmo, Trigger: instance (+4), word (+6), event (+8), and level
//     (+0xc) must all match, the level being the current one
//   Player: +6 against the event's instance and +8 against its code, any
//     entity kind, any level (GPlayerMonitor_v5)
//   Group, Script, Kit: instance, word and event, no level
struct Input {
    std::vector<uint16_t> from;
    uint16_t to = 0;
    EntityKind kind = EntityKind::Agent;
    uint16_t instance = 0, word = 0, event = 0;
    uint16_t level = 0;   // Agent, Gizmo, Trigger and Player monitors only
};

// FUN_0048a8c0: an id, a text and (u16, u16) pairs; what reads it is not
// traced (quest log entries, likely).
struct Entry {
    uint16_t id = 0;
    std::string text;
    std::vector<std::pair<uint16_t, uint16_t>> pairs;
};

// GQuestNode: a quest that may follow this one when it ends
// (FUN_004f60a0): its weight, adjusted by how earlier quests ended.
struct Next {
    uint16_t quest = 0;   // quest.mgr id
    uint16_t weight = 0;
    struct Adjust {
        uint16_t quest = 0;    // an ended quest
        uint16_t result = 0;   // the result it ended with (EndQuest's word)
        int16_t delta = 0;     // added to the weight; 0 rules this quest out
    };
    std::vector<Adjust> adjust;
};

struct Quest {
    double version = 0;   // 1.25, 1.5 or 1.75 in the files the indexes name
    std::vector<State> states;   // state 0 is where a quest starts
    std::vector<Input> inputs;
    std::vector<Entry> entries;  // GQuest+0x28
    std::vector<uint16_t> words; // GQuest+0x40 (0, 1, 2 in most: the results it can end with, likely)
    std::vector<Next> next;      // from version 1.25 (+0x34)
};

// Throws std::runtime_error on malformed data or trailing bytes. Version
// 1.0 files (e3pass.qst, test.qst, which no index names) use an older
// layout and fail.
Quest parse(const std::vector<uint8_t>& data);

}  // namespace harbinger::qst
