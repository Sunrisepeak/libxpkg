# libxpkg

> xpkg 包描述规范的 C++23 参考实现 — `import mcpplibs.xpkg;`

[![CI](https://github.com/Sunrisepeak/libxpkg/actions/workflows/ci.yml/badge.svg)](https://github.com/Sunrisepeak/libxpkg/actions/workflows/ci.yml)

**libxpkg** 是 [xpkg 规范](https://github.com/d2learn/xim-pkgindex/blob/main/docs/V1/xpackage-spec.md) 的 C++23 标准库实现，让任何 C++ 工具都能读取、索引和执行 xpkg 格式的包定义，而无需依赖 xlings 本身。

## 模块

libxpkg 由四个独立的 C++23 子模块组成，按依赖层级排列。可通过聚合 target `xpkg` 一次引入全部模块（`add_deps("xpkg")`），也可按需引用单个子模块：

| 模块 | 头文件 | 依赖 | 功能 |
|------|--------|------|------|
| `mcpplibs.xpkg` | `xpkg.cppm` | 无外部依赖 | 数据模型：`Package`、`PackageIndex` 等核心类型 |
| `mcpplibs.xpkg.loader` | `xpkg-loader.cppm` | model + lua | 解析 `.lua` 包定义文件，构建包索引 |
| `mcpplibs.xpkg.index` | `xpkg-index.cppm` | model | 搜索、别名解析、索引合并 |
| `mcpplibs.xpkg.executor` | `xpkg-executor.cppm` | model + lua | 执行包 Lua 钩子（install/config/uninstall） |

## 快速开始

### 读取包元数据

```cpp
#include <iostream>
import mcpplibs.xpkg;
import mcpplibs.xpkg.loader;

int main() {
    auto pkg = mcpplibs::xpkg::load_package("path/to/hello.lua");
    if (pkg) {
        std::cout << "name: " << pkg->name << "\n";
        std::cout << "desc: " << pkg->description << "\n";
    }
    return 0;
}
```

### 完整生命周期（加载 → 索引 → 搜索 → 安装 → 卸载）

```cpp
import mcpplibs.xpkg;
import mcpplibs.xpkg.loader;
import mcpplibs.xpkg.index;
import mcpplibs.xpkg.executor;

using namespace mcpplibs::xpkg;

// 1. 加载包
auto pkg = load_package("pkgindex/pkgs/h/hello.lua");

// 2. 构建索引并搜索
auto idx  = build_index("pkgindex/");
auto hits = search(*idx, "hello");

// 3. 创建执行器并运行钩子
auto exec = create_executor("pkgindex/pkgs/h/hello.lua");
ExecutionContext ctx{ .pkg_name = "hello", .version = "1.0.0", ... };
exec->run_hook(HookType::Install,   ctx);
exec->run_hook(HookType::Uninstall, ctx);
```

完整示例见 [`examples/lifecycle.cpp`](examples/lifecycle.cpp)。

## 项目结构

```
libxpkg/
├── src/
│   ├── xpkg.cppm              # mcpplibs.xpkg — 数据模型
│   ├── xpkg-loader.cppm       # mcpplibs.xpkg.loader
│   ├── xpkg-index.cppm        # mcpplibs.xpkg.index
│   ├── xpkg-executor.cppm     # mcpplibs.xpkg.executor
│   ├── xpkg-lua-stdlib.cppm   # Lua 标准库封装（自动生成）
│   └── lua-stdlib/            # 嵌入式 Lua 脚本
├── tests/
│   ├── fixtures/pkgindex/     # 独立测试包索引（无外部依赖）
│   ├── test_model.cpp
│   ├── test_loader.cppm
│   ├── test_index.cpp
│   ├── test_executor.cpp
│   └── xmake.lua
├── examples/
│   ├── basic.cpp              # 最小示例
│   ├── lifecycle.cpp          # 完整生命周期演示
│   └── xmake.lua
├── .agents/
│   ├── docs/                  # 开发问题排查报告
│   ├── plans/                 # 设计与实现方案
│   └── skills/                # 项目最佳实践
├── xmake.lua
└── CMakeLists.txt
```

## 构建与测试

**前置条件**：GCC 15+ 或 Clang 20+（需支持 C++23 模块）

```bash
# Linux（通过 xlings 安装 GCC 15）
xlings install gcc@15.1 -y
xmake f -m release -y
xmake

# macOS（通过 Homebrew 安装 LLVM 20）
brew install llvm@20
export PATH=/opt/homebrew/opt/llvm@20/bin:$PATH
xmake f --toolchain=llvm --sdk=/opt/homebrew/opt/llvm@20 -m release -y
xmake
```

**运行测试**

```bash
xmake run xpkg_model_test
xmake run xpkg_index_test
xmake run xpkg_loader_test
xmake run xpkg_executor_test
```

**运行示例**

```bash
xmake run basic
xmake run lifecycle
```

## CI/CD

GitHub Actions 在三个平台上自动构建和测试：

| 平台 | 工具链 | 状态 |
|------|--------|------|
| Linux (ubuntu-latest) | GCC 15.1 via Xlings | [![Linux](https://github.com/Sunrisepeak/libxpkg/actions/workflows/ci.yml/badge.svg)](https://github.com/Sunrisepeak/libxpkg/actions/workflows/ci.yml) |
| macOS 14 | LLVM 20 via Homebrew | [![macOS](https://github.com/Sunrisepeak/libxpkg/actions/workflows/ci.yml/badge.svg)](https://github.com/Sunrisepeak/libxpkg/actions/workflows/ci.yml) |
| Windows (latest) | MSVC via xmake | [![Windows](https://github.com/Sunrisepeak/libxpkg/actions/workflows/ci.yml/badge.svg)](https://github.com/Sunrisepeak/libxpkg/actions/workflows/ci.yml) |

## xmake 集成

**引入全部模块**（推荐）：

```lua
add_repositories("mcpplibs-index https://github.com/mcpplibs/mcpplibs-index.git")
add_requires("mcpplibs-xpkg")

target("myapp")
    set_kind("binary")
    set_languages("c++23")
    add_files("main.cpp")
    add_deps("xpkg")  -- 聚合 target，包含所有子模块
    set_policy("build.c++.modules", true)
```

**按需引入单个子模块**：

```lua
target("myapp")
    set_kind("binary")
    set_languages("c++23")
    add_files("main.cpp")
    add_deps("mcpplibs-xpkg", "mcpplibs-xpkg-loader")  -- 仅引入数据模型 + 加载器
    set_policy("build.c++.modules", true)
```

## 相关链接

- [xpkg 规范文档](https://github.com/d2learn/xim-pkgindex/blob/main/docs/V1/xpackage-spec.md)
- [xim-pkgindex — 官方包索引仓库](https://github.com/d2learn/xim-pkgindex)
- [xlings — xpkg 的参考实现](https://github.com/d2learn/xlings)
- [mcpp-style-ref — 现代 C++ 编码风格参考](https://github.com/mcpp-community/mcpp-style-ref)
- [mcpplibs/lua — C API Lua 绑定](https://github.com/mcpplibs/lua)
