# libxpkg 设计方案

> 文档日期：2026-03-01

## 1. 项目定位

libxpkg 是 **xpkg 包描述规范的 C++23 标准库（参考实现）**：

- 解析和验证 xpkg Lua 格式包定义文件
- 管理 xpkg 包索引仓库结构
- 通过 mcpplibs/lua 执行包 Lua 钩子（installed/install/config/uninstall）
- 通用设计，可被任何 C++ 工具嵌入使用，不绑定 xlings

xpkg 是一套基于 Lua 的包描述规范，目前由 xlings/xim（Lua 实现）使用。libxpkg 让任何 C++ 工具都能读取、操作和执行 xpkg 格式的包及索引，而无需依赖 xlings 本身。

---

## 2. 架构：分层子模块（方案 B）

四个独立 C++23 子模块，按依赖分层：

```
mcpplibs.xpkg              ← 纯 C++ 数据模型，零外部依赖
mcpplibs.xpkg.loader       ← 依赖 model + lua 库；解析 .lua 文件
mcpplibs.xpkg.index        ← 依赖 model；纯 C++ 索引操作
mcpplibs.xpkg.executor     ← 依赖 model + lua 库；执行钩子
```

依赖关系图：

```
┌─────────────────────────────────────┐
│         mcpplibs.xpkg               │  (数据模型，零外部依赖)
└───────────────┬─────────────────────┘
                │ import
    ┌───────────┼───────────┐
    ▼           ▼           ▼
┌────────┐ ┌────────┐ ┌──────────┐
│ loader │ │ index  │ │ executor │
│ +lua   │ │        │ │ +lua     │
└────────┘ └────────┘ └──────────┘
```

---

## 3. 数据模型层 (xpkg.cppm → mcpplibs.xpkg)

**模块**：`mcpplibs.xpkg`
**文件**：`src/xpkg.cppm`
**依赖**：`import std;`（无外部依赖）

```cpp
export module mcpplibs.xpkg;
import std;

export namespace mcpplibs::xpkg {

enum class PackageType   { Package, Script, Template, Config };
enum class PackageStatus { Dev, Stable, Deprecated };

struct PlatformResource {
    std::string url;
    std::string sha256;
    std::string ref;       // 版本别名 "latest" -> "1.0.0"
};

struct PlatformMatrix {
    // platform -> version -> resource
    std::unordered_map<std::string,
        std::unordered_map<std::string, PlatformResource>> entries;
    // platform -> deps list
    std::unordered_map<std::string, std::vector<std::string>> deps;
    // platform 继承 (ubuntu = linux)
    std::unordered_map<std::string, std::string> inherits;
};

struct Package {
    std::string spec;           // "1"
    std::string name;
    std::string description;
    PackageType type;
    PackageStatus status;
    std::string namespace_;
    std::string homepage, repo, docs;
    std::vector<std::string> authors, maintainers, licenses;
    std::vector<std::string> categories, keywords, programs, archs;
    bool xvm_enable = false;
    PlatformMatrix xpm;
};

struct IndexEntry {
    std::string name;           // "vscode@1.85.0"
    std::string version;
    std::filesystem::path path;
    PackageType type;
    std::string description;
    bool installed = false;
    std::string ref;            // 别名指向的规范名
};

struct PackageIndex {
    std::unordered_map<std::string, IndexEntry> entries;
    std::unordered_map<std::string, std::vector<std::string>> mutex_groups;
};

struct RepoConfig {
    std::string name;           // namespace，主仓库为空
    std::string url_global, url_cn;
    std::filesystem::path local_path;
};

struct IndexRepos {
    RepoConfig main_repo;
    std::vector<RepoConfig> sub_repos;
};

} // namespace mcpplibs::xpkg
```

---

## 4. 加载层 (xpkg-loader.cppm → mcpplibs.xpkg.loader)

