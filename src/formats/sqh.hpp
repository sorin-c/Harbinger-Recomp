// Reader for Harbinger's .sqh resource archives (res0.sqh, res1.sqh).
// Format notes and evidence: docs/formats/sqh.md
#pragma once

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace harbinger::sqh {

struct Entry {
    std::string path;  // as stored, '/'-separated, e.g. "res/act1/levels/(g)airlock.chk"
    uint32_t offset = 0;
    uint32_t size = 0;
};

// Parses a directory table. `table` holds the whole table, including the
// leading 4-byte size field. Throws std::runtime_error on malformed input.
std::vector<Entry> parseDirectory(const std::vector<uint8_t>& table);

// Lookup key used by the game's paths: lower case, '\\' folded to '/'.
std::string normalizePath(std::string_view path);

// Resolves a normalized path the way the game does: component by component,
// skipping non-matching siblings by their span. Returns the table position of
// the file's offset/size pair, or nothing when the path is absent.
std::optional<size_t> lookup(const std::vector<uint8_t>& table, std::string_view normalizedPath);

class Archive {
public:
    explicit Archive(const std::string& filename);

    const std::vector<Entry>& entries() const { return entries_; }
    const Entry* find(std::string_view path) const;
    const std::vector<uint8_t>& table() const { return table_; }
    std::vector<uint8_t> read(const Entry& entry);

private:
    std::ifstream file_;
    std::vector<uint8_t> table_;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> index_;
};

}  // namespace harbinger::sqh
