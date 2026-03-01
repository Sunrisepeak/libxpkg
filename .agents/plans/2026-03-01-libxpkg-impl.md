# libxpkg Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement libxpkg — a C++23 library for parsing, indexing, and executing xpkg Lua packages — with a bundled Lua runtime stdlib for hook execution compatibility with xim-pkgindex.

**Architecture:** Four C++23 submodules (`mcpplibs.xpkg`, `.loader`, `.index`, `.executor`). The executor bundles a Lua stdlib (`src/lua-stdlib/`) embedded as C++ string literals, providing `xim.libxpkg.*` API compatibility and a compatibility layer for xmake-extended `os.*`/`path.*` functions.

**Tech Stack:** C++23 modules, Lua 5.4, mcpplibs.capi.lua (from mcpplibs/lua), xmake, gtest

**Reference:**
- Design doc: `.agents/plans/2026-03-01-libxpkg-design.md`
- Real packages: `/home/<user>/workspace/github/d2learn/xim-pkgindex/pkgs/`
- xim Lua sources: `/home/<user>/workspace/github/d2learn/xlings/core/xim/libxpkg/`

---

## Phase 0: Project Bootstrap

### Task 0: Replace template boilerplate, set up xmake targets

**Files:**
- Modify: `xmake.lua`
- Modify: `tests/xmake.lua`
- Modify: `tests/main.cpp`
- Delete: `src/templates.cppm` (will be replaced by new modules)

**Step 1: Update `xmake.lua`**

Replace current content with:

```lua
add_rules("mode.release", "mode.debug")
set_languages("c++23")

add_requires("lua")

-- NOTE: mcpplibs-capi-lua is a local dependency.
-- Add its path via: xmake f --lua_path=/path/to/mcpplibs/lua
-- Or add as a git submodule at deps/lua/ and uncomment:
-- add_requires("mcpplibs-capi-lua", {local = true})
-- For now, use a local includes() if available:
local lua_dep_path = os.getenv("MCPPLIBS_LUA_PATH") or "../lua"
if os.isdir(lua_dep_path) then
    includes(lua_dep_path)
end

-- Data model (zero external deps)
target("mcpplibs-xpkg")
    set_kind("static")
    add_files("src/xpkg.cppm", {public = true, install = true})
    set_policy("build.c++.modules", true)

-- Loader (depends on lua)
target("mcpplibs-xpkg-loader")
    set_kind("static")
    add_deps("mcpplibs-xpkg", "mcpplibs-capi-lua")
    add_packages("lua", {public = true})
    add_files("src/xpkg-loader.cppm", {public = true, install = true})
    set_policy("build.c++.modules", true)

-- Index (pure C++)
target("mcpplibs-xpkg-index")
    set_kind("static")
    add_deps("mcpplibs-xpkg")
    add_files("src/xpkg-index.cppm", {public = true, install = true})
    set_policy("build.c++.modules", true)

-- Executor (depends on lua)
target("mcpplibs-xpkg-executor")
    set_kind("static")
    add_deps("mcpplibs-xpkg", "mcpplibs-capi-lua")
    add_packages("lua", {public = true})
    add_files("src/xpkg-executor.cppm", {public = true, install = true})
    set_policy("build.c++.modules", true)

if not is_host("macosx") then
    includes("examples", "tests")
end
```

**Step 2: Update `tests/xmake.lua`**

```lua
add_rules("mode.debug", "mode.release")
set_languages("c++23")
add_requires("gtest")

target("xpkg_test")
    set_kind("binary")
    add_files("*.cpp")
    add_deps("mcpplibs-xpkg", "mcpplibs-xpkg-loader",
             "mcpplibs-xpkg-index", "mcpplibs-xpkg-executor")
    add_packages("gtest")
    set_policy("build.c++.modules", true)
```

**Step 3: Clear tests/main.cpp to a minimal placeholder**

```cpp
#include <gtest/gtest.h>

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
```

**Step 4: Delete src/templates.cppm**

```bash
rm /path/to/libxpkg/src/templates.cppm
```

**Step 5: Commit**

```bash
git add xmake.lua tests/xmake.lua tests/main.cpp
git rm src/templates.cppm
git commit -m "chore: bootstrap project layout for libxpkg"
```

---

## Phase 1: Data Model

### Task 1: xpkg.cppm — core data structures

**Files:**
- Create: `src/xpkg.cppm`

**Step 1: Write `src/xpkg.cppm`**

```cpp
module;
export module mcpplibs.xpkg;
import std;

export namespace mcpplibs::xpkg {

enum class PackageType   { Package, Script, Template, Config };
enum class PackageStatus { Dev, Stable, Deprecated };

struct PlatformResource {
    std::string url;
    std::string sha256;
    std::string ref;  // "latest" -> "1.0.0"
};

struct PlatformMatrix {
    // platform -> version -> resource
    std::unordered_map<std::string,
        std::unordered_map<std::string, PlatformResource>> entries;
    std::unordered_map<std::string, std::vector<std::string>> deps;
    std::unordered_map<std::string, std::string> inherits;
};

struct Package {
    std::string spec;
    std::string name;
    std::string description;
    PackageType type     = PackageType::Package;
    PackageStatus status = PackageStatus::Dev;
    std::string namespace_;
    std::string homepage, repo, docs;
    std::vector<std::string> authors, maintainers, licenses;
    std::vector<std::string> categories, keywords, programs, archs;
    bool xvm_enable = false;
    PlatformMatrix xpm;
};

struct IndexEntry {
    std::string name;        // "vscode@1.85.0"
    std::string version;
    std::filesystem::path path;
    PackageType type = PackageType::Package;
    std::string description;
    bool installed = false;
    std::string ref;
};

struct PackageIndex {
    std::unordered_map<std::string, IndexEntry> entries;
    std::unordered_map<std::string, std::vector<std::string>> mutex_groups;
};

struct RepoConfig {
    std::string name;
    std::string url_global, url_cn;
    std::filesystem::path local_path;
};

struct IndexRepos {
    RepoConfig main_repo;
    std::vector<RepoConfig> sub_repos;
};

} // namespace mcpplibs::xpkg
```

**Step 2: Write a smoke test in `tests/test_model.cpp`**

```cpp
#include <gtest/gtest.h>
import mcpplibs.xpkg;
using namespace mcpplibs::xpkg;

TEST(ModelTest, DefaultPackage) {
    Package p;
    EXPECT_EQ(p.status, PackageStatus::Dev);
    EXPECT_FALSE(p.xvm_enable);
}

TEST(ModelTest, IndexEntry) {
    IndexEntry e;
    e.name = "vscode@1.85.0";
    e.version = "1.85.0";
    EXPECT_FALSE(e.installed);
}
```

