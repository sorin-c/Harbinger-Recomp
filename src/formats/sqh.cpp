#include "formats/sqh.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace harbinger::sqh {
namespace {

constexpr uint32_t kLastSibling = 0xFFFFFFFFu;

uint32_t readBE32(const std::vector<uint8_t>& b, size_t p) {
    if (p + 4 > b.size()) throw std::runtime_error("sqh: truncated directory");
    return (uint32_t(b[p]) << 24) | (uint32_t(b[p + 1]) << 16) | (uint32_t(b[p + 2]) << 8) | b[p + 3];
}

// Walks the nodes in [pos, end). A node is:
//   u8 nameLen (including the terminating NUL), char name[nameLen],
//   u32be span: for a directory, the byte length of its children
//               (0xFFFFFFFF = children run to the end of the parent);
//               for a file, 8 (or 0xFFFFFFFF) followed by
//   u32be offset, u32be size  (files only).
// Files are told apart from directories by a '.' in the name.
size_t walk(const std::vector<uint8_t>& t, size_t pos, size_t end, const std::string& prefix,
            std::vector<Entry>& out) {
    while (pos < end) {
        const size_t nameLen = t[pos];
        if (nameLen == 0 || pos + 1 + nameLen > end || t[pos + nameLen] != 0)
            throw std::runtime_error("sqh: bad node name");
        std::string name(reinterpret_cast<const char*>(&t[pos + 1]), nameLen - 1);
        pos += 1 + nameLen;
        const uint32_t span = readBE32(t, pos);
        pos += 4;

        if (name.find('.') != std::string::npos) {
            out.push_back({prefix + name, readBE32(t, pos), readBE32(t, pos + 4)});
            pos += 8;
        } else {
            const size_t childEnd = span == kLastSibling ? end : pos + span;
            if (childEnd > end) throw std::runtime_error("sqh: directory overruns parent");
            pos = walk(t, pos, childEnd, prefix + name + "/", out);
        }
    }
    return pos;
}

}  // namespace

std::vector<Entry> parseDirectory(const std::vector<uint8_t>& table) {
    if (table.size() < 4) throw std::runtime_error("sqh: table too small");
    const uint32_t size = uint32_t(table[0]) | (uint32_t(table[1]) << 8) | (uint32_t(table[2]) << 16) |
                          (uint32_t(table[3]) << 24);
    if (size != table.size()) throw std::runtime_error("sqh: table size mismatch");
    std::vector<Entry> out;
    walk(table, 4, table.size(), "", out);
    return out;
}

std::optional<size_t> lookup(const std::vector<uint8_t>& t, std::string_view path) {
    size_t pos = 4;
    while (true) {
        const size_t slash = path.find('/');
        const std::string_view part = path.substr(0, slash);
        // Scan the sibling list at `pos` for `part`.
        while (true) {
            if (pos >= t.size()) return std::nullopt;
            const size_t nameLen = t[pos];
            if (nameLen == 0 || pos + 1 + nameLen + 4 > t.size()) return std::nullopt;
            const std::string_view name(reinterpret_cast<const char*>(&t[pos + 1]), nameLen - 1);
            const size_t after = pos + 1 + nameLen + 4;
            if (name == part) {
                pos = after;
                break;
            }
            const uint32_t span = readBE32(t, pos + 1 + nameLen);
            if (span & 0x80000000u) return std::nullopt;  // last sibling, no match
            pos = after + span;
        }
        if (slash == std::string_view::npos) return pos;
        path.remove_prefix(slash + 1);
    }
}

std::string normalizePath(std::string_view path) {
    std::string s(path);
    for (char& c : s) c = c == '\\' ? '/' : char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

Archive::Archive(const std::string& filename) : file_(filename, std::ios::binary) {
    if (!file_) throw std::runtime_error("sqh: cannot open " + filename);
    uint8_t head[4];
    if (!file_.read(reinterpret_cast<char*>(head), 4)) throw std::runtime_error("sqh: cannot read header");
    const uint32_t size = uint32_t(head[0]) | (uint32_t(head[1]) << 8) | (uint32_t(head[2]) << 16) |
                          (uint32_t(head[3]) << 24);
    std::vector<uint8_t> table(size);
    std::copy(head, head + 4, table.begin());
    if (size < 4 || !file_.read(reinterpret_cast<char*>(table.data() + 4), size - 4))
        throw std::runtime_error("sqh: truncated directory table");
    entries_ = parseDirectory(table);
    table_ = std::move(table);
    for (size_t i = 0; i < entries_.size(); ++i) index_.emplace(normalizePath(entries_[i].path), i);
}

const Entry* Archive::find(std::string_view path) const {
    auto it = index_.find(normalizePath(path));
    return it == index_.end() ? nullptr : &entries_[it->second];
}

std::vector<uint8_t> Archive::read(const Entry& entry) {
    std::vector<uint8_t> data(entry.size);
    file_.clear();
    file_.seekg(entry.offset);
    if (!file_.read(reinterpret_cast<char*>(data.data()), entry.size))
        throw std::runtime_error("sqh: short read for " + entry.path);
    return data;
}

}  // namespace harbinger::sqh