**模块**：`mcpplibs.xpkg.loader`
**文件**：`src/xpkg-loader.cppm`
**依赖**：`mcpplibs.xpkg` + `mcpplibs.capi.lua`（lua 库）

### API

```cpp
export module mcpplibs.xpkg.loader;
import mcpplibs.xpkg;
import std;

export namespace mcpplibs::xpkg {

// 解析 xpkg .lua 包定义文件 → Package
std::expected<Package, std::string>
load_package(const std::filesystem::path& path);

// 解析 xim-indexrepos.lua → IndexRepos
std::expected<IndexRepos, std::string>
load_index_repos(const std::filesystem::path& path);

// 扫描 repo 目录中的 pkgs/ 构建 PackageIndex
std::expected<PackageIndex, std::string>
build_index(const std::filesystem::path& repo_dir,
            const std::string& namespace_ = "");

// 持久化索引 (JSON 格式)
std::expected<PackageIndex, std::string>
load_index_db(const std::filesystem::path& db_path);

void save_index_db(const PackageIndex& index,
                   const std::filesystem::path& db_path);

} // namespace mcpplibs::xpkg
```

### 实现要点

- 用 `mcpplibs::capi::lua` 的 `L_newstate` / `L_dofile` 加载 Lua 文件
- 读取全局 `package` table（`getglobal` → 遍历 table 字段）
- 递归遍历 `xpm` 平台矩阵（`next` 迭代）
- 索引扫描：递归遍历 `pkgs/` 目录，每个 `.lua` 提取轻量元数据
- 索引持久化使用 JSON 格式（可用标准库或 nlohmann/json）

### xim-pkgindex 目录结构映射

```
xim-pkgindex/
├── pkgs/
│   ├── a/
│   │   └── aarch64-linux-gnu.lua   ← load_package() 的输入
│   ├── l/
│   │   └── llvm.lua
│   └── v/
│       └── vscode.lua
├── xim-indexrepos.lua              ← load_index_repos() 的输入
└── ...
```

---

## 5. 索引层 (xpkg-index.cppm → mcpplibs.xpkg.index)

**模块**：`mcpplibs.xpkg.index`
**文件**：`src/xpkg-index.cppm`
**依赖**：`mcpplibs.xpkg`（纯 C++，无 lua 依赖）

### API

```cpp
export module mcpplibs.xpkg.index;
import mcpplibs.xpkg;
import std;

export namespace mcpplibs::xpkg {

// 模糊搜索（名称/描述子串匹配）
std::vector<std::string>
search(const PackageIndex&, const std::string& query);

// 解引用别名 "vscode" → "vscode@1.85.0"
std::string
resolve(const PackageIndex&, const std::string& name);

// 最优版本匹配（已安装优先，然后 latest）
std::optional<std::string>
match_version(const PackageIndex&, const std::string& name);

// 互斥包查询
std::vector<std::string>
mutex_packages(const PackageIndex&, const std::string& pkg_name);

// 合并索引（主仓库 + 子仓库 namespace 前缀）
PackageIndex
merge(PackageIndex base, const PackageIndex& overlay,
      const std::string& namespace_ = "");

void
set_installed(PackageIndex&, const std::string& name, bool installed);

} // namespace mcpplibs::xpkg
```

### 设计说明

- `search()` 对 name 和 description 字段做大小写不敏感子串搜索
- `resolve()` 处理 `ref` 字段，将别名（"vscode"）解析到具体版本（"vscode@1.85.0"）
- `merge()` 将子仓库条目加 namespace 前缀后合并到主索引

---

## 6. 执行层 (xpkg-executor.cppm → mcpplibs.xpkg.executor)

**模块**：`mcpplibs.xpkg.executor`
**文件**：`src/xpkg-executor.cppm`
**依赖**：`mcpplibs.xpkg` + `mcpplibs.capi.lua`（lua 库）

### API

