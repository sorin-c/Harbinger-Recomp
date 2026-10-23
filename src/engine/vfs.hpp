// Virtual file system over the game's .sqh archives and loose files.
// Mirrors how the original resolves resource paths; see docs/formats/sqh.md.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formats/sqh.hpp"

namespace harbinger {

class Vfs {
public:
    // Mounts every res*.sqh in `installDir`. Later archives (by name) take
    // precedence, so res1 overrides res0 (guessed order, see sqh.md).
    explicit Vfs(const std::filesystem::path& installDir);

    // Reads a resource by game path (any case, '\' or '/'). Like the original,
    // a ".tga" request is served from the ".gim" entry, and a path missing from
    // every archive falls back to a loose file under the install folder.
    std::optional<std::vector<uint8_t>> read(std::string_view path);
    bool exists(std::string_view path) const;

    // All archive paths (normalized, de-duplicated) that start with `prefix`.
    std::vector<std::string> list(std::string_view prefix = {}) const;

    const std::filesystem::path& installDir() const { return dir_; }
    size_t archiveCount() const { return archives_.size(); }

private:
    struct Hit {
        sqh::Archive* archive;
        const sqh::Entry* entry;
    };
    std::optional<Hit> resolve(const std::string& normalized) const;

    std::filesystem::path dir_;
    std::vector<std::unique_ptr<sqh::Archive>> archives_;  // highest precedence last
};

}  // namespace harbinger
