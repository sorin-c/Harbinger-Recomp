// Reader for Harbinger.ini, which names the resource indexes, music and acts.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace harbinger {

class Ini {
public:
    // Parses "[Section]" headers and "key=value" lines; ';' starts a comment line.
    // Section and key names are case-insensitive. A repeated key keeps its first
    // value, as Windows' GetPrivateProfileString does.
    explicit Ini(std::string_view text);
    static Ini fromBytes(const std::vector<uint8_t>& data);

    std::optional<std::string> get(std::string_view section, std::string_view key) const;
    // Like get(), but throws std::runtime_error naming the missing entry.
    std::string require(std::string_view section, std::string_view key) const;

private:
    std::map<std::string, std::map<std::string, std::string>> sections_;
};

}  // namespace harbinger