**Step 3: Add test file to `tests/xmake.lua`** (add `"test_model.cpp"` to add_files)

**Step 4: Build and run tests**

```bash
xmake build mcpplibs-xpkg
xmake run xpkg_test
```
Expected: 2 tests pass.

**Step 5: Commit**

```bash
git add src/xpkg.cppm tests/test_model.cpp tests/xmake.lua
git commit -m "feat: add xpkg data model (mcpplibs.xpkg)"
```

---

## Phase 2: Lua Stdlib

> These Lua files are the xpkg runtime stdlib bundled with libxpkg.
> They will be embedded in the executor as C++ string literals.

### Task 2: prelude.lua — compatibility layer + import()

**Files:**
- Create: `src/lua-stdlib/prelude.lua`

This file must be loaded first, before any package script. It provides:
1. `import()` function compatible with `import("xim.libxpkg.*")`
2. `os.*` xmake extensions (`trymv`, `isdir`, `isfile`, `host`, `dirs`)
3. `path.*` module (`join`, `filename`, `directory`, `is_absolute`)
4. `io.readfile` / `io.writefile`
5. `cprint()` (colors stripped)
6. `try/catch` block pattern

**Step 1: Write `src/lua-stdlib/prelude.lua`**

```lua
-- prelude.lua: xmake compatibility layer + import() for libxpkg runtime
-- Loaded by PackageExecutor before any package script.

-- _LIBXPKG_MODULES is populated by C++ before this file runs:
-- each module is dostring'd and the returned table stored here.
_LIBXPKG_MODULES = _LIBXPKG_MODULES or {}

-- import(): maps "xim.libxpkg.X" to preloaded modules
function import(mod_path)
    local name = mod_path:match("xim%.libxpkg%.(.+)")
    if name and _LIBXPKG_MODULES[name] then
        return _LIBXPKG_MODULES[name]
    end
    -- Stub for unknown imports (base.runtime etc.)
    return setmetatable({}, {
        __index = function(_, k) return function(...) end end
    })
end

-- os.* extensions (xmake compat)
local _os_orig_execute = os.execute
os.isfile = function(p)
    local f = io.open(p, "r")
    if f then f:close() return true end
    return false
end
os.isdir = function(p)
    -- portable dir check via os.execute
    local sep = package.config:sub(1,1)
    if sep == "\\" then
        return os.execute('if exist "' .. p .. '\\" exit 0') == 0
    else
        return os.execute('[ -d "' .. p .. '" ]') == 0
    end
end
os.host = function()
    return _RUNTIME and _RUNTIME.platform or "linux"
end
os.trymv = function(src, dst)
    local ok = pcall(os.rename, src, dst)
    if not ok then
        -- fallback: copy + remove
        local inf = io.open(src, "rb")
        if not inf then return false end
        local content = inf:read("*a"); inf:close()
        local outf = io.open(dst, "wb")
        if not outf then return false end
        outf:write(content); outf:close()
        os.remove(src)
    end
    return true
end
os.mv = function(src, dst) return os.trymv(src, dst) end
os.cp = function(src, dst)
    local inf = io.open(src, "rb")
    if not inf then return false end
    local content = inf:read("*a"); inf:close()
    local outf = io.open(dst, "wb")
    if not outf then return false end
    outf:write(content); outf:close()
    return true
end
os.dirs = function(pattern)
    -- Returns list of directories matching shell glob pattern
    local result = {}
    local f = io.popen('ls -d ' .. pattern .. ' 2>/dev/null')
    if f then
        for line in f:lines() do
            if os.isdir(line) then table.insert(result, line) end
        end
        f:close()
    end
    return result
end
os.sleep = function(ms) -- stub, sleep not critical
end

-- path module
path = {}
path.join = function(...)
    local parts = {...}
    local sep = "/"
    local result = parts[1] or ""
    for i = 2, #parts do
        if parts[i] and parts[i] ~= "" then
            result = result:gsub("[/\\]+$", "") .. sep .. parts[i]
        end
    end
    return result
end
path.filename = function(p)
    return (p or ""):match("[^/\\]+$") or ""
end
path.directory = function(p)
    return (p or ""):match("^(.*)[/\\][^/\\]+$") or ""
end
path.is_absolute = function(p)
    return (p or ""):sub(1,1) == "/" or (p or ""):match("^%a:[/\\]") ~= nil
end

-- io extensions
io.readfile = function(p)
    local f = io.open(p, "r")
    if not f then return nil end
    local content = f:read("*a"); f:close()
    return content
end
io.writefile = function(p, content)
    local f = io.open(p, "w")
    if not f then return false end
    f:write(content); f:close()
    return true
end

-- cprint: strip color markers, fallback to print
cprint = function(fmt, ...)
    if type(fmt) == "string" then
        fmt = fmt:gsub("%${%w+}", "")
        -- handle varargs safely
        local ok, msg = pcall(string.format, fmt, ...)
        print(ok and msg or fmt)
    else
        print(fmt)
    end
end

-- string extensions (xmake compat)
if not string.split then
    function string.split(s, sep, plain)
        local result = {}
        local pattern = plain and sep or sep
        local i = 1
        while true do
            local j, k = s:find(pattern, i, plain)
            if not j then
                table.insert(result, s:sub(i))
                break
            end
            table.insert(result, s:sub(i, j-1))
            i = k + 1
        end
        return result
    end
end

-- try/catch: simulates xmake try { function, catch { function } } syntax
function try(block)
    local fn = block[1]
    local catch_block = block.catch
    local ok, err = pcall(fn)
    if not ok then
        if catch_block and catch_block[1] then
            catch_block[1](err)
        end
        return nil
    end
    return err
end
```

**Step 2: Manual verify** — run the Lua file standalone to check no syntax errors:

```bash
lua src/lua-stdlib/prelude.lua
```
Expected: no output, no errors.

**Step 3: Commit**

```bash
git add src/lua-stdlib/prelude.lua
git commit -m "feat: add lua stdlib prelude (os/path/io compat + import)"
```

---

### Task 3: log.lua

**Files:**
- Create: `src/lua-stdlib/xim/libxpkg/log.lua`

**Step 1: Write `src/lua-stdlib/xim/libxpkg/log.lua`**