```cpp
export module mcpplibs.xpkg.executor;
import mcpplibs.xpkg;
import std;

export namespace mcpplibs::xpkg {

struct ExecutionContext {
    std::string pkg_name, version, platform, arch;
    std::filesystem::path install_file, install_dir;
    std::vector<std::string> deps, args;
};

struct HookResult {
    bool success;
    std::string output, error;
    std::string version;  // installed() 返回的版本号
};

enum class HookType { Installed, Build, Install, Config, Uninstall };

class PackageExecutor {
public:
    explicit PackageExecutor(const std::filesystem::path& pkg_path);
    bool has_hook(HookType hook) const;
    HookResult run_hook(HookType hook, const ExecutionContext& ctx);
    // check_installed 是 run_hook(Installed, ctx) 的便捷封装
    HookResult check_installed(const ExecutionContext& ctx);
};

std::expected<PackageExecutor, std::string>
create_executor(const std::filesystem::path& pkg_path);

} // namespace mcpplibs::xpkg
```

### 实现要点

- 每个 `PackageExecutor` 维护独立 `lua_State`
- 用 `L_dofile` 加载包文件，注册 xpkg 标准 API（pkginfo/system/log）到 Lua 全局
- 执行前将 `ExecutionContext` 数据注入 Lua 全局（`pkginfo.name()` 等返回 ctx 中的值）
- 通过 `getglobal` + `pcall` 调用 Lua 钩子函数
- 钩子名称映射：

| HookType   | Lua 函数名   |
|------------|-------------|
| Installed  | `installed` |
| Build      | `build`     |
| Install    | `install`   |
| Config     | `config`    |
| Uninstall  | `uninstall` |

---

## 7. 文件布局

```
libxpkg/
├── src/
│   ├── xpkg.cppm              # mcpplibs.xpkg (数据模型)
│   ├── xpkg-loader.cppm       # mcpplibs.xpkg.loader
│   ├── xpkg-index.cppm        # mcpplibs.xpkg.index
│   └── xpkg-executor.cppm     # mcpplibs.xpkg.executor
├── tests/
│   ├── main.cpp
│   ├── test_loader.cpp        # 用 xim-pkgindex 真实包测试
│   ├── test_index.cpp
│   ├── test_executor.cpp
│   └── xmake.lua
├── examples/
│   ├── basic.cpp              # 加载包元数据
│   ├── index_demo.cpp         # 构建并搜索索引
│   └── xmake.lua
├── docs/
│   └── architecture.md        # 模块概览与架构说明
├── .agents/
│   ├── skills/                # 项目专属技能
│   └── plans/
│       └── 2026-03-01-libxpkg-design.md   ← 本文档
├── xmake.lua
├── CMakeLists.txt
└── README.md
```

---

## 8. 构建配置要点 (xmake.lua)

```lua
add_requires("lua")
-- mcpplibs-capi-lua 通过 git 子模块或 add_requires 引入
-- add_requires("mcpplibs-capi-lua")

add_rules("mode.release", "mode.debug")
set_languages("c++23")

-- 数据模型（无外部依赖）
target("mcpplibs-xpkg")
    set_kind("static")
    set_languages("c++23")
    add_files("src/xpkg.cppm", {public = true})
    set_policy("build.c++.modules", true)

-- 加载层（依赖 lua）
target("mcpplibs-xpkg-loader")
    set_kind("static")
    add_deps("mcpplibs-xpkg")
    add_packages("lua", {public = true})
    add_files("src/xpkg-loader.cppm", {public = true})
    set_policy("build.c++.modules", true)
    -- 同时依赖 mcpplibs-capi-lua（需要 add_deps）

-- 索引层（纯 C++）
target("mcpplibs-xpkg-index")
    set_kind("static")
    add_deps("mcpplibs-xpkg")
    add_files("src/xpkg-index.cppm", {public = true})
    set_policy("build.c++.modules", true)

-- 执行层（依赖 lua）
target("mcpplibs-xpkg-executor")
    set_kind("static")
    add_deps("mcpplibs-xpkg")
    add_packages("lua", {public = true})
    add_files("src/xpkg-executor.cppm", {public = true})
    set_policy("build.c++.modules", true)
    -- 同时依赖 mcpplibs-capi-lua（需要 add_deps）

includes("examples", "tests")
```

