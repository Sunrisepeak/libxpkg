#include <gtest/gtest.h>
#include <cstdlib>
import std;
import mcpplibs.xpkg;
import mcpplibs.xpkg.executor;

using namespace mcpplibs::xpkg;
namespace fs = std::filesystem;

#ifndef XPKG_TEST_PKGINDEX
#  define XPKG_TEST_PKGINDEX tests/fixtures/pkgindex
#endif

#define XPKG_STRINGIFY_IMPL(x) #x
#define XPKG_STRINGIFY(x) XPKG_STRINGIFY_IMPL(x)

namespace {

std::string_view normalize_pkgindex_macro(std::string_view value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

static const fs::path PKGINDEX{
    std::string(normalize_pkgindex_macro(XPKG_STRINGIFY(XPKG_TEST_PKGINDEX)))
};
static const fs::path HELLO_PKG = PKGINDEX / "pkgs/h/hello.lua";

fs::path make_temp_dir(std::string_view prefix) {
    auto dir = fs::temp_directory_path() / fs::path(prefix);
    dir += std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(dir);
    return dir;
}

void write_text(const fs::path& path, std::string_view content) {
    std::ofstream out(path);
    ASSERT_TRUE(out.good()) << "failed to write " << path.string();
    out << content;
}

void write_executable_script(const fs::path& path, std::string_view content) {
    write_text(path, content);
    fs::permissions(path,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
                    fs::perms::group_read | fs::perms::group_exec |
                    fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::replace);
}

struct ScopedEnvVar {
    std::string name;
    std::optional<std::string> old_value;

    ScopedEnvVar(std::string name_, std::string value)
        : name(std::move(name_)) {
        if (const char* existing = std::getenv(name.c_str())) {
            old_value = existing;
        }
        set(value);
    }

    ~ScopedEnvVar() {
        if (old_value) {
            set(*old_value);
        } else {
            unset();
        }
    }

private:
    void set(std::string_view value) const {
#ifdef _WIN32
        _putenv_s(name.c_str(), std::string(value).c_str());
#else
        ::setenv(name.c_str(), std::string(value).c_str(), 1);
#endif
    }

    void unset() const {
#ifdef _WIN32
        _putenv_s(name.c_str(), "");
#else
        ::unsetenv(name.c_str());
#endif
    }
};

ExecutionContext make_context(const fs::path& install_dir, std::string platform) {
    ExecutionContext ctx;
    ctx.pkg_name = "elfpatch-macos";
    ctx.version = "1.0.0";
    ctx.platform = std::move(platform);
    ctx.arch = "arm64";
    ctx.install_file = install_dir / "elfpatch-macos.lua";
    ctx.install_dir = install_dir;
    ctx.run_dir = install_dir;
    ctx.xpkg_dir = install_dir;
    ctx.bin_dir = install_dir / "bin";
    ctx.project_data_dir = install_dir / "data";
    return ctx;
}

} // namespace

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

TEST(ExecutorTest, ApplyElfpatchAuto_DisabledReturnsZeroCounts) {
    auto exec = create_executor(HELLO_PKG);
    ASSERT_TRUE(exec.has_value()) << (exec ? "" : exec.error());

    auto patch_result = exec->apply_elfpatch_auto();
    EXPECT_TRUE(patch_result.success) << patch_result.error;
    EXPECT_EQ(patch_result.output, "0 0 0");
}

TEST(ExecutorTest, ApplyElfpatchAuto_WindowsSkipsPatching) {
    const fs::path temp_dir = make_temp_dir("libxpkg-elfpatch-windows-");
    const fs::path install_dir = temp_dir / "install";
    const fs::path lib_dir = install_dir / "lib";
    const fs::path pkg_path = temp_dir / "elfpatch-windows.lua";

    fs::create_directories(lib_dir);
    write_text(pkg_path,
               "package = { spec = \"1\", name = \"elfpatch-windows\", xpm = { windows = { [\"latest\"] = { ref = \"1.0.0\" }, [\"1.0.0\"] = { url = \"https://example.com/demo.zip\", sha256 = \"0\" } } } }\n"
               "local elfpatch = import(\"xim.libxpkg.elfpatch\")\n"
               "function install()\n"
               "    elfpatch.auto({ enable = true })\n"
               "    return true\n"
               "end\n");

    auto exec = create_executor(pkg_path);
    ASSERT_TRUE(exec.has_value()) << (exec ? "" : exec.error());

    auto hook_result = exec->run_hook(HookType::Install, make_context(install_dir, "windows"));
    ASSERT_TRUE(hook_result.success) << hook_result.error;

    auto patch_result = exec->apply_elfpatch_auto();
    EXPECT_TRUE(patch_result.success) << patch_result.error;
    EXPECT_EQ(patch_result.output, "0 0 0");

    fs::remove_all(temp_dir);
}

TEST(ExecutorTest, ApplyElfpatchAuto_LinuxUsesPatchelfForElf) {
#ifdef _WIN32
    GTEST_SKIP() << "Linux tool emulation test is POSIX-specific";
#endif

    const fs::path temp_dir = make_temp_dir("libxpkg-elfpatch-linux-");
    const fs::path tools_dir = temp_dir / "tools";
    const fs::path install_dir = temp_dir / "install";
    const fs::path lib_dir = install_dir / "lib";
    const fs::path log_path = temp_dir / "tool.log";
    const fs::path pkg_path = temp_dir / "elfpatch-linux.lua";
    const fs::path binary_path = install_dir / "demo-bin";

    fs::create_directories(tools_dir);
    fs::create_directories(lib_dir);

    write_executable_script(tools_dir / "patchelf",
                            "#!/bin/sh\n"
                            "printf 'patchelf %s\\n' \"$*\" >> \"$ELFPATCH_LOG\"\n");

    {
        std::ofstream binary(binary_path, std::ios::binary);
        ASSERT_TRUE(binary.good());
        const unsigned char magic[] = {0x7f, 'E', 'L', 'F', 0, 0, 0, 0};
        binary.write(reinterpret_cast<const char*>(magic), sizeof(magic));
    }
    fs::permissions(binary_path,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec,
                    fs::perm_options::replace);

    write_text(pkg_path,
               "package = { spec = \"1\", name = \"elfpatch-linux\", xpm = { linux = { [\"latest\"] = { ref = \"1.0.0\" }, [\"1.0.0\"] = { url = \"https://example.com/demo.tar.gz\", sha256 = \"0\" } } } }\n"
               "local elfpatch = import(\"xim.libxpkg.elfpatch\")\n"
               "function install()\n"
               "    elfpatch.auto({ enable = true })\n"
               "    return true\n"
               "end\n");

    const std::string original_path = std::getenv("PATH") ? std::getenv("PATH") : "";
    ScopedEnvVar path_env("PATH", tools_dir.string() + ":" + original_path);
    ScopedEnvVar log_env("ELFPATCH_LOG", log_path.string());

    auto exec = create_executor(pkg_path);
    ASSERT_TRUE(exec.has_value()) << (exec ? "" : exec.error());

    auto hook_result = exec->run_hook(HookType::Install, make_context(install_dir, "linux"));
    ASSERT_TRUE(hook_result.success) << hook_result.error;

    auto patch_result = exec->apply_elfpatch_auto();
    EXPECT_TRUE(patch_result.success) << patch_result.error;
    EXPECT_EQ(patch_result.output, "1 1 0");

    std::ifstream log_file(log_path);
    std::ostringstream log_buffer;
    log_buffer << log_file.rdbuf();
    const std::string log = log_buffer.str();
    EXPECT_NE(log.find("--set-rpath " + lib_dir.string()), std::string::npos);

    fs::remove_all(temp_dir);
}

TEST(ExecutorTest, ApplyElfpatchAuto_MacOsUsesInstallNameToolForMachO) {
#ifdef _WIN32
    GTEST_SKIP() << "macOS tool emulation test is POSIX-specific";
#endif

    const fs::path temp_dir = make_temp_dir("libxpkg-elfpatch-macos-");
    const fs::path tools_dir = temp_dir / "tools";
    const fs::path install_dir = temp_dir / "install";
    const fs::path lib_dir = install_dir / "lib";
    const fs::path log_path = temp_dir / "tool.log";
    const fs::path pkg_path = temp_dir / "elfpatch-macos.lua";
    const fs::path binary_path = install_dir / "demo-bin";

    fs::create_directories(tools_dir);
    fs::create_directories(lib_dir);

    write_executable_script(tools_dir / "install_name_tool",
                            "#!/bin/sh\n"
                            "printf 'install_name_tool %s\\n' \"$*\" >> \"$ELFPATCH_LOG\"\n");
    write_executable_script(tools_dir / "otool",
                            "#!/bin/sh\n"
                            "if [ \"$1\" = \"-L\" ]; then\n"
                            "  printf '%s:\\n' \"$2\"\n"
                            "  printf '\\t/opt/demo/lib/libdemo.dylib (compatibility version 1.0.0, current version 1.0.0)\\n'\n"
                            "fi\n");

    {
        std::ofstream binary(binary_path, std::ios::binary);
        ASSERT_TRUE(binary.good());
        const unsigned char magic[] = {0xfe, 0xed, 0xfa, 0xcf, 0, 0, 0, 0};
        binary.write(reinterpret_cast<const char*>(magic), sizeof(magic));
    }
    fs::permissions(binary_path,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec,
                    fs::perm_options::replace);

    write_text(pkg_path,
               "package = { spec = \"1\", name = \"elfpatch-macos\", xpm = { macosx = { [\"latest\"] = { ref = \"1.0.0\" }, [\"1.0.0\"] = { url = \"https://example.com/demo.tar.gz\", sha256 = \"0\" } } } }\n"
               "local elfpatch = import(\"xim.libxpkg.elfpatch\")\n"
               "function install()\n"
               "    elfpatch.auto({ enable = true })\n"
               "    return true\n"
               "end\n");

    const std::string original_path = std::getenv("PATH") ? std::getenv("PATH") : "";
    ScopedEnvVar path_env("PATH", tools_dir.string() + ":" + original_path);
    ScopedEnvVar log_env("ELFPATCH_LOG", log_path.string());

    auto exec = create_executor(pkg_path);
    ASSERT_TRUE(exec.has_value()) << (exec ? "" : exec.error());

    auto hook_result = exec->run_hook(HookType::Install, make_context(install_dir, "macosx"));
    ASSERT_TRUE(hook_result.success) << hook_result.error;

    auto patch_result = exec->apply_elfpatch_auto();
    EXPECT_TRUE(patch_result.success) << patch_result.error;
    EXPECT_EQ(patch_result.output, "1 1 0");

    std::ifstream log_file(log_path);
    std::ostringstream log_buffer;
    log_buffer << log_file.rdbuf();
    const std::string log = log_buffer.str();
    EXPECT_NE(log.find("-add_rpath " + lib_dir.string()), std::string::npos);
    EXPECT_NE(log.find("-change /opt/demo/lib/libdemo.dylib @rpath/libdemo.dylib " + binary_path.string()),
              std::string::npos);

    fs::remove_all(temp_dir);
}

TEST(ExecutorTest, ApplyElfpatchAuto_MacOsAddRpathFailureCountsAsFailed) {
#ifdef _WIN32
    GTEST_SKIP() << "macOS tool emulation test is POSIX-specific";
#endif

    const fs::path temp_dir = make_temp_dir("libxpkg-elfpatch-macos-rpath-fail-");
    const fs::path tools_dir = temp_dir / "tools";
    const fs::path install_dir = temp_dir / "install";
    const fs::path lib_dir = install_dir / "lib";
    const fs::path pkg_path = temp_dir / "elfpatch-macos.lua";
    const fs::path binary_path = install_dir / "demo-bin";

    fs::create_directories(tools_dir);
    fs::create_directories(lib_dir);

    write_executable_script(tools_dir / "install_name_tool",
                            "#!/bin/sh\n"
                            "exit 1\n");
    write_executable_script(tools_dir / "otool",
                            "#!/bin/sh\n"
                            "if [ \"$1\" = \"-L\" ]; then\n"
                            "  printf '%s:\\n' \"$2\"\n"
                            "fi\n");

    {
        std::ofstream binary(binary_path, std::ios::binary);
        ASSERT_TRUE(binary.good());
        const unsigned char magic[] = {0xfe, 0xed, 0xfa, 0xcf, 0, 0, 0, 0};
        binary.write(reinterpret_cast<const char*>(magic), sizeof(magic));
    }
    fs::permissions(binary_path,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec,
                    fs::perm_options::replace);

    write_text(pkg_path,
               "package = { spec = \"1\", name = \"elfpatch-macos\", xpm = { macosx = { [\"latest\"] = { ref = \"1.0.0\" }, [\"1.0.0\"] = { url = \"https://example.com/demo.tar.gz\", sha256 = \"0\" } } } }\n"
               "local elfpatch = import(\"xim.libxpkg.elfpatch\")\n"
               "function install()\n"
               "    elfpatch.auto({ enable = true })\n"
               "    return true\n"
               "end\n");

    const std::string original_path = std::getenv("PATH") ? std::getenv("PATH") : "";
    ScopedEnvVar path_env("PATH", tools_dir.string() + ":" + original_path);

    auto exec = create_executor(pkg_path);
    ASSERT_TRUE(exec.has_value()) << (exec ? "" : exec.error());

    auto hook_result = exec->run_hook(HookType::Install, make_context(install_dir, "macosx"));
    ASSERT_TRUE(hook_result.success) << hook_result.error;

    auto patch_result = exec->apply_elfpatch_auto();
    EXPECT_TRUE(patch_result.success) << patch_result.error;
    EXPECT_EQ(patch_result.output, "1 0 1");

    fs::remove_all(temp_dir);
}

TEST(ExecutorTest, ApplyElfpatchAuto_MacOsMissingToolSkipsGracefully) {
#ifdef _WIN32
    GTEST_SKIP() << "macOS tool lookup test is POSIX-specific";
#endif

    const fs::path temp_dir = make_temp_dir("libxpkg-elfpatch-macos-missing-tool-");
    const fs::path empty_tools_dir = temp_dir / "empty-tools";
    const fs::path install_dir = temp_dir / "install";
    const fs::path lib_dir = install_dir / "lib";
    const fs::path pkg_path = temp_dir / "elfpatch-macos.lua";
    const fs::path binary_path = install_dir / "demo-bin";

    fs::create_directories(empty_tools_dir);
    fs::create_directories(lib_dir);

    {
        std::ofstream binary(binary_path, std::ios::binary);
        ASSERT_TRUE(binary.good());
        const unsigned char magic[] = {0xfe, 0xed, 0xfa, 0xcf, 0, 0, 0, 0};
        binary.write(reinterpret_cast<const char*>(magic), sizeof(magic));
    }
    fs::permissions(binary_path,
                    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec,
                    fs::perm_options::replace);

    write_text(pkg_path,
               "package = { spec = \"1\", name = \"elfpatch-macos\", xpm = { macosx = { [\"latest\"] = { ref = \"1.0.0\" }, [\"1.0.0\"] = { url = \"https://example.com/demo.tar.gz\", sha256 = \"0\" } } } }\n"
               "local elfpatch = import(\"xim.libxpkg.elfpatch\")\n"
               "function install()\n"
               "    elfpatch.auto({ enable = true })\n"
               "    return true\n"
               "end\n");

    ScopedEnvVar path_env("PATH", empty_tools_dir.string());

    auto exec = create_executor(pkg_path);
    ASSERT_TRUE(exec.has_value()) << (exec ? "" : exec.error());

    auto hook_result = exec->run_hook(HookType::Install, make_context(install_dir, "macosx"));
    ASSERT_TRUE(hook_result.success) << hook_result.error;

    auto patch_result = exec->apply_elfpatch_auto();
    EXPECT_TRUE(patch_result.success) << patch_result.error;
    EXPECT_EQ(patch_result.output, "0 0 0");

    fs::remove_all(temp_dir);
}
