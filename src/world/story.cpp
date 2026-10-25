#include "world/story.hpp"

#include <algorithm>

namespace harbinger::world {

Story::Story(Host host, uint16_t character) : host_(std::move(host)), character_(character) {}

void Story::begin(uint16_t quest) { setQuest(quest); }

void Story::setQuest(uint16_t id) {
    // FUN_004f6c50: a quest that does not load leaves the active one as it is.
    auto q = host_.quest ? host_.quest(id) : nullptr;
    if (!q || q->states.empty()) {
        say("quest " + std::to_string(id) + " missing");
        return;
    }
    quest_ = std::move(q);
    questId_ = id;
    phases_.clear();
    say("quest " + std::to_string(id));
    enter(0);
}

void Story::enter(size_t state) {
    if (!quest_ || state >= quest_->states.size()) return;
    const auto keep = quest_;  // an action may replace the quest
    state_ = state;
    say("  state " + std::to_string(state) + " " + keep->states[state].name);
    runActions(keep->states[state].actions);
}

std::optional<uint16_t> Story::result(uint16_t quest) const {
    // FUN_004f82f0 looks the quest up in the finished map; the latest wins.
    for (auto it = ended_.rbegin(); it != ended_.rend(); ++it)
        if (it->first == quest) return it->second;
    return std::nullopt;
}

void Story::endQuest(uint16_t result) {
    if (dispatching_) {  // FUN_004f8160 with +100 == 1: after the inputs
        pendingEnd_ = result;
        return;
    }
    if (!quest_) return;
    ended_.emplace_back(questId_, result);
    say("end quest " + std::to_string(questId_) + " result " + std::to_string(result));
    // FUN_004f60a0: each following quest's weight, adjusted by how earlier
    // quests ended (FUN_004f8c40: a delta of 0 rules it out), then a pick
    // weighted by them.
    struct Candidate {
        uint16_t quest;
        uint32_t weight;
    };
    std::vector<Candidate> pool;
    uint32_t total = 0;
    for (const auto& n : quest_->next) {
        int w = n.weight;
        bool out = false;
        for (const auto& a : n.adjust) {
            if (this->result(a.quest).value_or(0) != a.result) continue;
            if (a.delta == 0) {
                out = true;
                break;
            }
            w = std::clamp(w + a.delta, 0, 0xffff);
        }
        if (out || w == 0) continue;
        pool.push_back({n.quest, uint32_t(w)});
        total += uint32_t(w);
    }
    uint16_t next = 0;
    if (total) {
        const uint32_t r = (host_.random ? host_.random() : 0) % total;
        uint32_t sum = 0;
        next = pool.back().quest;
        for (const auto& c : pool) {
            sum += c.weight;
            if (r <= sum) {
                next = c.quest;
                break;
            }
        }
    }
    questEnded_ = true;  // +0x31
    setQuest(next);
}

void Story::runActions(const std::vector<dlg::Action>& actions) {
    for (const auto& a : actions) runAction(a);
}

void Story::runAction(const dlg::Action& a) {
    // FUN_00489280: each action whose character matches (slot 9), then its slot 8.
    if (!a.forCharacter(character_)) return;
    say("  action " + std::string(dlg::name(a.type)) + " " + std::to_string(a.id) +
        (a.text.empty() ? "" : " \"" + a.text.substr(0, 40) + "\""));
    switch (a.type) {
        case dlg::ActionType::SetQuest: setQuest(a.id); return;
        case dlg::ActionType::EndQuest: endQuest(a.id); return;
        case dlg::ActionType::SetDialog: openDialog(a.id); return;
        case dlg::ActionType::PopDialogGroup: pop_ = true; return;
        case dlg::ActionType::QuestString:
            // FUN_004f7340 adds the line; the host shows "Quest Log updated."
            log_.push_back(a.text);
            break;
        default: break;
    }
    if (host_.run) host_.run(a);
}

void Story::event(qst::EntityKind kind, uint16_t code, uint16_t instance) {
    if (!quest_) return;
    const auto keep = quest_;
    const uint16_t level = host_.level ? host_.level() : 0;
    ++dispatching_;
    // FUN_004f5eb0: every input whose source states hold the current one
    // (FUN_004e1e70) and whose monitor matches (slot 5) moves the machine.
    for (const auto& in : keep->inputs) {
        if (std::find(in.from.begin(), in.from.end(), uint16_t(state_)) == in.from.end()) continue;
        bool match = false;
        switch (in.kind) {
            case qst::EntityKind::Agent: case qst::EntityKind::Gizmo: case qst::EntityKind::Trigger:
                // GAgentMonitor_v5 (the event's last word is always 0).
                match = kind == in.kind && in.instance == instance && in.word == 0 && in.event == code &&
                        in.level == level;
                break;
            case qst::EntityKind::Player:
                // GPlayerMonitor_v5: any entity kind and level.
                match = in.word == instance && in.event == code;
                break;
            case qst::EntityKind::Script: case qst::EntityKind::Kit:
                // GIInputMonitor_v5.
                match = kind == in.kind && in.instance == instance && in.word == 0 && in.event == code;
                break;
            case qst::EntityKind::Group:
                // GGroupMonitor_v5 asks a group of agents (+0xc) for its
                // state; groups are not rebuilt.
                break;
        }
        if (!match) continue;
        say("  input -> state " + std::to_string(in.to));
        enter(in.to);
        if (quest_ != keep) break;  // the quest changed under us
    }
    --dispatching_;
    if (!dispatching_ && pendingEnd_) {
        const uint16_t r = *pendingEnd_;
        pendingEnd_.reset();
        endQuest(r);
    }
    settle();
}

uint16_t Story::npcDialog(uint16_t instance, bool advance) {
    if (!quest_ || state_ >= quest_->states.size()) return 0;
    const uint16_t level = host_.level ? host_.level() : 0;
    const auto& entities = quest_->states[state_].entities;
    for (size_t i = 0; i < entities.size(); ++i) {
        const auto& e = entities[i];
        if (e.kind != 0 || e.level != level || e.instance != instance) continue;
        uint8_t& phase = phases_[{state_, i}];
        if (phase >= e.phases.size()) continue;
        for (const auto& c : e.phases[phase]) {
            if (c.character != dlg::kAnyCharacter && c.character != character_) continue;
            if (phase == 0 && advance) phase = 1;
            return c.dialog;
        }
    }
    return 0;
}

bool Story::talk(uint16_t instance) {
    const uint16_t id = npcDialog(instance, true);
    say("talk to " + std::to_string(instance) + (id ? "" : ": nothing to say"));
    const bool opened = id && openDialog(id, instance);
    // GCulibineNPCPlug_v77, as the talk action runs: the Talked event.
    event(qst::EntityKind::Agent, qst::Talked, instance);
    return opened && talking();
}

bool Story::openDialog(uint16_t id, std::optional<uint16_t> npc) {
    auto d = id && host_.dialog ? host_.dialog(id) : nullptr;
    npc_ = npc;  // FUN_004f7ca0 sets +0x68 whatever it opens
    if (!d || d->nodes.empty()) {
        say("dialog " + std::to_string(id) + " missing");
        return false;
    }
    say("dialog " + std::to_string(id));
    Open o;
    o.dialog = std::move(d);
    o.node = &o.dialog->nodes.front();  // FUN_0047ec70
    o.id = id;
    stack_.push_back(std::move(o));
    runActions(stack_.back().dialog->actions);
    settle();
    return true;
}

bool Story::talking() const { return !stack_.empty() && stack_.back().node; }

const dlg::Node* Story::node() const { return talking() ? stack_.back().node : nullptr; }

uint16_t Story::portrait() const { return talking() ? stack_.back().dialog->portrait : 0; }

void Story::refresh() {
    offered_.clear();
    const dlg::Node* n = node();
    if (!n) return;
    const uint32_t money = host_.money ? host_.money() : 0;
    for (const auto& r : n->responses) {
        if (offered_.size() == dlg::kMaxResponses) break;
        bool ok = true;
        switch (r.condition) {
            case dlg::Condition::None: break;
            case dlg::Condition::HasItem: ok = host_.hasItem && host_.hasItem(r.param); break;
            case dlg::Condition::LacksItem: ok = !(host_.hasItem && host_.hasItem(r.param)); break;
            case dlg::Condition::MoneyAtLeast: ok = money >= r.param; break;
            case dlg::Condition::MoneyBelow: ok = money < r.param; break;
        }
        if (ok) offered_.push_back(&r);
    }
}

void Story::settle() {
    // FUN_004f7f40: a PopDialogGroup drops the top dialog (never the last);
    // a stack whose top has no node left is the end of the conversation.
    if (pop_) {
        pop_ = false;
        if (stack_.size() > 1) stack_.pop_back();
    }
    if (!stack_.empty() && !stack_.back().node) {
        stack_.clear();
        npc_.reset();
    }
    refresh();
    ++revision_;
}

void Story::choose(size_t i) {
    if (!talking() || i >= offered_.size()) return;
    questEnded_ = false;
    // FUN_0047ee70: leave the node, run the response's actions, go to the
    // node its target names in the same dialog (none: the dialog is done).
    const dlg::Response r = *offered_[i];
    const auto dialog = stack_.back().dialog;
    const size_t at = stack_.size() - 1;
    stack_[at].node = nullptr;
    say("choose \"" + r.text.substr(0, 40) + "\" -> " + r.target);
    runActions(r.actions);
    if (at < stack_.size() && stack_[at].dialog == dialog) stack_[at].node = dialog->find(r.target);
    // FUN_004f8000: a quest that ended while the conversation ends hands
    // the NPC on to the next quest's dialog straight away.
    if (questEnded_ && !stack_.empty() && !stack_.back().node) {
        const auto npc = npc_;
        stack_.clear();
        npc_.reset();
        if (npc)
            if (const uint16_t id = npcDialog(*npc, true)) openDialog(id, npc);
    }
    questEnded_ = false;
    settle();
}

void Story::close() {
    stack_.clear();
    npc_.reset();
    pop_ = false;
    refresh();
    ++revision_;
}

namespace {

mgr::Index readIndex(Vfs& vfs, const Ini& ini, const std::string& section, const char* key) {
    const auto path = ini.get(section, key);
    if (!path) return {};
    const auto data = vfs.read(*path);
    if (!data) return {};
    try {
        return mgr::parse(*data);
    } catch (const std::exception&) {
        return {};
    }
}

template <class T, class Parse>
std::shared_ptr<const T> load(Vfs& vfs, const mgr::Index& index, std::map<uint16_t, std::shared_ptr<const T>>& cache,
                              uint16_t id, Parse parse) {
    if (const auto it = cache.find(id); it != cache.end()) return it->second;
    std::shared_ptr<const T> out;
    if (const std::string* path = index.find(id))
        if (const auto data = vfs.read(*path)) try {
                out = std::make_shared<const T>(parse(*data));
            } catch (const std::exception&) {
            }
    cache[id] = out;
    return out;
}

}  // namespace

StoryFiles::StoryFiles(Vfs& vfs, const Ini& ini, const std::string& section)
    : vfs_(vfs), quests_(readIndex(vfs, ini, section, "Quests")), dialogs_(readIndex(vfs, ini, section, "Dialog")) {}

std::shared_ptr<const qst::Quest> StoryFiles::quest(uint16_t id) {
    return load<qst::Quest>(vfs_, quests_, questCache_, id, qst::parse);
}

std::shared_ptr<const dlg::Dialog> StoryFiles::dialog(uint16_t id) {
    return load<dlg::Dialog>(vfs_, dialogs_, dialogCache_, id, dlg::parse);
}

void StoryFiles::attach(Story::Host& host) {
    host.quest = [this](uint16_t id) { return quest(id); };
    host.dialog = [this](uint16_t id) { return dialog(id); };
}

}  // namespace harbinger::world