---

## 9. 验证方式

1. `xmake` 构建所有目标无报错
2. `xmake run templates_test` 基础测试通过
3. 用 `xim-pkgindex` 中的真实包（如 `pkgs/l/llvm.lua`）测试 `load_package`
4. 用 `xim-pkgindex` 根目录测试 `build_index`，验证包条目数量
5. 测试 `search("python")` 返回 python 相关包
6. 测试 `resolve("vscode")` 返回 `"vscode@x.x.x"`

---

---

## 10. 运行时架构（Runtime）

### 背景

现有 xim-pkgindex 包脚本通过 xmake 的 Lua 运行时访问 `xim.libxpkg.*` API：

```lua
import("xim.libxpkg.pkginfo")
import("xim.libxpkg.xvm")
import("xim.libxpkg.system")
import("xim.libxpkg.log")

function install()
    return os.trymv(file, pkginfo.install_dir())  -- xmake 扩展 os.*
end
```

libxpkg 执行层（executor）使用裸 Lua 5.4，必须自建兼容运行时。

---

### 方案选择：C++ 骨架 + 捆绑 Lua 标准库（方案 B）

**C++ 只做三件事：**
1. 管理 `lua_State` 生命周期
2. 将 `ExecutionContext` 注入为 Lua 全局变量
3. 加载捆绑的 Lua 标准库并调用钩子

**Lua 负责所有 API 逻辑**（pkginfo/xvm/system/log/os compat 等）

---

### 文件布局

```
src/
├── xpkg-executor.cppm          # C++ 骨架
└── lua-stdlib/                 # 捆绑的 Lua 运行时（xpkg 标准库）
    ├── prelude.lua             # ① os/path/io/try 兼容层  ② import() 函数
    └── xim/libxpkg/
        ├── pkginfo.lua         # pkginfo.name/version/install_dir/...
        ├── xvm.lua             # xvm.add/remove/use/info/has
        ├── system.lua          # system.exec/rundir/xpkgdir/unix_api
        ├── log.lua             # log.info/warn/error/debug
        ├── utils.lua           # utils.filepath_to_absolute/...
        └── pkgmanager.lua      # pkgmanager.install/remove
```

所有 .lua 文件通过 xmake `bin2c` 规则编译为 C++ 字节数组，零运行时文件依赖。

---

### ExecutionContext → Lua 全局变量注入

C++ 在执行任何钩子前，将 `ExecutionContext` 注入为 Lua 全局表 `_RUNTIME`：

```cpp
// C++ 端（executor）
lua::newtable(L);
lua::pushstring(L, ctx.pkg_name);    lua::setfield(L, -2, "pkg_name");
lua::pushstring(L, ctx.version);     lua::setfield(L, -2, "version");
lua::pushstring(L, ctx.platform);    lua::setfield(L, -2, "platform");
lua::pushstring(L, ctx.arch);        lua::setfield(L, -2, "arch");
// install_file, install_dir, run_dir, xpkg_dir, bin_dir, deps_list...
lua::setglobal(L, "_RUNTIME");
```

---

### prelude.lua — 兼容层 + import() 函数

`prelude.lua` 是执行任何包脚本前必须加载的运行时垫片：

