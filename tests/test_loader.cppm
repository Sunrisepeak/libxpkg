module;
#include <gtest/gtest.h>
#include <filesystem>
#include <string_view>

export module xpkg.test.loader;
import mcpplibs.xpkg;
import mcpplibs.xpkg.loader;

using namespace mcpplibs::xpkg;
namespace fs = std::filesystem;

static constexpr std::string_view PKGINDEX =
    "/home/speak/workspace/github/d2learn/xim-pkgindex";

TEST(LoaderTest, LoadPackage_MissingFile) {
    auto result = load_package("/nonexistent/pkg.lua");
    EXPECT_FALSE(result.has_value());
}

TEST(LoaderTest, LoadPackage_Mdbook) {
    auto result = load_package(fs::path(PKGINDEX) / "pkgs/m/mdbook.lua");
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_EQ(result->name, "mdbook");
    EXPECT_EQ(result->type, PackageType::Package);
    EXPECT_EQ(result->status, PackageStatus::Stable);
    EXPECT_FALSE(result->xpm.entries.empty());
    EXPECT_TRUE(result->xvm_enable);
}

TEST(LoaderTest, LoadPackage_HasLinuxPlatform) {
    auto result = load_package(fs::path(PKGINDEX) / "pkgs/m/mdbook.lua");
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->xpm.entries.count("linux"), 0u);
}

TEST(LoaderTest, BuildIndex_ReturnsEntries) {
    auto result = build_index(fs::path(PKGINDEX));
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_GT(result->entries.size(), 0u);
    EXPECT_GT(result->entries.count("mdbook"), 0u);
}