```lua
-- xim.libxpkg.log: logging API for xpkg scripts
local M = {}

local PREFIX = "[xim:xpkg]: "

local function _log(level, color, text, ...)
    if not text then return end
    local msg
    local ok, formatted = pcall(string.format, text, ...)
    msg = ok and formatted or text
    -- strip ${color} markers if cprint not available
    msg = msg:gsub("%${%w+}", "")
    io.write(PREFIX .. msg .. "\n")
    io.flush()
end

function M.info(text, ...)  _log("INFO",  "green",  text, ...) end
function M.debug(text, ...) _log("DEBUG", "bright", text, ...) end
function M.warn(text, ...)  _log("WARN",  "yellow", text, ...) end
function M.error(text, ...) _log("ERROR", "red",    text, ...) end

return M
```

**Step 2: Verify syntax**

```bash
lua -e "dofile('src/lua-stdlib/xim/libxpkg/log.lua')"
```

**Step 3: Commit**

```bash
git add src/lua-stdlib/xim/libxpkg/log.lua
git commit -m "feat: add lua stdlib log module"
```

---

### Task 4: pkginfo.lua

**Files:**
- Create: `src/lua-stdlib/xim/libxpkg/pkginfo.lua`

**Step 1: Write `src/lua-stdlib/xim/libxpkg/pkginfo.lua`**

```lua
-- xim.libxpkg.pkginfo: package info API for xpkg scripts
-- Reads from _RUNTIME table injected by C++ PackageExecutor.
local M = {}

function M.name()         return _RUNTIME.pkg_name end
function M.version()      return _RUNTIME.version end
function M.install_file() return _RUNTIME.install_file end
function M.deps_list()    return _RUNTIME.deps_list or {} end

local function _ends_with(s, suffix)
    return suffix == "" or s:sub(-#suffix) == suffix
end

-- Scan xpkg_dir for a dependency's install directory
local function _resolve_dep_via_scan(dep_name, dep_version)
    local base = _RUNTIME.xpkg_dir
    if not base then return nil end
    local dirs = os.dirs(path.join(base, "*")) or {}
    for _, dep_root in ipairs(dirs) do
        local dirname = path.filename(dep_root)
        if dirname == dep_name or _ends_with(dirname, "-x-" .. dep_name) then
            local ver = dep_version
            if not ver then
                local vers = os.dirs(path.join(dep_root, "*")) or {}
                table.sort(vers)
                if #vers > 0 then ver = path.filename(vers[#vers]) end
            end
            if ver then
                local install_dir = path.join(dep_root, ver)
                if os.isdir(install_dir) then return install_dir end
            end
        end
    end
    return nil
end

function M.dep_install_dir(dep_name, dep_version)
    return _resolve_dep_via_scan(dep_name, dep_version)
end

function M.install_dir(pkgname, pkgversion)
    if not pkgname then
        return _RUNTIME.install_dir
    end
    local dir = M.dep_install_dir(pkgname, pkgversion)
    if dir then return dir end
    io.write(string.format("[xim:xpkg]: cannot get install dir for %s@%s\n",
        tostring(pkgname), tostring(pkgversion or "latest")))
    return nil
end

return M
```

**Step 2: Verify syntax**

```bash
lua -e "
_RUNTIME = { pkg_name='test', version='1.0', install_dir='/tmp/test',
             install_file='/tmp/test.tar.gz', xpkg_dir='/tmp/xpkgs',
             deps_list={} }
-- load prelude first for os.*/path.*
dofile('src/lua-stdlib/prelude.lua')
local m = dofile('src/lua-stdlib/xim/libxpkg/pkginfo.lua')
print(m.name())    -- test
print(m.version()) -- 1.0
"
```

**Step 3: Commit**

```bash
git add src/lua-stdlib/xim/libxpkg/pkginfo.lua
git commit -m "feat: add lua stdlib pkginfo module"
```

---

### Task 5: system.lua

**Files:**
- Create: `src/lua-stdlib/xim/libxpkg/system.lua`

**Step 1: Write `src/lua-stdlib/xim/libxpkg/system.lua`**

```lua
-- xim.libxpkg.system: system operations API
local M = {}

function M.exec(cmd, opt)
    opt = opt or {}
    if opt.retry then
        local retries = opt.retry
        while retries > 0 do
            local ok = os.execute(cmd)
            if ok == 0 or ok == true then return end
            retries = retries - 1
        end
    end
    local ret = os.execute(cmd)
    if ret ~= 0 and ret ~= true then
        error("exec failed: " .. tostring(cmd))
    end
end

function M.rundir()   return _RUNTIME.run_dir end
function M.xpkgdir()  return _RUNTIME.xpkg_dir end
function M.bindir()   return _RUNTIME.bin_dir end
function M.xpkg_args() return _RUNTIME.args or {} end
function M.subos_sysrootdir() return _RUNTIME.subos_sysrootdir end

function M.run_in_script(content, admin)
    local tmpfile = os.tmpname() .. ".sh"
    io.writefile(tmpfile, content)
    os.execute("chmod +x " .. tmpfile)
    local prefix = (admin == true) and "sudo " or ""
    os.execute(prefix .. tmpfile)
    os.remove(tmpfile)
end

function M.unix_api()
    return {
        append_to_shell_profile = function(config)
            if not config then return end
            if type(config) == "string" then
                config = { posix = config, fish = config }
            end
            local profile_dir = _RUNTIME.run_dir or "/tmp"
            local posix = path.join(profile_dir, "xlings-profile.sh")
            local fish  = path.join(profile_dir, "xlings-profile.fish")
            if config.posix and os.isfile(posix) then
                local cur = io.readfile(posix) or ""
                if not cur:find(config.posix, 1, true) then
                    io.writefile(posix, cur .. "\n" .. config.posix)
                end
            end
            if config.fish and os.isfile(fish) then
                local cur = io.readfile(fish) or ""
                if not cur:find(config.fish, 1, true) then
                    io.writefile(fish, cur .. "\n" .. config.fish)
                end
            end
        end
    }
end

return M
```

**Step 2: Verify syntax**

```bash
lua -e "dofile('src/lua-stdlib/prelude.lua'); print(dofile('src/lua-stdlib/xim/libxpkg/system.lua'))"
```

**Step 3: Commit**

```bash
git add src/lua-stdlib/xim/libxpkg/system.lua
git commit -m "feat: add lua stdlib system module"
```

---

### Task 6: xvm.lua and utils.lua

**Files:**
- Create: `src/lua-stdlib/xim/libxpkg/xvm.lua`
- Create: `src/lua-stdlib/xim/libxpkg/utils.lua`

