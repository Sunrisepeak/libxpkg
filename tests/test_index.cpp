#include <gtest/gtest.h>
import std;
import mcpplibs.xpkg;
import mcpplibs.xpkg.index;

using namespace mcpplibs::xpkg;

// Build a small PackageIndex for unit tests
static PackageIndex make_test_index() {
    PackageIndex idx;

    auto add = [&](const std::string& name, const std::string& desc,
                   PackageType type = PackageType::Package) {
        IndexEntry e;
        e.name        = name;
        e.description = desc;
        e.type        = type;
        e.installed   = false;
        idx.entries[name] = e;
    };

    add("vscode",       "Visual Studio Code editor");
    add("vscode@1.85.0","Visual Studio Code 1.85.0");
    add("python",       "Python programming language");
    add("python@3.12.0","Python 3.12.0");
    add("llvm",         "LLVM compiler infrastructure");
    add("neovim",       "Neovim text editor");

    // vscode is an alias for vscode@1.85.0
    idx.entries["vscode"].ref = "vscode@1.85.0";

    // mutex group: vscode@1.85.0 and neovim conflict
    idx.mutex_groups["editor"] = {"vscode@1.85.0", "neovim"};

    return idx;
}

// ── search ────────────────────────────────────────────────────────────────

TEST(IndexTest, Search_FindsByName) {
    auto idx = make_test_index();
    auto results = search(idx, "vscode");
    EXPECT_FALSE(results.empty());
    bool found = std::any_of(results.begin(), results.end(),
        [](auto& r){ return r.find("vscode") != std::string::npos; });
    EXPECT_TRUE(found);
}

TEST(IndexTest, Search_FindsByDescription) {
    auto idx = make_test_index();
    auto results = search(idx, "compiler");
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results.front(), "llvm");
}

TEST(IndexTest, Search_CaseInsensitive) {
    auto idx = make_test_index();
    auto results = search(idx, "PYTHON");
    EXPECT_FALSE(results.empty());
}

TEST(IndexTest, Search_NoMatch) {
    auto idx = make_test_index();
    auto results = search(idx, "xxxxxxnotfound");
    EXPECT_TRUE(results.empty());
}

// ── resolve ───────────────────────────────────────────────────────────────

TEST(IndexTest, Resolve_FollowsAlias) {
    auto idx = make_test_index();
    EXPECT_EQ(resolve(idx, "vscode"), "vscode@1.85.0");
}

TEST(IndexTest, Resolve_NoAliasReturnsSelf) {
    auto idx = make_test_index();
    EXPECT_EQ(resolve(idx, "llvm"), "llvm");
}

TEST(IndexTest, Resolve_MissingReturnsSelf) {
    auto idx = make_test_index();
    EXPECT_EQ(resolve(idx, "notexist"), "notexist");
}

// ── match_version ─────────────────────────────────────────────────────────

TEST(IndexTest, MatchVersion_ExactKey) {
    auto idx = make_test_index();
    auto r = match_version(idx, "python@3.12.0");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, "python@3.12.0");
}

TEST(IndexTest, MatchVersion_BaseNameReturnsLatest) {
    auto idx = make_test_index();
    auto r = match_version(idx, "python");
    ASSERT_TRUE(r.has_value());
    // "python" itself is an exact entry — returns "python"
    EXPECT_EQ(*r, "python");
}

TEST(IndexTest, MatchVersion_PrefersInstalled) {
    auto idx = make_test_index();
    // Remove exact "python" so it falls through to versioned lookup
    idx.entries.erase("python");
    idx.entries["python@3.12.0"].installed = true;
    auto r = match_version(idx, "python");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, "python@3.12.0");
}

TEST(IndexTest, MatchVersion_NotFound) {
    auto idx = make_test_index();
    auto r = match_version(idx, "doesnotexist");
    EXPECT_FALSE(r.has_value());
}

// ── mutex_packages ────────────────────────────────────────────────────────

TEST(IndexTest, MutexPackages_ReturnsGroup) {
    auto idx = make_test_index();
    auto result = mutex_packages(idx, "vscode@1.85.0");
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "neovim");
}

TEST(IndexTest, MutexPackages_NotInGroup) {
    auto idx = make_test_index();
    auto result = mutex_packages(idx, "python");
    EXPECT_TRUE(result.empty());
}

// ── merge ─────────────────────────────────────────────────────────────────

TEST(IndexTest, Merge_AddsOverlayEntries) {
    auto base = make_test_index();
    PackageIndex overlay;
    IndexEntry e;
    e.name = "rust"; e.description = "Rust programming language";
    overlay.entries["rust"] = e;

    auto merged = merge(base, overlay);
    EXPECT_GT(merged.entries.count("rust"), 0u);
}

TEST(IndexTest, Merge_AppliesNamespace) {
    PackageIndex base, overlay;
    IndexEntry e;
    e.name = "cmake"; e.description = "CMake build tool";
    overlay.entries["cmake"] = e;

    auto merged = merge(base, overlay, "extra");
    EXPECT_GT(merged.entries.count("extra-x-cmake"), 0u);
    EXPECT_EQ(merged.entries.count("cmake"), 0u);
}

TEST(IndexTest, Merge_PreservesBase) {
    auto base = make_test_index();
    PackageIndex overlay;
    auto merged = merge(base, overlay);
    EXPECT_EQ(merged.entries.size(), base.entries.size());
}

// ── set_installed ─────────────────────────────────────────────────────────

TEST(IndexTest, SetInstalled_UpdatesFlag) {
    auto idx = make_test_index();
    EXPECT_FALSE(idx.entries["llvm"].installed);
    set_installed(idx, "llvm", true);
    EXPECT_TRUE(idx.entries["llvm"].installed);
    set_installed(idx, "llvm", false);
    EXPECT_FALSE(idx.entries["llvm"].installed);
}

TEST(IndexTest, SetInstalled_MissingEntryNoOp) {
    auto idx = make_test_index();
    EXPECT_NO_THROW(set_installed(idx, "nonexistent", true));
}
