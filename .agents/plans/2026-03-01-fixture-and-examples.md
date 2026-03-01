# Fixture, Privacy Scrub & Lifecycle Example Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Remove all hardcoded `/home/<user>/` paths from the project, replace test dependencies on the external xim-pkgindex repo with a self-contained local fixture, and add a `lifecycle` example demonstrating the full load → parse → install → uninstall flow.

**Architecture:** A minimal `tests/fixtures/pkgindex/` directory mirrors the real xim-pkgindex layout and contains a single `hello` package with all hooks. Both tests and examples resolve the fixture path via a compile-time xmake define (`XPKG_TEST_PKGINDEX` / `XPKG_FIXTURES_DIR`), so no absolute path ever appears in source code. Historical docs are scrubbed with placeholder paths.

**Tech Stack:** C++23 modules, xmake, Lua 5.4, GTest

---

## Task 1: Create `tests/fixtures/pkgindex/pkgs/h/hello.lua`

**Files:**
- Create: `tests/fixtures/pkgindex/pkgs/h/hello.lua`

This is the self-contained test package. Its hooks use only stdlib Lua (`io`, `os`) and the injected `pkginfo` / `xvm` stubs from the prelude — no network, no real binary needed.

**Step 1: Create the directory**

```bash
mkdir -p tests/fixtures/pkgindex/pkgs/h
```

**Step 2: Write `hello.lua`**

```lua
package = {
    spec    = "1",
    name    = "hello",
    description = "Minimal fixture package for libxpkg tests",
    authors  = {"libxpkg-test"},
    licenses = {"MIT"},
    repo     = "https://github.com/mcpplibs/libxpkg",

    type     = "package",
    archs    = {"x86_64"},
    status   = "stable",
    categories = {"test"},
    keywords   = {"test", "fixture"},

    xvm_enable = true,

    xpm = {
        linux = {
            ["latest"] = { ref = "1.0.0" },
            ["1.0.0"]  = {
                url    = "https://example.com/hello-1.0.0-linux.tar.gz",
                sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
            },
        },
        windows = {
            ["latest"] = { ref = "1.0.0" },
            ["1.0.0"]  = {
                url    = "https://example.com/hello-1.0.0-windows.zip",
                sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
            },
        },
        macosx = {
            ["latest"] = { ref = "1.0.0" },
            ["1.0.0"]  = {
                url    = "https://example.com/hello-1.0.0-macosx.tar.gz",
                sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
            },
        },
    },
}

import("xim.libxpkg.pkginfo")
import("xim.libxpkg.xvm")

local MARKER = "hello.installed"

-- installed(): return version string if marker file exists, else nil
function installed()
    local dir = pkginfo.install_dir()
    if not dir then return nil end
    local marker = dir .. "/" .. MARKER
    if os.isfile(marker) then
        local f = io.open(marker, "r")
        if f then
            local ver = f:read("*l"); f:close()
            return ver or "1.0.0"
        end
        return "1.0.0"
    end
    return nil
end

-- install(): create install_dir and write marker file
function install()
    local dir = pkginfo.install_dir()
    if not dir then return false end
    os.execute("mkdir -p " .. dir)
    local f = io.open(dir .. "/" .. MARKER, "w")
    if not f then return false end
    f:write("1.0.0\n"); f:close()
    return true
end

-- config(): register with xvm
function config()
    xvm.add("hello")
    return true
end

-- uninstall(): remove marker and deregister from xvm
function uninstall()
    local dir = pkginfo.install_dir()
    if dir then os.remove(dir .. "/" .. MARKER) end
    xvm.remove("hello")
    return true
end
```

**Step 3: Verify fixture parses as valid Lua**

```bash
lua tests/fixtures/pkgindex/pkgs/h/hello.lua 2>&1 || true
# Expected: error about missing import() — that's fine, it means Lua parsed the syntax
# A syntax error would say "unexpected symbol near" — that would be a problem
```

**Step 4: Commit**

```bash
git add tests/fixtures/
git commit -m "test: add minimal hello fixture package for self-contained tests"
```