**Step 1: Write `src/lua-stdlib/xim/libxpkg/xvm.lua`**

```lua
-- xim.libxpkg.xvm: version management integration (calls xvm CLI)
local M = {}
local _log_enabled = true

local function _xvm(...)
    local args = {...}
    local cmd = "xvm " .. table.concat(args, " ")
    if _log_enabled then
        io.write("[xim:xpkg]: xvm " .. table.concat(args, " ") .. "\n")
    end
    return os.execute(cmd)
end

function M.add(name, opt)
    opt = opt or {}
    local ver    = opt.version or (_RUNTIME and _RUNTIME.version) or ""
    local bindir = opt.bindir  or (_RUNTIME and _RUNTIME.install_dir) or ""
    local args = {"add", name, "--version=" .. ver, "--bindir=" .. bindir}
    if opt.alias    then table.insert(args, "--alias=" .. opt.alias) end
    if opt.type     then table.insert(args, "--type="  .. opt.type)  end
    _xvm(table.unpack(args))
end

function M.remove(name, version)
    _xvm("remove", name, version or "")
end

function M.use(name, version)
    _xvm("use", name, version or "")
end

function M.has(name, version)
    local ret = os.execute("xvm has " .. name .. " " .. (version or ""))
    return ret == 0 or ret == true
end

function M.info(name, version)
    -- Returns stub; real xvm query not yet implemented
    return nil
end

function M.log_tag(enable)
    local old = _log_enabled
    _log_enabled = enable
    return old
end

return M
```

**Step 2: Write `src/lua-stdlib/xim/libxpkg/utils.lua`**

```lua
-- xim.libxpkg.utils: utility functions
local M = {}

function M.filepath_to_absolute(filepath)
    if path.is_absolute(filepath) then return filepath end
    return path.join(os.getenv("PWD") or ".", filepath)
end

function M.try_download_and_check(url, dir, sha256)
    -- Basic download via curl/wget; sha256 check via sha256sum
    local filename = url:match("[^/]+$") or "download"
    local dest = path.join(dir, filename)
    local ret = os.execute(string.format("curl -fsSL -o %s %s", dest, url))
    if ret ~= 0 and ret ~= true then
        io.write("[xim:xpkg]: download failed: " .. url .. "\n")
        return false
    end
    if sha256 then
        local f = io.popen("sha256sum " .. dest)
        local out = f and f:read("*l") or ""
        if f then f:close() end
        local actual = out:match("^(%x+)")
        if actual ~= sha256 then
            io.write("[xim:xpkg]: sha256 mismatch for " .. dest .. "\n")
            return false
        end
    end
    return true
end

function M.input_args_process(cmds_kv, args)
    local result = {}
    for _, arg in ipairs(args or {}) do
        local k, v = arg:match("^%-%-(%w+)=(.+)$")
        if k then result[k] = v end
    end
    return result
end

return M
```

**Step 3: Verify both files**

```bash
lua -e "dofile('src/lua-stdlib/prelude.lua'); print(dofile('src/lua-stdlib/xim/libxpkg/xvm.lua'))"
lua -e "dofile('src/lua-stdlib/prelude.lua'); print(dofile('src/lua-stdlib/xim/libxpkg/utils.lua'))"
```

**Step 4: Commit**

```bash
git add src/lua-stdlib/xim/libxpkg/xvm.lua src/lua-stdlib/xim/libxpkg/utils.lua
git commit -m "feat: add lua stdlib xvm and utils modules"
```

---

## Phase 3: Executor

### Task 7: lua_stdlib_embed.hpp — embed Lua files as C++ strings

**Files:**
- Create: `src/lua_stdlib_embed.hpp`

This file is generated from the Lua stdlib files. For the initial implementation, maintain it manually. Later a pre-build script can auto-regenerate it.

**Step 1: Write a generator script `scripts/gen_lua_embed.sh`**

```bash
#!/bin/bash
# scripts/gen_lua_embed.sh
# Generates src/lua_stdlib_embed.hpp from src/lua-stdlib/*.lua files

OUT="src/lua_stdlib_embed.hpp"
echo "// Auto-generated by scripts/gen_lua_embed.sh — do not edit manually" > $OUT
echo "#pragma once" >> $OUT
echo "#include <string_view>" >> $OUT
echo "" >> $OUT
echo "namespace mcpplibs::xpkg::detail {" >> $OUT

embed_file() {
    local varname="$1"
    local filepath="$2"
    echo "" >> $OUT
    echo "inline constexpr std::string_view ${varname} = R\"__LUA__(" >> $OUT
    cat "$filepath" >> $OUT
    echo ")__LUA__\";" >> $OUT
}

embed_file "prelude_lua"    "src/lua-stdlib/prelude.lua"
embed_file "log_lua"        "src/lua-stdlib/xim/libxpkg/log.lua"
embed_file "pkginfo_lua"    "src/lua-stdlib/xim/libxpkg/pkginfo.lua"
embed_file "system_lua"     "src/lua-stdlib/xim/libxpkg/system.lua"
embed_file "xvm_lua"        "src/lua-stdlib/xim/libxpkg/xvm.lua"
embed_file "utils_lua"      "src/lua-stdlib/xim/libxpkg/utils.lua"

echo "" >> $OUT
echo "} // namespace mcpplibs::xpkg::detail" >> $OUT
echo "Generated: $OUT"
```

**Step 2: Run the generator**

```bash
mkdir -p scripts
chmod +x scripts/gen_lua_embed.sh
bash scripts/gen_lua_embed.sh
```

Expected: `src/lua_stdlib_embed.hpp` created with 6 string_view constants.

**Step 3: Add xmake pre-build rule** to `xmake.lua` (add to executor target):

```lua
target("mcpplibs-xpkg-executor")
    ...
    before_build(function(target)
        os.exec("bash scripts/gen_lua_embed.sh")
    end)
```

**Step 4: Commit**

```bash
git add scripts/gen_lua_embed.sh src/lua_stdlib_embed.hpp xmake.lua
git commit -m "feat: add lua stdlib embed generator"
```

---

### Task 8: xpkg-executor.cppm — PackageExecutor

**Files:**
- Create: `src/xpkg-executor.cppm`

**Step 1: Write failing test in `tests/test_executor.cpp`**

