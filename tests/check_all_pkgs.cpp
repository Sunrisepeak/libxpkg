// One-off integration check: load every .lua in a pkgindex and report failures.
// Usage: ./check_all_pkgs <path-to-pkgindex>
#include <cstdio>
import mcpplibs.xpkg;
import mcpplibs.xpkg.loader;
import std;

namespace fs = std::filesystem;
using namespace mcpplibs::xpkg;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::println(stderr, "usage: {} <pkgindex-dir>", argv[0]);
        return 1;
    }
    fs::path pkgs_dir = fs::path(argv[1]) / "pkgs";
    int total = 0, ok = 0, fail = 0;

    for (auto& letter : fs::directory_iterator(pkgs_dir)) {
        if (!letter.is_directory()) continue;
        for (auto& entry : fs::directory_iterator(letter)) {
            if (entry.path().extension() != ".lua") continue;
            ++total;
            auto result = load_package(entry.path());
            if (result) {
                ++ok;
            } else {
                ++fail;
                std::println(stderr, "FAIL: {} — {}", entry.path().string(), result.error());
            }
        }
    }

    std::println("Total: {}  OK: {}  FAIL: {}", total, ok, fail);

    // Also test build_index
    auto idx = build_index(fs::path(argv[1]));
    if (idx) {
        std::println("build_index: {} entries", idx->entries.size());
    } else {
        std::println(stderr, "build_index FAILED: {}", idx.error());
    }
    return fail;
}