---

## Task 2: Inject fixture path define into `tests/xmake.lua`

**Files:**
- Modify: `tests/xmake.lua`

Both loader and executor test targets need to know the fixture path at compile time.

**Step 1: Edit `tests/xmake.lua` — add define to loader and executor targets**

Current content of `tests/xmake.lua`:
```lua
add_requires("gtest")

target("xpkg_model_test")
    set_kind("binary")
    add_files("main.cpp", "test_model.cpp")
    add_deps("mcpplibs-xpkg")
    add_packages("gtest")
    set_policy("build.c++.modules", true)

target("xpkg_loader_test")
    set_kind("binary")
    add_files("main.cpp", "test_loader.cppm")
    add_deps("mcpplibs-xpkg", "mcpplibs-xpkg-loader")
    add_packages("gtest", "mcpplibs-capi-lua")
    set_policy("build.c++.modules", true)

target("xpkg_index_test")
    set_kind("binary")
    add_files("main.cpp", "test_index.cpp")
    add_deps("mcpplibs-xpkg", "mcpplibs-xpkg-index")
    add_packages("gtest")
    set_policy("build.c++.modules", true)

target("xpkg_executor_test")
    set_kind("binary")
    add_files("main.cpp", "test_executor.cpp")
    add_deps("mcpplibs-xpkg", "mcpplibs-xpkg-executor")
    add_packages("gtest", "mcpplibs-capi-lua")
    set_policy("build.c++.modules", true)
```

Replace with:

```lua
add_requires("gtest")

-- Data model tests — only mcpplibs.xpkg, no external deps
target("xpkg_model_test")
    set_kind("binary")
    add_files("main.cpp", "test_model.cpp")
    add_deps("mcpplibs-xpkg")
    add_packages("gtest")
    set_policy("build.c++.modules", true)

-- Loader tests — mcpplibs.xpkg + mcpplibs.xpkg.loader (needs lua)
target("xpkg_loader_test")
    set_kind("binary")
    add_files("main.cpp", "test_loader.cppm")
    add_deps("mcpplibs-xpkg", "mcpplibs-xpkg-loader")
    add_packages("gtest", "mcpplibs-capi-lua")
    add_defines('XPKG_TEST_PKGINDEX="$(projectdir)/tests/fixtures/pkgindex"')
    set_policy("build.c++.modules", true)

-- Index tests — mcpplibs.xpkg + mcpplibs.xpkg.index, pure C++
target("xpkg_index_test")
    set_kind("binary")
    add_files("main.cpp", "test_index.cpp")
    add_deps("mcpplibs-xpkg", "mcpplibs-xpkg-index")
    add_packages("gtest")
    set_policy("build.c++.modules", true)

-- Executor tests — mcpplibs.xpkg + mcpplibs.xpkg.executor (needs lua)
target("xpkg_executor_test")
    set_kind("binary")
    add_files("main.cpp", "test_executor.cpp")
    add_deps("mcpplibs-xpkg", "mcpplibs-xpkg-executor")
    add_packages("gtest", "mcpplibs-capi-lua")
    add_defines('XPKG_TEST_PKGINDEX="$(projectdir)/tests/fixtures/pkgindex"')
    set_policy("build.c++.modules", true)
```

**Step 2: Verify xmake parses without error**

```bash
xmake l -c "print('ok')"
# Just check syntax didn't break the project
xmake build xpkg_model_test 2>&1 | tail -3
# Expected: build ok
```

**Step 3: Commit**

```bash
git add tests/xmake.lua
git commit -m "build: inject XPKG_TEST_PKGINDEX compile-time define into test targets"
```

---

## Task 3: Update `tests/test_loader.cppm` to use fixture

**Files:**
- Modify: `tests/test_loader.cppm`

Replace the hardcoded absolute path with the compile-time define. Also update test assertions to match `hello` package instead of `mdbook`.

**Step 1: Rewrite `tests/test_loader.cppm`**