```cpp
#include <gtest/gtest.h>
import mcpplibs.xpkg;
import mcpplibs.xpkg.executor;
using namespace mcpplibs::xpkg;
namespace fs = std::filesystem;

// Path to a real package from xim-pkgindex (must exist on test machine)
// Set via environment variable XPKG_TEST_PKG or use fallback
static fs::path test_pkg_path() {
    if (auto* p = std::getenv("XPKG_TEST_PKG")) return p;
    return "/home/<user>/workspace/github/d2learn/xim-pkgindex/pkgs/m/mdbook.lua";
}

TEST(ExecutorTest, CreateExecutor_ExistingFile) {
    auto result = create_executor(test_pkg_path());
    EXPECT_TRUE(result.has_value()) << result.error();
}

TEST(ExecutorTest, CreateExecutor_MissingFile) {
    auto result = create_executor("/nonexistent/path.lua");
    EXPECT_FALSE(result.has_value());
}

TEST(ExecutorTest, HasHook_Install) {
    auto exec = create_executor(test_pkg_path());
    ASSERT_TRUE(exec.has_value());
    EXPECT_TRUE(exec->has_hook(HookType::Install));
}

TEST(ExecutorTest, HasHook_Installed_NotRequired) {
    auto exec = create_executor(test_pkg_path());
    ASSERT_TRUE(exec.has_value());
    // mdbook.lua doesn't have installed(), so this may be false
    // Just verify the call doesn't crash
    (void)exec->has_hook(HookType::Installed);
}
```

**Step 2: Run test to verify it fails** (executor not implemented yet)

```bash
xmake build xpkg_test 2>&1 | head -20
```
Expected: compile error — `mcpplibs.xpkg.executor` module not found.

**Step 3: Write `src/xpkg-executor.cppm`**

```cpp
module;

#include "lua_stdlib_embed.hpp"  // generated lua strings
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

export module mcpplibs.xpkg.executor;
import mcpplibs.xpkg;
import mcpplibs.capi.lua;
import std;

namespace lua = mcpplibs::capi::lua;
namespace fs  = std::filesystem;

export namespace mcpplibs::xpkg {

// --- Public types (already declared in design) ---

struct ExecutionContext {
    std::string pkg_name, version, platform, arch;
    fs::path install_file, install_dir;
    fs::path run_dir, xpkg_dir, bin_dir;
    std::vector<std::string> deps_list, args;
    std::string subos_sysrootdir;
};

struct HookResult {
    bool success = false;
    std::string output, error;
    std::string version;
};

enum class HookType { Installed, Build, Install, Config, Uninstall };

// --- Implementation detail ---

namespace detail {

constexpr std::string_view hook_name(HookType h) {
    switch (h) {
        case HookType::Installed:  return "installed";
        case HookType::Build:      return "build";
        case HookType::Install:    return "install";
        case HookType::Config:     return "config";
        case HookType::Uninstall:  return "uninstall";
    }
    return "";
}

// Push a string field onto the table at stack top
void push_string_field(lua::State* L, std::string_view key, std::string_view val) {
    lua::pushstring(L, std::string(val).c_str());
    lua::setfield(L, -2, std::string(key).c_str());
}

// Load and pre-register all xim.libxpkg.* modules into _LIBXPKG_MODULES
bool load_stdlib(lua::State* L, std::string& err) {
    // Create _LIBXPKG_MODULES table
    lua::newtable(L);
    lua::setglobal(L, "_LIBXPKG_MODULES");

    struct ModEntry { std::string_view name; std::string_view src; };
    const ModEntry mods[] = {
        { "log",        detail::log_lua     },
        { "pkginfo",    detail::pkginfo_lua  },
        { "system",     detail::system_lua  },
        { "xvm",        detail::xvm_lua     },
        { "utils",      detail::utils_lua   },
    };

    for (auto& m : mods) {
        // dostring(src) → leaves return value on stack
        if (lua::L_dostring(L, std::string(m.src).c_str()) != lua::OK) {
            err = "failed to load module " + std::string(m.name) + ": "
                + lua::tostring(L, -1);
            lua::pop(L, 1);
            return false;
        }
        // _LIBXPKG_MODULES[name] = returned table
        lua::getglobal(L, "_LIBXPKG_MODULES");
        lua::insert(L, -2);  // move module table below _LIBXPKG_MODULES
        lua::setfield(L, -2, std::string(m.name).c_str());
        lua::pop(L, 1);  // pop _LIBXPKG_MODULES
    }

    // Load prelude (defines import(), os.*, path.*, etc.)
    if (lua::L_dostring(L, std::string(detail::prelude_lua).c_str()) != lua::OK) {
        err = "failed to load prelude: " + std::string(lua::tostring(L, -1));
        lua::pop(L, 1);
        return false;
    }
    return true;
}

// Inject ExecutionContext into Lua as _RUNTIME global table
void inject_context(lua::State* L, const ExecutionContext& ctx) {
    lua::newtable(L);
    push_string_field(L, "pkg_name",         ctx.pkg_name);
    push_string_field(L, "version",           ctx.version);
    push_string_field(L, "platform",          ctx.platform);
    push_string_field(L, "arch",              ctx.arch);
    push_string_field(L, "install_file",      ctx.install_file.string());
    push_string_field(L, "install_dir",       ctx.install_dir.string());
    push_string_field(L, "run_dir",           ctx.run_dir.string());
    push_string_field(L, "xpkg_dir",          ctx.xpkg_dir.string());
    push_string_field(L, "bin_dir",           ctx.bin_dir.string());
    push_string_field(L, "subos_sysrootdir",  ctx.subos_sysrootdir);
    // deps_list as table
    lua::newtable(L);
    for (int i = 0; i < (int)ctx.deps_list.size(); ++i) {
        lua::pushstring(L, ctx.deps_list[i].c_str());
        lua::rawseti(L, -2, i + 1);
    }
    lua::setfield(L, -2, "deps_list");
    lua::setglobal(L, "_RUNTIME");
}

} // namespace detail

// --- PackageExecutor ---

class PackageExecutor {
    lua::State* L_ = nullptr;
    fs::path pkg_path_;

public:
    explicit PackageExecutor(lua::State* L, const fs::path& pkg_path)
        : L_(L), pkg_path_(pkg_path) {}

    ~PackageExecutor() {
        if (L_) { lua::close(L_); L_ = nullptr; }
    }

    // Non-copyable, movable
    PackageExecutor(const PackageExecutor&) = delete;
    PackageExecutor& operator=(const PackageExecutor&) = delete;

    PackageExecutor(PackageExecutor&& o) noexcept
        : L_(std::exchange(o.L_, nullptr)), pkg_path_(std::move(o.pkg_path_)) {}

    bool has_hook(HookType hook) const {
        auto name = detail::hook_name(hook);
        lua::getglobal(L_, std::string(name).c_str());
        bool found = lua::type(L_, -1) == lua::TFUNCTION;
        lua::pop(L_, 1);
        return found;
    }

    HookResult run_hook(HookType hook, const ExecutionContext& ctx) {
        detail::inject_context(L_, ctx);

        auto name = detail::hook_name(hook);
        lua::getglobal(L_, std::string(name).c_str());
        if (lua::type(L_, -1) != lua::TFUNCTION) {
            lua::pop(L_, 1);
            return { .success = false, .error = "hook not found: " + std::string(name) };
        }

        HookResult result;
        if (lua::pcall(L_, 0, 1, 0) == lua::OK) {
            // Hook returned boolean or string (version for installed())
            if (lua::type(L_, -1) == lua::TBOOLEAN) {
                result.success = lua::toboolean(L_, -1);
            } else if (lua::type(L_, -1) == lua::TSTRING) {
                result.version = lua::tostring(L_, -1);
                result.success = !result.version.empty();
            } else {
                result.success = true;  // nil return = success (hook exists, didn't fail)
            }
            lua::pop(L_, 1);
        } else {
            result.success = false;
            result.error = lua::tostring(L_, -1);
            lua::pop(L_, 1);
        }
        return result;
    }

    HookResult check_installed(const ExecutionContext& ctx) {
        return run_hook(HookType::Installed, ctx);
    }
};

// --- Factory ---

std::expected<PackageExecutor, std::string>
create_executor(const fs::path& pkg_path) {
    if (!fs::exists(pkg_path)) {
        return std::unexpected("package file not found: " + pkg_path.string());
    }

    lua::State* L = lua::L_newstate();
    if (!L) return std::unexpected("failed to create lua state");

    lua::L_openlibs(L);

    std::string err;
    if (!detail::load_stdlib(L, err)) {
        lua::close(L);
        return std::unexpected(err);
    }

    if (lua::L_dofile(L, pkg_path.string().c_str()) != lua::OK) {
        err = lua::tostring(L, -1);
        lua::close(L);
        return std::unexpected("failed to load package: " + err);
    }

    return PackageExecutor(L, pkg_path);
}

} // namespace mcpplibs::xpkg
```

