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

TEST(IndexTest, FindCandidates_ExplicitNamespaceIsExact) {
    PackageIndex idx;
    IndexEntry entry;
    entry.identity = {
        .namespaceName = "alpha",
        .name = "demo",
    };
    entry.canonicalName = "alpha:demo";
    entry.entryKey = "alpha:demo";
    entry.name = "demo";
    idx.entries[entry.entryKey] = entry;
    idx.identityEntries[entry.canonicalName].push_back(entry.entryKey);
    idx.shortNames["demo"] = {"alpha:demo", "beta:demo"};

    auto result = find_candidates(idx, "demo", std::string_view { "alpha" });

    EXPECT_EQ(result, (std::vector<std::string> { "alpha:demo" }));
}

TEST(IndexTest, FindCandidates_BareNameReturnsAllSortedIdentities) {
    PackageIndex idx;
    idx.shortNames["demo"] = {"alpha:demo", "beta:demo"};

    auto result = find_candidates(idx, "demo");

    EXPECT_EQ(result,
              (std::vector<std::string> { "alpha:demo", "beta:demo" }));
}

TEST(IndexTest, Resolve_BareAliasRefInheritsCandidateNamespace) {
    PackageIndex idx;
    IndexEntry alias;
    alias.identity = {
        .namespaceName = "alpha",
        .name = "tool",
    };
    alias.canonicalName = "alpha:tool";
    alias.entryKey = "alpha:tool";
    alias.name = "tool";
    alias.ref = "compiler@1.0.0";
    idx.entries[alias.entryKey] = alias;

    EXPECT_EQ(resolve(idx, "alpha:tool"), "alpha:compiler@1.0.0");
}

TEST(IndexTest, MatchVersion_DoesNotCrossNamespaceIdentity) {
    PackageIndex idx;
    auto add = [&](std::string entryKey, std::string canonicalName,
                   std::string version, bool installed) {
        IndexEntry entry;
        entry.entryKey = entryKey;
        entry.canonicalName = canonicalName;
        entry.version = version;
        entry.installed = installed;
        idx.entries[entryKey] = entry;
        idx.identityEntries[canonicalName].push_back(entryKey);
    };
    add("alpha:demo@1.0.0", "alpha:demo", "1.0.0", false);
    add("beta:demo@9.0.0", "beta:demo", "9.0.0", true);

    auto result = match_version(idx, "alpha:demo");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "alpha:demo@1.0.0");
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
    EXPECT_GT(merged.entries.count("extra:cmake"), 0u);
    EXPECT_EQ(merged.entries.count("cmake"), 0u);
}

TEST(IndexTest, Merge_PreservesLegacyVersionedEntryKey) {
    PackageIndex base, overlay;
    IndexEntry entry;
    entry.name = "cmake@3.31.0";
    overlay.entries["cmake@3.31.0"] = entry;

    auto merged = merge(std::move(base), overlay, "extra");

    EXPECT_TRUE(merged.entries.contains("extra:cmake@3.31.0"));
    EXPECT_EQ(merged.entries.at("extra:cmake@3.31.0").version, "3.31.0");
}

TEST(IndexTest, Merge_PreservesBase) {
    auto base = make_test_index();
    PackageIndex overlay;
    auto merged = merge(base, overlay);
    EXPECT_EQ(merged.entries.size(), base.entries.size());
}

TEST(IndexTest, Merge_RejectsDuplicateCanonicalIdentity) {
    PackageIndex base, overlay;
    IndexEntry baseEntry;
    baseEntry.identity = {
        .namespaceName = "alpha",
        .name = "demo",
    };
    baseEntry.name = "demo";
    base.entries["alpha:demo"] = baseEntry;

    IndexEntry overlayEntry;
    overlayEntry.identity = {
        .namespaceName = "alpha",
        .name = "demo",
    };
    overlayEntry.name = "demo";
    overlay.entries["alpha:demo"] = overlayEntry;

    EXPECT_THROW(
        {
            auto ignored = merge(std::move(base), overlay);
            static_cast<void>(ignored);
        },
        std::invalid_argument);
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
