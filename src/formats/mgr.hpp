// Resource index files (.mgr): map numeric ids to resource paths.
// Format notes and evidence: docs/formats/mgr.md
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace harbinger::mgr {

struct Index {
    std::string name;  // e.g. "Items.itm", "Font.fnt"
    std::vector<std::pair<uint16_t, std::string>> entries;  // file order

    // Path for `id`, or nullptr.
    const std::string* find(uint16_t id) const;
};

// Parses the plain id/path layout. Throws std::runtime_error on malformed data
// or trailing bytes (the dialog, speech, datacard and *animgroup indexes use
// other layouts that are not decoded yet).
Index parse(const std::vector<uint8_t>& data);

}  // namespace harbinger::mgr