**Step 4: Regenerate lua_stdlib_embed.hpp**

```bash
bash scripts/gen_lua_embed.sh
```

**Step 5: Build and run executor tests**

```bash
xmake build xpkg_test
xmake run xpkg_test --gtest_filter="ExecutorTest*"
```
Expected: CreateExecutor_ExistingFile PASS, CreateExecutor_MissingFile PASS, HasHook_Install PASS.

**Step 6: Commit**

```bash
git add src/xpkg-executor.cppm tests/test_executor.cpp
git commit -m "feat: implement PackageExecutor with bundled lua stdlib"
```

---

## Phase 4: Loader

### Task 9: xpkg-loader.cppm — load_package()

**Files:**
- Create: `src/xpkg-loader.cppm`
- Create: `tests/test_loader.cpp`

**Step 1: Write failing test**

```cpp
// tests/test_loader.cpp
#include <gtest/gtest.h>
import mcpplibs.xpkg;
import mcpplibs.xpkg.loader;
using namespace mcpplibs::xpkg;
namespace fs = std::filesystem;

static constexpr auto PKGINDEX =
    "/home/<user>/workspace/github/d2learn/xim-pkgindex";

TEST(LoaderTest, LoadPackage_Mdbook) {
    auto result = load_package(
        fs::path(PKGINDEX) / "pkgs/m/mdbook.lua");
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_EQ(result->name, "mdbook");
    EXPECT_EQ(result->type, PackageType::Package);
    EXPECT_EQ(result->status, PackageStatus::Stable);
    EXPECT_FALSE(result->xpm.entries.empty());
}

TEST(LoaderTest, LoadPackage_MissingFile) {
    auto result = load_package("/nonexistent/pkg.lua");
    EXPECT_FALSE(result.has_value());
}

TEST(LoaderTest, LoadPackage_HasPlatforms) {
    auto result = load_package(
        fs::path(PKGINDEX) / "pkgs/m/mdbook.lua");
    ASSERT_TRUE(result.has_value());
    auto& xpm = result->xpm.entries;
    EXPECT_TRUE(xpm.count("linux") > 0 || xpm.count("windows") > 0);
}
```

**Step 2: Run test to verify it fails**

```bash
xmake build xpkg_test 2>&1 | head -5
```
Expected: compile error — `mcpplibs.xpkg.loader` not found.

**Step 3: Write `src/xpkg-loader.cppm`**

Key implementation notes:
- Use `lua::L_newstate()` + `lua::L_openlibs()` + `lua::L_dofile()` to execute the .lua
- Read global `package` table: `lua::getglobal(L, "package")`
- Iterate table fields: `lua::pushnil(L)` + `lua::next(L, table_idx)` loop
- For nested `xpm`: recurse into platform/version tables
- String fields: `lua::tostring(L, -1)` after `lua::getfield()`
- Boolean fields: `lua::toboolean(L, -1)`