```cpp
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
#  define XPKG_TEST_PKGINDEX "tests/fixtures/pkgindex"
#endif

static const fs::path PKGINDEX{ XPKG_TEST_PKGINDEX };

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
```

**Step 2: Build and run loader tests**

```bash
xmake build xpkg_loader_test 2>&1 | tail -5
xmake run xpkg_loader_test 2>&1
```

Expected output:
```
[  PASSED  ] 4 tests.
```

If build fails with "Bad file data" (stale BMI cache):
```bash
xmake clean --all && xmake build xpkg_loader_test 2>&1 | tail -5
```

**Step 3: Commit**

```bash
git add tests/test_loader.cppm
git commit -m "test: use local fixture instead of absolute xim-pkgindex path in loader tests"
```

---

## Task 4: Update `tests/test_executor.cpp` to use fixture

**Files:**
- Modify: `tests/test_executor.cpp`

Replace hardcoded mdbook path with hello fixture. Update the `HasHook_Installed_False` test — `hello.lua` **does** have an `installed()` hook (unlike mdbook), so rename it to `HasHook_Installed_True`.

**Step 1: Rewrite `tests/test_executor.cpp`**

```cpp
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
    // hello.lua has an installed() hook (unlike mdbook)
    EXPECT_TRUE(exec->has_hook(HookType::Installed));
}
```

**Step 2: Build and run executor tests**

```bash
xmake build xpkg_executor_test 2>&1 | tail -5
xmake run xpkg_executor_test 2>&1
```

Expected:
```
[  PASSED  ] 6 tests.
```

**Step 3: Commit**

```bash
git add tests/test_executor.cpp
git commit -m "test: switch executor tests from external mdbook to local hello fixture"
```

---

## Task 5: Scrub `/home/<user>/` from documentation files

**Files:**
- Modify: `docs/plans/2026-03-01-libxpkg-impl.md` (lines containing `/home/<user>/`)
- Modify: `.agents/plans/2026-03-01-libxpkg-design.md` (lines containing `/home/<user>/`)
- Modify: `.agents/docs/2026-03-01-gcc15-module-abi-issues.md` (if any)

**Step 1: Check exact occurrences**

```bash
grep -n "/home/speak" docs/plans/2026-03-01-libxpkg-impl.md
grep -n "/home/speak" .agents/plans/2026-03-01-libxpkg-design.md
grep -n "/home/speak" .agents/docs/2026-03-01-gcc15-module-abi-issues.md
```

**Step 2: Replace all `/home/<user>/workspace/github/d2learn/` occurrences**

Use sed to replace in all three files (substitution: obfuscate username to `<user>`):

```bash
find docs/ .agents/ -name "*.md" -exec \
  sed -i 's|/home/<user>/workspace/github/d2learn/|/home/<user>/workspace/github/d2learn/|g' {} \;

find docs/ .agents/ -name "*.md" -exec \
  sed -i 's|/home/<user>/workspace/github/mcpplibs/|/home/<user>/workspace/github/mcpplibs/|g' {} \;
```

**Step 3: Verify no `/home/<user>/` remains in any tracked file**

```bash
grep -rn "/home/speak" . --include="*.md" --include="*.cpp" --include="*.cppm" --include="*.lua" --include="*.txt"
# Expected: no output
```

**Step 4: Commit**

```bash
git add docs/ .agents/
git commit -m "privacy: replace absolute user paths with placeholder in documentation"
```

---

## Task 6: Create `examples/lifecycle.cpp`

**Files:**
- Create: `examples/lifecycle.cpp`

Demonstrates the full libxpkg API flow: load → index → search → executor → install → config → installed-check → uninstall.

The `ExecutionContext::install_dir` is set to a temp directory so the example is fully runnable without root or special setup. Temp dir is cleaned up at the end.

**Step 1: Write `examples/lifecycle.cpp`**

