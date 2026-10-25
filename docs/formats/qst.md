# Quests (`.qst`)

A quest is a state machine (`GQuest`, a `GStateMachine`). Each state says
which dialog each NPC opens while it is current and runs entity actions when
entered; inputs move the machine to another state when something happens in
the world (a trigger fires, an agent dies, the player talks to someone or
picks something up). When a quest ends, one of the quests it names follows.
Only **one** quest is active at a time.

Parser: `src/formats/qst.cpp`. Quest logic: `src/world/story.cpp`. Python
prototype: `research/prototypes/qst_parse.py`. Tests: `tests/test_story.cpp`.
The quest log screen and its strings are plan item 1b.

The files are found through the character's `Quests` index in
`Harbinger.ini` (`[Human] Quests = res\common\managers\hquest.mgr`...). Each
character's index names 35 quests; all of them load (test).

## Layout

Little-endian; strings and action lists as in [dlg.md](dlg.md).

| Field | Type | Notes | Label | Evidence |
|---|---|---|---|---|
| version | f64 | 1.25, 1.5 or 1.75 in the files the indexes name; the two 1.0 files (`e3pass.qst`, `test.qst`) use an older layout, are named by no index, and are not read | proven | test |
| state count | u32 | | proven | `GStateMachine_v4` |
| per state: name | str | "Pre-Quest", "Char Specific 1"... | proven | `GEntityState_v3` |
| per state: entity dialog count | u32 | | proven | `FUN_0048af00` |
| per entity: kind, level, instance | 3 × u16 | kind 0 (an agent) in every file; the level.mgr id; the agent's instance in that level | proven | `FUN_0048a540` |
| per entity: phase count | u16 | | proven | |
| per phase: count, then (character, dialog) | u32, n × 2 × u16 | character 0 Human, 1 Gladiator, 2 Culibine, 3 any | proven | `FUN_0048a6c0` picks the pair for the player's character |
| per state: actions | action list | run when the state is entered | proven | |
| input count | u32 | | proven | `GMachineInput_v2` |
| per input: source states | u32 n, n × u16 | | proven | |
| per input: target state | u16 | | proven | |
| per input: monitor kind | u16 | 0 Agent, 1 Gizmo, 2 Trigger, 3 Player, 4 Group, 5 Script, 6 Kit (`FUN_004bf910` makes it) | proven | `GEntityStateInput_v2` |
| per input: instance, word, event | 3 × u16 | | proven | `GAgentMonitor_v8`, `GGroupMonitor_v8` |
| per input, kinds 0-3: level | u16 | | proven | `GAgentMonitor_v8` |
| entry count, entries | u32; u16 id, str, u32 n × (u16, u16) | what reads them is not traced (quest log entries, likely) | proven layout, likely use | `FUN_0048a8c0` |
| words | u32 n, n × u16 | 0, 1, 2 in most quests: the results it can end with (likely) | proven layout | `GQuest_v4` |
| version > 1.25: next quests | u32 n × (u16 quest, u16 weight, u32 m × (u16 quest, u16 result, i16 delta)) | | proven | `FUN_004f64d0`, `GQuestNode_v2`, `FUN_004f8eb0` |

## How a quest runs

| Finding | Label | Evidence |
|---|---|---|
| The game starts with the first quest of the character's index (the Human's quest 1 "Begin Game") | proven | `FUN_004f6a10` |
| Set Quest replaces the active quest and enters its state 0; a quest that does not load leaves the active one as it is | proven | `FUN_004f6c50` |
| Entering a state runs its actions | proven | `GEntityState` slot |
| An event (kind, code, instance) checks every input whose sources hold the current state; a matching one enters its target state | proven | `FUN_004f7260`, `FUN_004f5eb0`, `FUN_004e1e70` |
| An End Quest during that check waits until the inputs are done (+100 guard) | proven | `FUN_004f8160` |
| End Quest records (quest, result) and picks the next quest: each candidate's weight plus the deltas of the adjustments whose quest ended with that result (delta 0 rules it out), then r = random % total and the first candidate whose running sum reaches r | proven | `FUN_004f60a0`, `FUN_004f8c40`, `FUN_004f82f0` |
| An NPC's dialog is the entity entry of the current state for its level and instance: phase 0 the first time, phase 1 after | proven | `FUN_004f7220`, `FUN_0048a6c0` |
| Group monitors ask a group of agents for its state; not rebuilt (6 inputs per character use them) | proven, not rebuilt | `GGroupMonitor_v5` |

## Events

The code `FUN_004c2c50` passes on, and which monitors match it:

| Code | Fired by | Instance | Label |
|---|---|---|---|
| 0 | an agent dies (`GAgentPlug_v98`) | the agent's instance | proven |
| 2 | the player talks to an NPC (`GCulibineNPCPlug_v77`) | the NPC's instance | proven |
| 6 | an event trigger fires (`GEventTrigger_v3`); consoles, breakers and chests when used | the trigger id / gizmo instance | proven |
| 9 | an item goes into the inventory (`GInventory_v1`) | the item id | proven |

| Monitor | Matches | Label |
|---|---|---|
| Agent, Gizmo, Trigger | same kind, instance, word 0, code, and the current level | proven (`GAgentMonitor_v5`) |
| Player | word against the event's instance and event against its code, any kind and level | proven (`GPlayerMonitor_v5`) |
| Script, Kit | same kind, instance, word 0, code | proven (`GIInputMonitor_v5`) |

What the shipped quests use (inputs, all three characters alike): Trigger
fired (about 82 per character), Player talked (13-15), Player picked (14-16),
Agent died (5), Group (6). No input watches a gizmo.