```cpp
module;

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

export module mcpplibs.xpkg.loader;
import mcpplibs.xpkg;
import mcpplibs.capi.lua;
import std;

namespace lua = mcpplibs::capi::lua;
namespace fs  = std::filesystem;

namespace mcpplibs::xpkg::detail {

std::string lua_string_field(lua::State* L, int idx, const char* key) {
    lua::getfield(L, idx, key);
    std::string result;
    if (lua::type(L, -1) == lua::TSTRING)
        result = lua::tostring(L, -1);
    lua::pop(L, 1);
    return result;
}

bool lua_bool_field(lua::State* L, int idx, const char* key) {
    lua::getfield(L, idx, key);
    bool result = lua::toboolean(L, -1);
    lua::pop(L, 1);
    return result;
}

std::vector<std::string> lua_string_array(lua::State* L, int idx, const char* key) {
    std::vector<std::string> result;
    lua::getfield(L, idx, key);
    if (lua::type(L, -1) == lua::TTABLE) {
        lua::pushnil(L);
        while (lua::next(L, -2)) {
            if (lua::type(L, -1) == lua::TSTRING)
                result.push_back(lua::tostring(L, -1));
            lua::pop(L, 1);
        }
    }
    lua::pop(L, 1);
    return result;
}

PackageType parse_type(const std::string& s) {
    if (s == "script")   return PackageType::Script;
    if (s == "template") return PackageType::Template;
    if (s == "config")   return PackageType::Config;
    return PackageType::Package;
}

PackageStatus parse_status(const std::string& s) {
    if (s == "stable")     return PackageStatus::Stable;
    if (s == "deprecated") return PackageStatus::Deprecated;
    return PackageStatus::Dev;
}

// Parse xpm.{platform}.{version} nested table
PlatformMatrix parse_xpm(lua::State* L, int idx) {
    PlatformMatrix xpm;
    lua::getfield(L, idx, "xpm");
    if (lua::type(L, -1) != lua::TTABLE) { lua::pop(L, 1); return xpm; }

    int xpm_idx = lua::gettop(L);
    lua::pushnil(L);
    while (lua::next(L, xpm_idx)) {
        // key = platform name
        std::string platform;
        if (lua::type(L, -2) == lua::TSTRING) platform = lua::tostring(L, -2);
        if (!platform.empty() && lua::type(L, -1) == lua::TTABLE) {
            int plat_idx = lua::gettop(L);
            lua::pushnil(L);
            while (lua::next(L, plat_idx)) {
                // key = version string
                std::string version;
                if (lua::type(L, -2) == lua::TSTRING) version = lua::tostring(L, -2);
                if (!version.empty()) {
                    PlatformResource res;
                    if (lua::type(L, -1) == lua::TTABLE) {
                        res.url    = lua_string_field(L, lua::gettop(L), "url");
                        res.sha256 = lua_string_field(L, lua::gettop(L), "sha256");
                        res.ref    = lua_string_field(L, lua::gettop(L), "ref");
                    } else if (lua::type(L, -1) == lua::TSTRING) {
                        res.url = lua::tostring(L, -1);  // e.g. "XLINGS_RES"
                    }
                    xpm.entries[platform][version] = std::move(res);
                }
                lua::pop(L, 1);
            }
        }
        lua::pop(L, 1);
    }
    lua::pop(L, 1);  // pop xpm table
    return xpm;
}

} // namespace mcpplibs::xpkg::detail

export namespace mcpplibs::xpkg {

std::expected<Package, std::string>
load_package(const fs::path& path) {
    if (!fs::exists(path))
        return std::unexpected("file not found: " + path.string());

    lua::State* L = lua::L_newstate();
    if (!L) return std::unexpected("failed to create lua state");
    lua::L_openlibs(L);

    // Execute the package .lua file
    if (lua::L_dofile(L, path.string().c_str()) != lua::OK) {
        std::string err = lua::tostring(L, -1);
        lua::close(L);
        return std::unexpected("lua error: " + err);
    }

    // Read global 'package' table
    lua::getglobal(L, "package");
    if (lua::type(L, -1) != lua::TTABLE) {
        lua::close(L);
        return std::unexpected("'package' table not found in " + path.string());
    }

    int pkg_idx = lua::gettop(L);
    Package p;
    p.spec        = detail::lua_string_field(L, pkg_idx, "spec");
    p.name        = detail::lua_string_field(L, pkg_idx, "name");
    p.description = detail::lua_string_field(L, pkg_idx, "description");
    p.type        = detail::parse_type(detail::lua_string_field(L, pkg_idx, "type"));
    p.status      = detail::parse_status(detail::lua_string_field(L, pkg_idx, "status"));
    p.namespace_  = detail::lua_string_field(L, pkg_idx, "namespace");
    p.homepage    = detail::lua_string_field(L, pkg_idx, "homepage");
    p.repo        = detail::lua_string_field(L, pkg_idx, "repo");
    p.docs        = detail::lua_string_field(L, pkg_idx, "docs");
    p.xvm_enable  = detail::lua_bool_field(L, pkg_idx, "xvm_enable");
    p.authors     = detail::lua_string_array(L, pkg_idx, "authors");
    p.licenses    = detail::lua_string_array(L, pkg_idx, "licenses");
    p.categories  = detail::lua_string_array(L, pkg_idx, "categories");
    p.keywords    = detail::lua_string_array(L, pkg_idx, "keywords");
    p.archs       = detail::lua_string_array(L, pkg_idx, "archs");
    p.xpm         = detail::parse_xpm(L, pkg_idx);

    lua::close(L);
    return p;
}

std::expected<PackageIndex, std::string>
build_index(const fs::path& repo_dir, const std::string& namespace_) {
    PackageIndex index;
    auto pkgs_dir = repo_dir / "pkgs";
    if (!fs::is_directory(pkgs_dir))
        return std::unexpected("pkgs/ not found in " + repo_dir.string());

    for (auto& letter_dir : fs::directory_iterator(pkgs_dir)) {
        if (!letter_dir.is_directory()) continue;
        for (auto& entry : fs::directory_iterator(letter_dir)) {
            if (entry.path().extension() != ".lua") continue;
            auto result = load_package(entry.path());
            if (!result) continue;  // skip malformed packages
            auto& pkg = *result;
            // Build canonical name: "name@version" or just "name" for latest
            std::string key = (namespace_.empty() ? "" : namespace_ + "-x-")
                            + pkg.name;
            IndexEntry ie;
            ie.name        = key;
            ie.version     = "";   // no single version from pkg def
            ie.path        = entry.path();
            ie.type        = pkg.type;
            ie.description = pkg.description;
            index.entries[key] = std::move(ie);
        }
    }
    return index;
}

std::expected<IndexRepos, std::string>
load_index_repos(const fs::path& path) {
    // Minimal implementation: parse xim-indexrepos.lua
    // Full implementation deferred to next iteration
    return std::unexpected("load_index_repos: not yet implemented");
}

} // namespace mcpplibs::xpkg
```

**Step 4: Build and run loader tests**

```bash
xmake build xpkg_test
xmake run xpkg_test --gtest_filter="LoaderTest*"
```
Expected: all 3 LoaderTest cases pass.

**Step 5: Commit**

```bash
git add src/xpkg-loader.cppm tests/test_loader.cpp
git commit -m "feat: implement load_package and build_index (mcpplibs.xpkg.loader)"
```

---

## Phase 5: Index

### Task 10: xpkg-index.cppm — search, resolve, match_version

**Files:**
- Create: `src/xpkg-index.cppm`
- Create: `tests/test_index.cpp`

**Step 1: Write failing tests**