```cpp
import std;
import mcpplibs.xpkg;
import mcpplibs.xpkg.loader;
import mcpplibs.xpkg.index;
import mcpplibs.xpkg.executor;

namespace fs = std::filesystem;
using namespace mcpplibs::xpkg;

#ifndef XPKG_FIXTURES_DIR
#  define XPKG_FIXTURES_DIR "tests/fixtures"
#endif

static const fs::path PKGINDEX = fs::path(XPKG_FIXTURES_DIR) / "pkgindex";
static const fs::path HELLO_PKG = PKGINDEX / "pkgs/h/hello.lua";

int main() {
    std::println("=== libxpkg lifecycle demo ===\n");

    // ── 1. Load package metadata ─────────────────────────────────────────
    std::println("[ 1/6 ] load_package(\"hello.lua\")");
    auto pkg = load_package(HELLO_PKG);
    if (!pkg) {
        std::println("  ERROR: {}", pkg.error());
        return 1;
    }
    std::println("  name        : {}", pkg->name);
    std::println("  description : {}", pkg->description);
    std::println("  status      : {}",
        pkg->status == PackageStatus::Stable ? "stable" : "other");
    std::println("  xvm_enable  : {}", pkg->xvm_enable);
    std::println("  platforms   : {}", pkg->xpm.entries.size());

    // ── 2. Build index ────────────────────────────────────────────────────
    std::println("\n[ 2/6 ] build_index(pkgindex/)");
    auto idx = build_index(PKGINDEX);
    if (!idx) {
        std::println("  ERROR: {}", idx.error());
        return 1;
    }
    std::println("  total entries : {}", idx->entries.size());

    // ── 3. Search ─────────────────────────────────────────────────────────
    std::println("\n[ 3/6 ] search(index, \"hello\")");
    auto hits = search(*idx, "hello");
    std::println("  found {} result(s):", hits.size());
    for (auto& h : hits)
        std::println("    - {}", h);

    // ── 4. Create executor ────────────────────────────────────────────────
    std::println("\n[ 4/6 ] create_executor(\"hello.lua\")");
    auto exec = create_executor(HELLO_PKG);
    if (!exec) {
        std::println("  ERROR: {}", exec.error());
        return 1;
    }
    std::println("  hooks present:");
    std::println("    installed : {}", exec->has_hook(HookType::Installed));
    std::println("    install   : {}", exec->has_hook(HookType::Install));
    std::println("    config    : {}", exec->has_hook(HookType::Config));
    std::println("    uninstall : {}", exec->has_hook(HookType::Uninstall));

    // ── Prepare ExecutionContext with a temp install dir ──────────────────
    auto install_dir = fs::temp_directory_path() / "xpkg-lifecycle-demo" / "hello" / "1.0.0";
    fs::create_directories(install_dir);

    ExecutionContext ctx;
    ctx.pkg_name    = "hello";
    ctx.version     = "1.0.0";
    ctx.platform    = "linux";
    ctx.arch        = "x86_64";
    ctx.install_dir = install_dir;
    ctx.run_dir     = fs::temp_directory_path() / "xpkg-lifecycle-demo";
    ctx.xpkg_dir    = fs::temp_directory_path() / "xpkg-lifecycle-demo";
    ctx.bin_dir     = fs::temp_directory_path() / "xpkg-lifecycle-demo" / "bin";

    // ── 5. Install ────────────────────────────────────────────────────────
    std::println("\n[ 5/6 ] run_hook(Install)");
    auto r_install = exec->run_hook(HookType::Install, ctx);
    std::println("  success : {}", r_install.success);
    if (!r_install.error.empty())
        std::println("  error   : {}", r_install.error);

    // ── 5b. check_installed ───────────────────────────────────────────────
    std::println("\n[  +  ] check_installed()");
    auto r_check = exec->check_installed(ctx);
    std::println("  success : {}", r_check.success);
    std::println("  version : {}", r_check.version.empty() ? "(not installed)" : r_check.version);

    // ── 5c. Config ────────────────────────────────────────────────────────
    std::println("\n[  +  ] run_hook(Config)");
    auto r_config = exec->run_hook(HookType::Config, ctx);
    std::println("  success : {}", r_config.success);

    // ── 6. Uninstall ──────────────────────────────────────────────────────
    std::println("\n[ 6/6 ] run_hook(Uninstall)");
    auto r_uninst = exec->run_hook(HookType::Uninstall, ctx);
    std::println("  success : {}", r_uninst.success);
    if (!r_uninst.error.empty())
        std::println("  error   : {}", r_uninst.error);

    // ── Cleanup ───────────────────────────────────────────────────────────
    std::error_code ec;
    fs::remove_all(fs::temp_directory_path() / "xpkg-lifecycle-demo", ec);

    std::println("\n=== done ===");
    return 0;
}
```

