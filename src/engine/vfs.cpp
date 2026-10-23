#include "engine/vfs.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>

namespace harbinger {
namespace fs = std::filesystem;

namespace {

bool endsWith(const std::string& s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}  // namespace

Vfs::Vfs(const fs::path& installDir) : dir_(installDir) {
    std::vector<fs::path> found;
    for (const auto& e : fs::directory_iterator(installDir)) {
        const std::string name = sqh::normalizePath(e.path().filename().string());
        if (e.is_regular_file() && name.rfind("res", 0) == 0 && endsWith(name, ".sqh")) found.push_back(e.path());
    }
    if (found.empty()) throw std::runtime_error("no res*.sqh archives in " + installDir.string());
    std::sort(found.begin(), found.end());
    for (const auto& p : found) archives_.push_back(std::make_unique<sqh::Archive>(p.string()));
}

std::optional<Vfs::Hit> Vfs::resolve(const std::string& normalized) const {
    for (auto it = archives_.rbegin(); it != archives_.rend(); ++it)
        if (const sqh::Entry* e = (*it)->find(normalized)) return Hit{it->get(), e};
    return std::nullopt;
}

std::optional<std::vector<uint8_t>> Vfs::read(std::string_view path) {
    std::string key = sqh::normalizePath(path);
    if (endsWith(key, ".tga")) {
        std::string gim = key.substr(0, key.size() - 3) + "gim";
        if (auto hit = resolve(gim)) return hit->archive->read(*hit->entry);
    }
    if (auto hit = resolve(key)) return hit->archive->read(*hit->entry);

    // Loose file fallback. Game paths are case-insensitive; try as given, then lower case.
    std::string loose(path);
    std::replace(loose.begin(), loose.end(), '\\', '/');
    for (const fs::path& p : {dir_ / loose, dir_ / key}) {
        std::ifstream in(p, std::ios::binary);
        if (in) return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
    }
    return std::nullopt;
}

bool Vfs::exists(std::string_view path) const {
    const std::string key = sqh::normalizePath(path);
    if (endsWith(key, ".tga") && resolve(key.substr(0, key.size() - 3) + "gim")) return true;
    return resolve(key).has_value() || fs::exists(dir_ / key);
}

std::vector<std::string> Vfs::list(std::string_view prefix) const {
    const std::string p = sqh::normalizePath(prefix);
    std::set<std::string> out;
    for (const auto& a : archives_)
        for (const auto& e : a->entries())
            if (e.path.rfind(p, 0) == 0) out.insert(e.path);
    return {out.begin(), out.end()};
}

}  // namespace harbinger