```cpp
// tests/test_index.cpp
#include <gtest/gtest.h>
import mcpplibs.xpkg;
import mcpplibs.xpkg.loader;
import mcpplibs.xpkg.index;
using namespace mcpplibs::xpkg;
namespace fs = std::filesystem;

static PackageIndex build_test_index() {
    auto result = build_index(
        "/home/<user>/workspace/github/d2learn/xim-pkgindex");
    return result.value_or(PackageIndex{});
}

TEST(IndexTest, Search_Python) {
    auto index = build_test_index();
    auto results = search(index, "python");
    EXPECT_FALSE(results.empty());
    for (auto& r : results)
        EXPECT_TRUE(r.find("python") != std::string::npos
                 || index.entries.at(r).description.find("python")
                    != std::string::npos);
}

TEST(IndexTest, Search_Empty_ReturnsAll) {
    auto index = build_test_index();
    auto all = search(index, "");
    EXPECT_EQ(all.size(), index.entries.size());
}

TEST(IndexTest, SetInstalled) {
    PackageIndex index;
    index.entries["mdbook"] = IndexEntry{ .name="mdbook", .installed=false };
    set_installed(index, "mdbook", true);
    EXPECT_TRUE(index.entries["mdbook"].installed);
}

TEST(IndexTest, Merge) {
    PackageIndex base, overlay;
    base.entries["pkg-a"]    = IndexEntry{ .name="pkg-a" };
    overlay.entries["pkg-b"] = IndexEntry{ .name="pkg-b" };
    auto merged = merge(base, overlay, "ns");
    EXPECT_TRUE(merged.entries.count("pkg-a") > 0);
    EXPECT_TRUE(merged.entries.count("ns-x-pkg-b") > 0);
}
```

**Step 2: Run to confirm failure**

```bash
xmake build xpkg_test 2>&1 | head -5
```

**Step 3: Write `src/xpkg-index.cppm`**

```cpp
module;
export module mcpplibs.xpkg.index;
import mcpplibs.xpkg;
import std;

export namespace mcpplibs::xpkg {

// Case-insensitive substring search on name and description
std::vector<std::string> search(const PackageIndex& index, const std::string& query) {
    std::vector<std::string> results;
    auto lower = [](std::string s) {
        std::ranges::transform(s, s.begin(), ::tolower);
        return s;
    };
    std::string q = lower(query);
    for (auto& [key, entry] : index.entries) {
        if (q.empty()
            || lower(entry.name).find(q) != std::string::npos
            || lower(entry.description).find(q) != std::string::npos) {
            results.push_back(key);
        }
    }
    std::ranges::sort(results);
    return results;
}

// Dereference alias: if entry.ref is set, resolve to ref
std::string resolve(const PackageIndex& index, const std::string& name) {
    auto it = index.entries.find(name);
    if (it == index.entries.end()) return name;
    if (!it->second.ref.empty()) return it->second.ref;
    return name;
}

// Best version: prefer installed, else first entry with this name
std::optional<std::string> match_version(const PackageIndex& index,
                                          const std::string& name) {
    std::optional<std::string> best;
    for (auto& [key, entry] : index.entries) {
        if (entry.name == name || key == name) {
            if (entry.installed) return key;
            if (!best) best = key;
        }
    }
    return best;
}

std::vector<std::string> mutex_packages(const PackageIndex& index,
                                         const std::string& pkg_name) {
    for (auto& [group_name, members] : index.mutex_groups) {
        for (auto& m : members) {
            if (m == pkg_name) {
                return members;
            }
        }
    }
    return {};
}

// Merge overlay into base, prefixing overlay keys with namespace_-x-
PackageIndex merge(PackageIndex base, const PackageIndex& overlay,
                   const std::string& namespace_) {
    for (auto& [key, entry] : overlay.entries) {
        std::string new_key = namespace_.empty() ? key : (namespace_ + "-x-" + key);
        auto e = entry;
        e.name = new_key;
        base.entries[new_key] = std::move(e);
    }
    return base;
}

void set_installed(PackageIndex& index, const std::string& name, bool installed) {
    auto it = index.entries.find(name);
    if (it != index.entries.end()) it->second.installed = installed;
}

} // namespace mcpplibs::xpkg
```

**Step 4: Build and run index tests**

```bash
xmake build xpkg_test
xmake run xpkg_test --gtest_filter="IndexTest*"
```
Expected: all 4 IndexTest cases pass.

**Step 5: Commit**

```bash
git add src/xpkg-index.cppm tests/test_index.cpp
git commit -m "feat: implement index operations (mcpplibs.xpkg.index)"
```

---

## Phase 6: Integration Verification

### Task 11: End-to-end smoke tests

**Step 1: Run all tests**

```bash
xmake build
xmake run xpkg_test
```
Expected: all tests pass.

**Step 2: Manual end-to-end smoke**

```bash
# Build all targets
xmake build

# Verify real package count
xmake run xpkg_test --gtest_filter="LoaderTest*:IndexTest*"
```

**Step 3: Verify executor with mdbook.lua has_hook**

```bash
xmake run xpkg_test --gtest_filter="ExecutorTest*"
```

**Step 4: Final commit**

```bash
git add .
git commit -m "feat: libxpkg v0.1 — data model, loader, index, executor with lua stdlib"
```

---

## Summary of Files Created

| File | Module | Description |
|------|--------|-------------|
| `src/xpkg.cppm` | `mcpplibs.xpkg` | Data model |
| `src/xpkg-loader.cppm` | `mcpplibs.xpkg.loader` | Parse .lua packages, build index |
| `src/xpkg-index.cppm` | `mcpplibs.xpkg.index` | Search, resolve, merge |
| `src/xpkg-executor.cppm` | `mcpplibs.xpkg.executor` | Execute hooks with Lua runtime |
| `src/lua_stdlib_embed.hpp` | (generated) | Embedded Lua stdlib |
| `src/lua-stdlib/prelude.lua` | — | os/path/io compat + import() |
| `src/lua-stdlib/xim/libxpkg/log.lua` | xim.libxpkg.log | Logging |
| `src/lua-stdlib/xim/libxpkg/pkginfo.lua` | xim.libxpkg.pkginfo | Package info |
| `src/lua-stdlib/xim/libxpkg/system.lua` | xim.libxpkg.system | System ops |
| `src/lua-stdlib/xim/libxpkg/xvm.lua` | xim.libxpkg.xvm | xvm CLI integration |
| `src/lua-stdlib/xim/libxpkg/utils.lua` | xim.libxpkg.utils | Utilities |
| `scripts/gen_lua_embed.sh` | — | Regenerates lua_stdlib_embed.hpp |
| `tests/test_model.cpp` | — | Data model tests |
| `tests/test_loader.cpp` | — | Loader tests (real packages) |
| `tests/test_index.cpp` | — | Index tests |
| `tests/test_executor.cpp` | — | Executor tests (real packages) |