**Step 2: Verify file was created**

```bash
wc -l examples/lifecycle.cpp
# Expected: ~80 lines
```

**Step 3: Commit (before building — build fix comes in next task)**

```bash
git add examples/lifecycle.cpp
git commit -m "example: add lifecycle demo showing full load/index/install/uninstall flow"
```

---

## Task 7: Update `examples/xmake.lua` and build lifecycle

**Files:**
- Modify: `examples/xmake.lua`

Add the `lifecycle` target with all required module deps and the `XPKG_FIXTURES_DIR` define.

**Step 1: Rewrite `examples/xmake.lua`**

Current content:
```lua
add_rules("mode.debug", "mode.release")

set_languages("c++23")

target("basic")
    set_kind("binary")
    add_files("basic.cpp")
    add_deps("mcpplibs-xpkg")
    set_policy("build.c++.modules", true)
```

Replace with:

```lua
add_rules("mode.debug", "mode.release")

set_languages("c++23")

target("basic")
    set_kind("binary")
    add_files("basic.cpp")
    add_deps("mcpplibs-xpkg")
    set_policy("build.c++.modules", true)

-- Full lifecycle demo: load → index → search → executor → install → uninstall
target("lifecycle")
    set_kind("binary")
    add_files("lifecycle.cpp")
    add_deps("mcpplibs-xpkg", "mcpplibs-xpkg-loader",
             "mcpplibs-xpkg-index", "mcpplibs-xpkg-executor")
    add_packages("mcpplibs-capi-lua")
    add_defines('XPKG_FIXTURES_DIR="$(projectdir)/tests/fixtures"')
    set_policy("build.c++.modules", true)
```

**Step 2: Build lifecycle example**

```bash
xmake build lifecycle 2>&1 | tail -10
```

Expected: `build ok`

If stale cache errors appear:
```bash
xmake clean --all && xmake build lifecycle 2>&1 | tail -10
```

**Step 3: Run lifecycle example**

```bash
xmake run lifecycle 2>&1
```

Expected output (success indicators):
```
=== libxpkg lifecycle demo ===

[ 1/6 ] load_package("hello.lua")
  name        : hello
  ...
[ 5/6 ] run_hook(Install)
  success : true
[  +  ] check_installed()
  success : true
  version : 1.0.0
[ 6/6 ] run_hook(Uninstall)
  success : true

=== done ===
```

**Step 4: Run all test suites to confirm nothing regressed**

```bash
xmake run xpkg_model_test 2>&1 | tail -3
xmake run xpkg_loader_test 2>&1 | tail -3
xmake run xpkg_index_test 2>&1 | tail -3
xmake run xpkg_executor_test 2>&1 | tail -3
```

Expected: all show `[  PASSED  ]`.

**Step 5: Commit**

```bash
git add examples/xmake.lua
git commit -m "example: add lifecycle target with all module deps and fixture path define"
```

---

## Verification checklist

After all tasks complete:

```bash
# No hardcoded /home/<user> in any source/test/build file
grep -rn "/home/" . --include="*.cpp" --include="*.cppm" --include="*.lua" \
     --exclude-dir=".git" --exclude-dir=".xmake" --exclude-dir="build"
# Expected: no output

# All tests pass
xmake run xpkg_model_test && xmake run xpkg_loader_test && \
xmake run xpkg_index_test && xmake run xpkg_executor_test
# Expected: PASSED x4

# Example runs end-to-end
xmake run lifecycle
# Expected: success : true at each hook step
```
