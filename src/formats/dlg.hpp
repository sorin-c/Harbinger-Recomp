// NPC dialogues (.dlg) and the entity actions that dialogues and quests
// fire. Format notes and evidence: docs/formats/dlg.md
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace harbinger {
class ByteReader;
}

namespace harbinger::dlg {

// The entity action types: the id each class's slot 2 returns, which the
// action factory (DAT_005840c8, FUN_0048a400) is keyed by.
enum class ActionType : uint16_t {
    GiveItem = 0, TakeItem = 1, UnlockUmbilical = 2, ClearUmbilicals = 3, GiveExp = 4, LaunchScript = 5,
    UnlockGizmo = 6, ActivateGizmo = 7, GiveMoney = 8, TakeMoney = 9, GiveHealth = 10, SetQuest = 11,
    EndQuest = 12, QuestString = 13, SetDialog = 14, LaunchTrade = 15, StatMod = 16, AddItemToAccount = 17,
    TakeItemFromAccount = 18, PrevQuestDialog = 19, NextQuestDialog = 20, PopDialogGroup = 21, SetLevel = 22,
    DisplayText = 23, ClearAccount = 24,
    Count,
};
const char* name(ActionType t);

// The character an action is for (GIEntityAction+4): it only runs when
// this matches the player's character (DAT_0058437c+0x5848c), or is Any
// (GAddItemToAccountEntityAction_v9). Every shipped action says Any.
constexpr uint16_t kAnyCharacter = 3;

// One entity action as the file stores it. Which of the fields a type
// reads (each class's slot 5):
//   id only: GiveItem, TakeItem, LaunchScript, UnlockGizmo, ActivateGizmo,
//     GiveMoney, TakeMoney, GiveHealth, SetQuest, EndQuest, SetDialog,
//     SetLevel, ClearAccount (u16 at +8)
//   nothing: ClearUmbilicals, LaunchTrade, PrevQuestDialog, NextQuestDialog,
//     PopDialogGroup
//   id and a second word: StatMod (+8, +0xa), AddItemToAccount and
//     TakeItemFromAccount (+8, +0xc)
//   text: QuestString, DisplayText
//   GiveExp: a u32 amount
//   UnlockUmbilical: level (+8), then from version 1.0 a gold flag (+0xc)
//     and an entry point (+0xe), from 1.5 an announce flag (+0xd)
struct Action {
    ActionType type = ActionType::Count;
    uint16_t character = kAnyCharacter;
    uint16_t id = 0;       // the first word (or GiveExp's amount, cut to 16 bits: see `amount`)
    uint16_t value = 0;    // the second word
    uint32_t amount = 0;   // GiveExp
    std::string text;      // QuestString, DisplayText
    // UnlockUmbilical
    bool gold = false, announce = false;
    uint16_t entry = 0;

    bool forCharacter(uint16_t c) const { return character == kAnyCharacter || character == c; }
};

// FUN_00489320: u32 count, then per action a u16 type and the type's own
// fields. `version` is the file's (some types read more in later versions).
std::vector<Action> readActions(ByteReader& r, double version);
// The engine's 8-byte double, as every versioned file starts.
double readF64(ByteReader& r);

// What must hold for a response to be offered (FUN_0047ea50): the player
// carries an item (FUN_004cf9e0 on the inventory at player+0x90) or not,
// or has at least / less than some money (player+0x24).
enum class Condition : uint16_t { None = 0, HasItem = 1, LacksItem = 2, MoneyAtLeast = 3, MoneyBelow = 4 };

// GDialogResponse: a line the player can say, where it leads, and what it
// does when picked.
struct Response {
    std::string text;      // a leading '*' shows it greyed (GCtrlTextBtn_v7, state 3)
    std::string target;    // the node to go to, or kEnd
    std::vector<Action> actions;
    Condition condition = Condition::None;  // from version 1.0 (+0x1c)
    uint16_t param = 0;    // the item id or the amount (+0x20)
};

// GDialog (the engine's name for one node): what the NPC says, and the
// player's responses.
struct Node {
    std::string text;
    std::string name;      // responses name their target by it
    std::vector<Response> responses;
};

// The response target that ends the conversation.
inline constexpr const char* kEnd = "END_DIALOG";
// FUN_0047ea50 offers at most four responses, the first four that pass.
constexpr size_t kMaxResponses = 4;

// A whole file (the engine's GDialogGroup): its nodes, the first being
// where the conversation starts, then from version 1.0 the portrait shown
// beside the NPC's words (+0x14: an item.mgr id, Torvus.itm, Ona.itm...,
// which FUN_0049f710 makes and hands to the portrait slot) and actions
// (none in the shipped files).
struct Dialog {
    double version = 0;    // 1.25 or 1.75 in the shipped files
    std::vector<Node> nodes;
    uint16_t portrait = 0; // item.mgr id, 0 = none
    std::vector<Action> actions;

    // The node named `name`, or nullptr.
    const Node* find(const std::string& name) const;
};

// Throws std::runtime_error on malformed data, unknown action types or
// trailing bytes.
Dialog parse(const std::vector<uint8_t>& data);

}  // namespace harbinger::dlg
