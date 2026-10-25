// Umbilicals: the pads that send the player between levels, the codes that
// say where they may go, and the pad and shell playing out a trip. Notes and
// evidence: docs/umbilicals.md.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/ini.hpp"
#include "engine/vfs.hpp"
#include "formats/chk.hpp"
#include "formats/giz.hpp"

namespace harbinger::world {

class Level;

// An umbilical pad's record (GUmbilicalPlug_v22): u16 state, u16 level,
// u16 entry, u16 own entry, u16 power needed.
struct UmbilicalLink {
    uint16_t state = 0;
    uint16_t level = 0;   // plug+0xf4: a fixed destination's level.mgr id (0 in most)
    uint16_t entry = 0;   // plug+0xf6: and its entry point
    uint16_t own = 0;     // plug+0xf8: the entry point in this level on the pad
    int16_t needed = 0;   // plug+0xde: power needed (GIPowerable)
    bool fixed() const { return level != 0 && entry != 0; }
};
std::optional<UmbilicalLink> umbilicalLink(const giz::Template& t, const chk::GizmoRecord& g);

// The name an entry point shows in the umbilical list, and whether its
// umbilical starts closed: a facing word above 15 marks it (FUN_0048b2c0
// clears bit 4 and keeps the mark at entry+0x2c). No shipped entry an
// umbilical arrives at has the mark.
struct EntryInfo {
    std::string name;
    bool closed = false;
};
// Looks up entry point `entry` of level `level` (FUN_004ee9d0 opens the
// level file for it); empty if either is missing.
using EntryLookup = std::function<std::optional<EntryInfo>(uint16_t level, uint16_t entry)>;
// The lookup over the game's level files, caching each level's entries.
EntryLookup levelEntries(Vfs& vfs, const Ini& ini);

// The player's umbilical codes (GPlayerPlug+0x220): a map keyed by
// level * 16 + entry, each with flags and the entry point's name.
class UmbilicalCodes {
public:
    enum Flag : uint16_t {
        Held = 1,   // the player has the code
        Open = 2,   // the umbilical there can be reached (power, entry mark)
        Plain = 4,  // been there, or given without the gold hint
    };
    struct Code {
        uint16_t level = 0, entry = 0;
        uint16_t flags = 0;
        std::string name;
        bool gold() const { return !(flags & Plain); }
    };

    // Unlock Umbilical (GUnlockUmbEntityAction_v8 -> FUN_004ef160): the
    // player gets the code. `gold` marks it as a hint (and makes a known
    // code gold again); a new code gets Open unless the entry is marked
    // closed. Returns true if the player did not hold it before (the
    // action then says "Player received umbilical code.").
    bool unlock(uint16_t level, uint16_t entry, bool gold, const EntryLookup& lookup);
    // Clear Umbilicals (GClearUmbilicalsAction_v8): every code goes.
    void clear() { codes_.clear(); }
    // FUN_004ef270 / FUN_004ef330: the umbilical at (level, entry) gained or
    // lost the power it needs (adding the place, not held, if unknown).
    void setOpen(uint16_t level, uint16_t entry, bool open, const EntryLookup& lookup);
    // FUN_004eeeb0, as the shell closes: the pad the player stands on is
    // known and no longer gold.
    void markHere(uint16_t level, uint16_t entry, const EntryLookup& lookup);
    // FUN_004ef3f0: whether the player holds any code (a pad without a
    // fixed destination only starts then).
    bool any() const;

    // The places the list offers from (level, entry), in its order
    // (FUN_004eeeb0 builds the text, FUN_004ef030 maps a picked line back):
    // held and open codes other than here, the gold ones first, each group
    // in key order.
    std::vector<Code> destinations(uint16_t level, uint16_t entry) const;
    // The list's text: one name a line, gold names starting with '*' (the
    // text box draws those with its third font).
    static std::string listText(const std::vector<Code>& codes);

    const std::map<uint16_t, Code>& all() const { return codes_; }

private:
    // FUN_004ee9d0 (AddUmbilical): adds (level, entry) with `flags` (Open
    // set unless the entry is marked closed); nothing if level or entry is
    // 0 or the entry does not exist.
    Code* add(uint16_t level, uint16_t entry, uint16_t flags, const EntryLookup& lookup);
    std::map<uint16_t, Code> codes_;
};

// A level's umbilicals at work: stepping on a pad closes its shell around
// the player, then the destination list opens (or, arriving, play goes on).
class Umbilicals {
public:
    explicit Umbilicals(Level& level);

    // A gizmo trigger fired on `pad` (GUmbilicalPlug_v1, from the trigger:
    // x = -1). With power and with a code held (or a fixed destination),
    // an idle pad starts its charge (state 2) and the shell attached to it
    // starts closing. Returns false if nothing started.
    bool trigger(const chk::GizmoRecord& pad, bool codesHeld, uint64_t nowMs);

    // What a trip has come to this frame.
    struct Event {
        enum Kind {
            Charged,  // the pad's charge played out: a fixed destination's code is given
            Closed,   // the shell has closed round the player
        } kind;
        const chk::GizmoRecord* pad = nullptr;
        UmbilicalLink link;
    };
    std::vector<Event> update(uint64_t nowMs);

    // After Closed: no trip (the list was cancelled, or the player has
    // just arrived). The shell goes (state 0, hidden) and the pad idles
    // again (GUmbilicalShellPlug_v29's second call, GIAttacher slot 0).
    void release(uint64_t nowMs);
    // Whether a trip is under way on some pad.
    bool busy() const { return pad_ != nullptr; }

private:
    Level& level_;
    const chk::GizmoRecord* pad_ = nullptr;
    const chk::GizmoRecord* shell_ = nullptr;
    UmbilicalLink link_;
    uint64_t padEnd_ = 0, shellEnd_ = 0;
    bool charged_ = false, closed_ = false;
};

}  // namespace harbinger::world
