#include "world/umbilical.hpp"

#include <memory>

#include "formats/mgr.hpp"
#include "world/gizmo_state.hpp"
#include "world/level.hpp"

namespace harbinger::world {

namespace {

uint16_t u16At(const std::vector<uint8_t>& d, size_t at) {
    return at + 2 <= d.size() ? uint16_t(d[at] | d[at + 1] << 8) : 0;
}

uint16_t key(uint16_t level, uint16_t entry) { return uint16_t(level * 16 + entry); }

}  // namespace

std::optional<UmbilicalLink> umbilicalLink(const giz::Template& t, const chk::GizmoRecord& g) {
    if (t.cls != giz::Umbilical || g.extra.size() < 10) return std::nullopt;
    const auto& d = g.extra;
    return UmbilicalLink{u16At(d, 0), u16At(d, 2), u16At(d, 4), u16At(d, 6), int16_t(u16At(d, 8))};
}

EntryLookup levelEntries(Vfs& vfs, const Ini& ini) {
    struct State {
        Vfs* vfs;
        mgr::Index levels;
        std::map<uint16_t, std::vector<chk::EntryPoint>> cache;
    };
    auto st = std::make_shared<State>();
    st->vfs = &vfs;
    if (const auto data = vfs.read(ini.require("Paths", "Levels"))) st->levels = mgr::parse(*data);
    return [st](uint16_t level, uint16_t entry) -> std::optional<EntryInfo> {
        auto it = st->cache.find(level);
        if (it == st->cache.end()) {
            std::vector<chk::EntryPoint> entries;
            if (const std::string* path = st->levels.find(level))
                if (const auto data = st->vfs->read(*path)) {
                    try {
                        entries = chk::parse(*data).entries;
                    } catch (const std::exception&) {
                        // "AddUmbilical() Load failed!!!": no code.
                    }
                }
            it = st->cache.emplace(level, std::move(entries)).first;
        }
        for (const auto& e : it->second)
            if (e.id == entry) return EntryInfo{e.name, e.closed};
        return std::nullopt;
    };
}

UmbilicalCodes::Code* UmbilicalCodes::add(uint16_t level, uint16_t entry, uint16_t flags, const EntryLookup& lookup) {
    if (level == 0 || entry == 0) return nullptr;
    const auto info = lookup ? lookup(level, entry) : std::nullopt;
    if (!info) return nullptr;
    // FUN_004ee9d0 with its last argument 0 (not loading a save): Open
    // comes from the entry point alone.
    flags = uint16_t(flags & ~Open);
    if (!info->closed) flags |= Open;
    Code& c = codes_[key(level, entry)];
    c = Code{level, entry, flags, info->name};
    return &c;
}

bool UmbilicalCodes::unlock(uint16_t level, uint16_t entry, bool gold, const EntryLookup& lookup) {
    if (auto it = codes_.find(key(level, entry)); it != codes_.end()) {
        const uint16_t was = it->second.flags;
        it->second.flags |= Held;
        if (gold) it->second.flags &= uint16_t(~Plain);
        return !(was & Held);
    }
    Code* c = add(level, entry, Held, lookup);
    if (c && !gold) c->flags |= Plain;
    return true;
}

void UmbilicalCodes::setOpen(uint16_t level, uint16_t entry, bool open, const EntryLookup& lookup) {
    Code* c = nullptr;
    if (auto it = codes_.find(key(level, entry)); it != codes_.end()) c = &it->second;
    else c = add(level, entry, 0, lookup);
    if (!c) return;
    if (open) c->flags |= Open;
    else c->flags &= uint16_t(~Open);
}

void UmbilicalCodes::markHere(uint16_t level, uint16_t entry, const EntryLookup& lookup) {
    if (auto it = codes_.find(key(level, entry)); it != codes_.end()) it->second.flags |= Held | Plain;
    else add(level, entry, Held | Plain, lookup);
}

bool UmbilicalCodes::any() const {
    for (const auto& [k, c] : codes_)
        if (c.flags & Held) return true;
    return false;
}

std::vector<UmbilicalCodes::Code> UmbilicalCodes::destinations(uint16_t level, uint16_t entry) const {
    std::vector<Code> gold, plain;
    for (const auto& [k, c] : codes_) {
        if (k == key(level, entry) || !(c.flags & Held) || !(c.flags & Open)) continue;
        (c.gold() ? gold : plain).push_back(c);
    }
    gold.insert(gold.end(), plain.begin(), plain.end());
    return gold;
}

std::string UmbilicalCodes::listText(const std::vector<Code>& codes) {
    std::string text;
    for (const auto& c : codes) {
        if (c.gold()) text += '*';
        text += c.name;
        text += '\n';
    }
    return text;
}

Umbilicals::Umbilicals(Level& level) : level_(level) {}

bool Umbilicals::trigger(const chk::GizmoRecord& pad, bool codesHeld, uint64_t nowMs) {
    const giz::Template& t = level_.gizmoTemplate(pad);
    const auto link = umbilicalLink(t, pad);
    if (!link || pad_) return false;
    if (!codesHeld && !link->fixed()) return false;
    const GizmoState& st = level_.gizmoState(pad);
    if (st.state != 0 || st.power < st.needed) return false;
    level_.setGizmoState(pad, 2, nowMs);
    pad_ = &pad;
    link_ = *link;
    padEnd_ = nowMs + playLength(t, level_.gizmoPlay(pad));
    charged_ = closed_ = false;
    // FUN_004bdaf0: the attached shell (GUmbilicalShellPlug_v1) closes if
    // it is open. Every shipped pad has exactly one.
    shell_ = nullptr;
    for (const auto& [id, list] : level_.map().attachments) {
        if (id != pad.instance) continue;
        for (uint16_t other : list)
            if (const chk::GizmoRecord* o = level_.gizmoByInstance(other);
                o && level_.gizmoTemplate(*o).cls == giz::UmbilicalShell && level_.gizmoState(*o).state == 0) {
                level_.setGizmoState(*o, 2, nowMs);
                shell_ = o;
                shellEnd_ = nowMs + playLength(level_.gizmoTemplate(*o), level_.gizmoPlay(*o));
                break;
            }
    }
    return true;
}

std::vector<Umbilicals::Event> Umbilicals::update(uint64_t nowMs) {
    std::vector<Event> out;
    if (!pad_) return out;
    // GPortalPlug_v29 (the pad's slot 29): at the end of the charge the pad
    // holds its last frame and, started by a trigger, gives the code of a
    // fixed destination (FUN_004a1a20).
    if (!charged_ && nowMs >= padEnd_) {
        charged_ = true;
        out.push_back({Event::Charged, pad_, link_});
    }
    // GUmbilicalShellPlug_v29, first call: the closed shell holds and the
    // list opens (FUN_004a1bb0). A pad without a shell goes straight on.
    if (!closed_ && (!shell_ || nowMs >= shellEnd_) && charged_) {
        closed_ = true;
        out.push_back({Event::Closed, pad_, link_});
    }
    return out;
}

void Umbilicals::release(uint64_t nowMs) {
    if (shell_) level_.setGizmoState(*shell_, 0, nowMs);
    if (pad_) level_.setGizmoState(*pad_, 0, nowMs);
    pad_ = shell_ = nullptr;
}

}  // namespace harbinger::world
