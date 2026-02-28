#include <gtest/gtest.h>
import std;
import mcpplibs.xpkg;
import mcpplibs.xpkg.executor;

using namespace mcpplibs::xpkg;
namespace fs = std::filesystem;

// Path to a real package from xim-pkgindex
static const fs::path MDBOOK_PKG =
    "/home/speak/workspace/github/d2learn/xim-pkgindex/pkgs/m/mdbook.lua";

TEST(ExecutorTest, CreateExecutor_ExistingFile) {
    auto result = create_executor(MDBOOK_PKG);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error());
}

TEST(ExecutorTest, CreateExecutor_MissingFile) {
    auto result = create_executor("/nonexistent/path/pkg.lua");
    EXPECT_FALSE(result.has_value());
}

TEST(ExecutorTest, HasHook_Install) {
    auto exec = create_executor(MDBOOK_PKG);
    ASSERT_TRUE(exec.has_value());
    EXPECT_TRUE(exec->has_hook(HookType::Install));
}

TEST(ExecutorTest, HasHook_Config) {
    auto exec = create_executor(MDBOOK_PKG);
    ASSERT_TRUE(exec.has_value());
    EXPECT_TRUE(exec->has_hook(HookType::Config));
}

TEST(ExecutorTest, HasHook_Uninstall) {
    auto exec = create_executor(MDBOOK_PKG);
    ASSERT_TRUE(exec.has_value());
    EXPECT_TRUE(exec->has_hook(HookType::Uninstall));
}

TEST(ExecutorTest, HasHook_Installed_False) {
    auto exec = create_executor(MDBOOK_PKG);
    ASSERT_TRUE(exec.has_value());
    // mdbook.lua does NOT have installed() hook
    EXPECT_FALSE(exec->has_hook(HookType::Installed));
}
