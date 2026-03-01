#include <gtest/gtest.h>
import std;
import mcpplibs.xpkg;
import mcpplibs.xpkg.executor;

using namespace mcpplibs::xpkg;
namespace fs = std::filesystem;

#ifndef XPKG_TEST_PKGINDEX
#  define XPKG_TEST_PKGINDEX "tests/fixtures/pkgindex"
#endif

static const fs::path HELLO_PKG =
    fs::path(XPKG_TEST_PKGINDEX) / "pkgs/h/hello.lua";

TEST(ExecutorTest, CreateExecutor_ExistingFile) {
    auto result = create_executor(HELLO_PKG);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error());
}

TEST(ExecutorTest, CreateExecutor_MissingFile) {
    auto result = create_executor("/nonexistent/path/pkg.lua");
    EXPECT_FALSE(result.has_value());
}

TEST(ExecutorTest, HasHook_Install) {
    auto exec = create_executor(HELLO_PKG);
    ASSERT_TRUE(exec.has_value());
    EXPECT_TRUE(exec->has_hook(HookType::Install));
}

TEST(ExecutorTest, HasHook_Config) {
    auto exec = create_executor(HELLO_PKG);
    ASSERT_TRUE(exec.has_value());
    EXPECT_TRUE(exec->has_hook(HookType::Config));
}

TEST(ExecutorTest, HasHook_Uninstall) {
    auto exec = create_executor(HELLO_PKG);
    ASSERT_TRUE(exec.has_value());
    EXPECT_TRUE(exec->has_hook(HookType::Uninstall));
}

TEST(ExecutorTest, HasHook_Installed_True) {
    auto exec = create_executor(HELLO_PKG);
    ASSERT_TRUE(exec.has_value());
    // hello.lua has an installed() hook (unlike the old mdbook fixture)
    EXPECT_TRUE(exec->has_hook(HookType::Installed));
}
