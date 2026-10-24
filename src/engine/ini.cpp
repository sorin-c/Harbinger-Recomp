#include "engine/ini.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace harbinger {
namespace {

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return out;
}

}  // namespace

Ini::Ini(std::string_view text) {
    std::string section;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        const std::string line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(std::string_view(line).substr(1, line.size() - 2)));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        sections_[section].emplace(lower(trim(std::string_view(line).substr(0, eq))),
                                   trim(std::string_view(line).substr(eq + 1)));
    }
}

Ini Ini::fromBytes(const std::vector<uint8_t>& data) {
    return Ini(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
}

std::optional<std::string> Ini::get(std::string_view section, std::string_view key) const {
    const auto s = sections_.find(lower(section));
    if (s == sections_.end()) return std::nullopt;
    const auto k = s->second.find(lower(key));
    if (k == s->second.end()) return std::nullopt;
    return k->second;
}

std::string Ini::require(std::string_view section, std::string_view key) const {
    if (auto v = get(section, key)) return *v;
    throw std::runtime_error("Harbinger.ini: missing [" + std::string(section) + "] " + std::string(key));
}

}  // namespace harbinger
