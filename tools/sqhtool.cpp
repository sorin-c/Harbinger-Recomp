// sqhtool: list or extract the contents of a Harbinger .sqh archive.
//   sqhtool list    <archive.sqh>
//   sqhtool extract <archive.sqh> <outdir> [path-prefix]
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "formats/sqh.hpp"

namespace fs = std::filesystem;
using harbinger::sqh::Archive;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s list <archive>\n       %s extract <archive> <outdir> [prefix]\n",
                     argv[0], argv[0]);
        return 2;
    }
    const std::string cmd = argv[1];
    try {
        Archive archive(argv[2]);
        if (cmd == "list") {
            for (const auto& e : archive.entries()) std::printf("%10u %10u %s\n", e.offset, e.size, e.path.c_str());
            return 0;
        }
        if (cmd == "extract" && argc >= 4) {
            const fs::path outDir = argv[3];
            const std::string prefix = argc >= 5 ? harbinger::sqh::normalizePath(argv[4]) : "";
            size_t count = 0;
            for (const auto& e : archive.entries()) {
                if (harbinger::sqh::normalizePath(e.path).rfind(prefix, 0) != 0) continue;
                const fs::path target = outDir / fs::u8path(e.path);
                fs::create_directories(target.parent_path());
                const auto data = archive.read(e);
                std::ofstream(target, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), data.size());
                ++count;
            }
            std::printf("extracted %zu files\n", count);
            return 0;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }
    std::fprintf(stderr, "unknown command\n");
    return 2;
}
