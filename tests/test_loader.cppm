module;
#include <gtest/gtest.h>
#include <filesystem>
#include <string_view>

export module xpkg.test.loader;
import mcpplibs.xpkg;
import mcpplibs.xpkg.loader;

using namespace mcpplibs::xpkg;
namespace fs = std::filesystem;

#ifndef XPKG_TEST_PKGINDEX
#  define XPKG_TEST_PKGINDEX tests/fixtures/pkgindex
#endif

#ifndef XPKG_TEST_PKGINDEX_BUILD
#  define XPKG_TEST_PKGINDEX_BUILD tests/fixtures/pkgindex-build
#endif

#define XPKG_STRINGIFY_IMPL(x) #x
#define XPKG_STRINGIFY(x) XPKG_STRINGIFY_IMPL(x)

constexpr std::string_view normalize_pkgindex_macro(std::string_view value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

static const fs::path PKGINDEX{
    std::string(normalize_pkgindex_macro(XPKG_STRINGIFY(XPKG_TEST_PKGINDEX)))
};

static const fs::path PKGINDEX_BUILD{
    std::string(normalize_pkgindex_macro(XPKG_STRINGIFY(XPKG_TEST_PKGINDEX_BUILD)))
};

TEST(LoaderTest, LoadPackage_MissingFile) {
    auto result = load_package("/nonexistent/pkg.lua");
    EXPECT_FALSE(result.has_value());
}

TEST(LoaderTest, LoadPackage_Hello) {
    auto result = load_package(PKGINDEX / "pkgs/h/hello.lua");
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_EQ(result->name, "hello");
    EXPECT_EQ(result->type, PackageType::Package);
    EXPECT_EQ(result->status, PackageStatus::Stable);
    EXPECT_FALSE(result->xpm.entries.empty());
    EXPECT_TRUE(result->xvm_enable);
}

TEST(LoaderTest, LoadPackage_HasLinuxPlatform) {
    auto result = load_package(PKGINDEX / "pkgs/h/hello.lua");
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->xpm.entries.count("linux"), 0u);
}

TEST(LoaderTest, BuildIndex_ReturnsEntries) {
    auto result = build_index(PKGINDEX);
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_GT(result->entries.size(), 0u);
    EXPECT_GT(result->entries.count("hello"), 0u);
}

TEST(LoaderTest, BuildIndex_PkgindexBuild_OsFiles) {
    // Tests that build_index works with pkgindex-build.lua that uses os.files()
    // This validates the C++ std::filesystem implementation works cross-platform
    auto result = build_index(PKGINDEX_BUILD);
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_GT(result->entries.count("testbuild"), 0u);
}

TEST(LoaderTest, BuildIndex_PkgindexBuild_TemplateAppended) {
    // After pkgindex-build runs, the testbuild package should have xpm data
    // from the appended template
    auto result = build_index(PKGINDEX_BUILD);
    ASSERT_TRUE(result.has_value()) << result.error();
    auto pkg = load_package(result->entries.at("testbuild").path);
    ASSERT_TRUE(pkg.has_value()) << pkg.error();
    // Template adds xpm with linux/windows/macosx platforms
    EXPECT_FALSE(pkg->xpm.entries.empty()) << "template xpm should have been appended by pkgindex-build";
}