```lua
-- ① 预加载所有 xim.libxpkg.* 模块（由 C++ 在 prelude 前注入为 _LIBXPKG_MODULES）
-- （C++ 逐一 dostring 加载每个模块，并将返回值填入 _LIBXPKG_MODULES 表）

-- ② 兼容 xmake 的 import() 函数
function import(mod_path)
    local name = mod_path:match("xim%.libxpkg%.(.+)")
    if name then
        return _LIBXPKG_MODULES[name]   -- 预加载的模块表
    end
    -- 其他路径（如 base.runtime）返回空 stub，或按需扩展
    return setmetatable({}, { __index = function() return function() end end })
end

-- ③ os.* xmake 扩展兼容层
os.trymv   = function(src, dst) return os.rename and pcall(os.rename, src, dst) end
os.isdir   = function(p) ... end   -- 用 io.open 模拟
os.isfile  = function(p) ... end
os.host    = function() return _RUNTIME.platform end
os.dirs    = function(pattern) ... end  -- lfs 或 popen("ls")

-- ④ path.* 兼容层
path = {}
path.join  = function(...) return table.concat({...}, "/") end
path.filename = function(p) return p:match("[^/]+$") end
-- ...

-- ⑤ io 扩展
io.readfile  = function(p) ... end
io.writefile = function(p, content) ... end

-- ⑥ cprint（去色简化版）
cprint = function(fmt, ...) print(string.format(fmt:gsub("${%w+}", ""), ...)) end

-- ⑦ try/catch（Lua 闭包模拟 xmake 语法）
function try(block)
    local ok, err = pcall(block[1])
    if not ok and block.catch then block.catch[1](err) end
end
```

---

### xim/libxpkg/pkginfo.lua — 从 _RUNTIME 读取

```lua
local M = {}
function M.name()         return _RUNTIME.pkg_name end
function M.version()      return _RUNTIME.version end
function M.install_file() return _RUNTIME.install_file end
function M.install_dir(pkgname, pkgversion)
    if not pkgname then return _RUNTIME.install_dir end
    -- 扫描 xpkg_dir 查找依赖目录
    ...
end
function M.deps_list()    return _RUNTIME.deps_list or {} end
return M
```

---

### xim/libxpkg/xvm.lua — 调用 xvm CLI

xvm 是 xlings 提供的外部工具。libxpkg 运行时通过 `os.execute()` 调用 `xvm` CLI 命令，而不内联实现 xvm 逻辑：

```lua
local M = {}
function M.add(name, opt)
    opt = opt or {}
    local ver = opt.version or _RUNTIME.version
    local bindir = opt.bindir or _RUNTIME.install_dir
    os.execute(string.format("xvm add %s %s %s", name, ver, bindir))
end
function M.remove(name, version)
    os.execute(string.format("xvm remove %s %s", name, version or ""))
end
function M.log_tag(enable) return true end  -- stub
return M
```

---

### C++ 执行流程（PackageExecutor::run_hook）

```
create_executor(pkg_path)
    └─ L = lua::L_newstate()
    └─ lua::L_openlibs(L)            -- 标准库 (os/io/string/math/table)
    └─ inject _LIBXPKG_MODULES table  -- 为每个模块 dostring → setfield
    └─ lua::L_dostring(L, prelude)    -- 加载兼容层 + import()
    └─ lua::L_dofile(L, pkg_path)     -- 加载包脚本（定义 install/config/...）

run_hook(HookType::Install, ctx)
    └─ inject_context(L, ctx)         -- _RUNTIME = { pkg_name=..., ... }
    └─ lua::getglobal(L, "install")
    └─ lua::pcall(L, 0, 1, 0)
    └─ 读取返回值 → HookResult
```

---

## 参考资料

- xim 实现：`/home/<user>/workspace/github/d2learn/xlings/core/xim/`
- 包索引仓库：`/home/<user>/workspace/github/d2learn/xim-pkgindex/`
- lua 库：`/home/<user>/workspace/github/mcpplibs/lua/`
- xpackage 规范文档：`/home/<user>/workspace/github/d2learn/xim-pkgindex/docs/V1/xpackage-spec.md`
- [mcpp-style-ref | 现代C++编码/项目风格参考](https://github.com/mcpp-community/mcpp-style-ref)
