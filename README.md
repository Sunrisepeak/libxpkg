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
│   └── test_executor.cpp
├── examples/
│   ├── basic.cpp              # 最小示例
│   └── lifecycle.cpp          # 完整生命周期演示
├── build.mcpp                 # 构建期由 lua-stdlib/ 生成嵌入式 Lua stdlib
├── .agents/
│   ├── docs/                  # 开发问题排查报告
│   ├── plans/                 # 设计与实现方案
│   └── skills/                # 项目最佳实践
└── mcpp.toml
```

## 构建与测试

本库由 **mcpp** 构建。mcpp 自带受管沙箱(工具链由 `mcpp.toml` 固定),
不需要手工准备编译器,也不再有 xmake / CMake 构建入口。

```bash
xlings install -y   # 装 .xlings.json 里固定的 mcpp
mcpp build
mcpp test
```

macOS 专用的 elfpatch 用例需要 `install_name_tool`,在 Linux 上跑不了,
CI 里按名字过滤:

```bash
mcpp test -- --gtest_filter=-ExecutorTest.ApplyElfpatchAuto_*
```

**Lua stdlib 的生成**

`src/lua-stdlib/**.lua` 是唯一的源码。它需要以裸字符串字面量的形式进二进制,
这一步由 `build.mcpp` 在构建期完成:生成的 `xpkg-lua-stdlib.cppm` 写进
`MCPP_OUT_DIR`,通过 `generated=` 交给构建,**不进版本库**。

所以改完 `.lua` 直接 `mcpp build` 即可,没有"忘记重新生成"这回事 ——
仓库里只有一份拷贝,两份拷贝对不上这种状态不存在。

(它曾经存在过:0.0.47 的 `xvm.files` 只改了生成物,源 `.lua` 一直没有。)

## CI/CD

GitHub Actions 在 Linux 上构建并测试,工具链由 mcpp 沙箱提供:

| 平台 | 构建 | 状态 |
|------|------|------|
| Linux (ubuntu-latest) | mcpp(沙箱内 GCC) | [![CI](https://github.com/openxlings/libxpkg/actions/workflows/ci.yml/badge.svg)](https://github.com/openxlings/libxpkg/actions/workflows/ci.yml) |

## 作为依赖引入

在使用方的 `mcpp.toml` 里声明:

```toml
[dependencies]
"mcpplibs.xpkg" = "0.0.48"
```

## 相关链接

- [xpkg 规范文档](https://github.com/d2learn/xim-pkgindex/blob/main/docs/V1/xpackage-spec.md)
- [xim-pkgindex — 官方包索引仓库](https://github.com/d2learn/xim-pkgindex)
- [xlings — xpkg 的参考实现](https://github.com/d2learn/xlings)
- [mcpp-style-ref — 现代 C++ 编码风格参考](https://github.com/mcpp-community/mcpp-style-ref)
- [mcpplibs/lua — C API Lua 绑定](https://github.com/mcpplibs/lua)
