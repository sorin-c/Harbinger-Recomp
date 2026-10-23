// Tests for the .sqh reader. The synthetic cases always run; the real-data
// cases run when HARBINGER_DATA points at an install folder holding res*.sqh.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "formats/sqh.hpp"

using namespace harbinger::sqh;

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

// Builds directory tables the same way the shipped archives are laid out.
struct TableBuilder {
    std::vector<uint8_t> b{0, 0, 0, 0};
    void be32(uint32_t v) {
        for (int s = 24; s >= 0; s -= 8) b.push_back(uint8_t(v >> s));
    }
    void name(const std::string& n) {
        b.push_back(uint8_t(n.size() + 1));
        b.insert(b.end(), n.begin(), n.end());
        b.push_back(0);
    }
    void file(const std::string& n, uint32_t off, uint32_t size, bool last = false) {
        name(n);
        be32(last ? 0xFFFFFFFFu : 8);
        be32(off);
        be32(size);
    }
    size_t dir(const std::string& n) {  // returns patch position for span
        name(n);
        size_t at = b.size();
        be32(0);
        return at;
    }
    void patchSpan(size_t at, uint32_t span) {
        for (int i = 0; i < 4; ++i) b[at + i] = uint8_t(span >> (24 - 8 * i));
    }
    std::vector<uint8_t> finish() {
        uint32_t n = uint32_t(b.size());
        for (int i = 0; i < 4; ++i) b[i] = uint8_t(n >> (8 * i));
        return b;
    }
};

static void testSynthetic() {
    // res/ (last) -> a/ (span) { x.gim, y.gim(last) }, empty/ (span 0), z.wav (last)
    TableBuilder t;
    size_t res = t.dir("res");
    t.patchSpan(res, 0xFFFFFFFFu);
    size_t a = t.dir("a");
    size_t aStart = t.b.size();
    t.file("x.gim", 100, 10);
    t.file("y.gim", 110, 20, true);
    t.patchSpan(a, uint32_t(t.b.size() - aStart));
    t.dir("empty");  // span 0: no children
    t.file("z.wav", 130, 5, true);
    auto table = t.finish();

    auto e = parseDirectory(table);
    CHECK(e.size() == 3);
    CHECK(e.size() == 3 && e[0].path == "res/a/x.gim" && e[0].offset == 100 && e[0].size == 10);
    CHECK(e.size() == 3 && e[1].path == "res/a/y.gim" && e[1].offset == 110);
    CHECK(e.size() == 3 && e[2].path == "res/z.wav" && e[2].size == 5);

    CHECK(lookup(table, "res/a/y.gim").has_value());
    CHECK(!lookup(table, "res/a/q.gim").has_value());
    CHECK(!lookup(table, "res/b/x.gim").has_value());

    CHECK(normalizePath("Res\\Act1\\Brushes\\Floor1.brs") == "res/act1/brushes/floor1.brs");

    auto bad = table;
    bad[0] ^= 1;  // size mismatch
    bool threw = false;
    try {
        parseDirectory(bad);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

static void checkArchive(const std::filesystem::path& p, size_t expectedFiles) {
    Archive arc(p.string());
    const auto& es = arc.entries();
    std::printf("  %s: %zu files\n", p.filename().string().c_str(), es.size());
    CHECK(es.size() == expectedFiles);

    // Payloads are packed back to back right after the table and fill the file exactly.
    std::vector<Entry> sorted(es.begin(), es.end());
    std::sort(sorted.begin(), sorted.end(), [](auto& x, auto& y) { return x.offset < y.offset; });
    bool contiguous = true;
    for (size_t i = 1; i < sorted.size(); ++i)
        contiguous &= sorted[i - 1].offset + sorted[i - 1].size == sorted[i].offset;
    CHECK(contiguous);
    // Every enumerated entry resolves to the same record through the game's lookup
    // algorithm, which confirms the enumeration's file/directory rule.
    size_t mismatches = 0;
    for (const auto& e : es) {
        auto pos = lookup(arc.table(), e.path);
        if (!pos || *pos + 8 > arc.table().size()) {
            ++mismatches;
            continue;
        }
        const auto& tb = arc.table();
        auto be = [&](size_t q) { return (uint32_t(tb[q]) << 24) | (uint32_t(tb[q + 1]) << 16) | (uint32_t(tb[q + 2]) << 8) | tb[q + 3]; };
        mismatches += be(*pos) != e.offset || be(*pos + 4) != e.size;
    }
    CHECK(mismatches == 0);

    CHECK(uint64_t(sorted.back().offset) + sorted.back().size == std::filesystem::file_size(p));
}

static void testRealData(const char* dir) {
    namespace fs = std::filesystem;
    const fs::path base = dir;
    std::printf("real data at %s\n", dir);
    checkArchive(base / "res1.sqh", 983);
    checkArchive(base / "res0.sqh", 16383);

    Archive res0((base / "res0.sqh").string());
    // Game-style path lookup (mixed case, backslashes) and stored payload signatures.
    const Entry* wav = res0.find("Res\\Act1\\Sounds\\Gizmos\\ball.wav");
    CHECK(wav != nullptr);
    if (wav) {
        auto d = res0.read(*wav);
        CHECK(d.size() >= 12 && std::memcmp(d.data(), "RIFF", 4) == 0 && std::memcmp(d.data() + 8, "WAVE", 4) == 0);
    }
    const Entry* gsc = res0.find("res/common/scripts/common scripts/checkpoint.gsc");
    CHECK(gsc != nullptr);
    if (gsc) {
        auto d = res0.read(*gsc);
        CHECK(d.size() > 8 && std::memcmp(d.data(), "Version ", 8) == 0);
    }
}

int main() {
    testSynthetic();
    if (const char* dir = std::getenv("HARBINGER_DATA"))
        testRealData(dir);
    else
        std::printf("HARBINGER_DATA not set; skipping real-data checks\n");
    if (failures) std::fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
