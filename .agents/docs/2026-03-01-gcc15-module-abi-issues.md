# GCC 15 C++23 模块构建问题排查报告

**日期**：2026-03-01
**项目**：mcpplibs/libxpkg
**工具链**：GCC 15.1.0 + xmake + C++23 模块 (`import std;`)

---

## 问题一：before_build 生成的模块文件包含 `#include`

### 现象

`xmake.lua` 的 `before_build` hook 生成 `src/xpkg-lua-stdlib.cppm`，其开头为：

```cpp
#include <string_view>
export module mcpplibs.xpkg.lua_stdlib;
```

GCC 15 在处理 C++23 模块接口单元时，全局模块分段（global module fragment）之外的 `#include` 是非法的。若在模块声明后出现 `#include`，编译器会报错。

### 根因

`before_build` 脚本沿用了旧的写法，直接 `#include` 标准头而非使用模块导入。

### 解决

在 `xmake.lua` 的 before_build 中，将生成逻辑改为：

```lua
f:write("module;\n")
f:write("export module mcpplibs.xpkg.lua_stdlib;\n")
f:write("import std;\n\n")   -- 替换原来的 #include <string_view>
```

**原则**：libxpkg 所有 `.cppm` 模块文件一律用 `import std;` 引入标准库，禁止在模块声明后使用 `#include`。全局模块分段（`module;` 和 `export module ...;` 之间）仅允许 `#include` 无法被模块化的第三方 C 头文件（如 `lua.h`）。

---

## 问题二：xpkg-index.cppm 中结构化绑定语法错误

### 现象

编译报错，代码如下：

```cpp
for (auto& [, members] : index.mutex_groups) {
```

错误信息：

```
error: expected identifier before ',' token
```

### 根因

C++23 标准**不支持**匿名结构化绑定（`[, value]` 中用 `,` 跳过第一个成员）。该语法属于 C++26 提案（P2169），GCC 15 默认的 `-std=c++23` 下不可用。

### 解决

改用具名绑定，再用 `(void)` 压制未使用警告：

```cpp
for (auto& [gkey, members] : index.mutex_groups) {
    (void)gkey;
    // 只使用 members
}
```

---

## 问题三：xpkg_loader_test 链接失败（GCC 15 模块 ABI 问题）★

这是本次最核心、最复杂的问题。

### 现象

`xpkg_loader_test` 在链接阶段报大量 `undefined reference`：

```
undefined reference to `std::__detail::_Hashtable_alloc<
    std::__detail::_Hash_node_base<
        std::__detail::_Hash_node<
            std::pair<const std::__cxx11::basic_string<char>,
                      mcpplibs::xpkg@mcpplibs.xpkg::IndexEntry>,
            false> *> *>::~_Hashtable_alloc()'

undefined reference to `std::_Vector_base<
    std::__cxx11::basic_string<char>, ...>::_Vector_impl::~_Vector_impl()'
```

注意类型名中的 `@mcpplibs.xpkg` 后缀——这是 GCC 的模块分区类型标识符。

### 排查过程

**阶段 1：怀疑 `import std;` 与 `#include <gtest/gtest.h>` 冲突**

将测试文件改为模块接口单元（`.cppm`），把 `#include <gtest/gtest.h>` 放入全局模块分段——问题依旧。

**阶段 2：怀疑编译选项问题**

尝试 `-fvisibility=default`、`-fkeep-inline-functions`——均无效。

**阶段 3：用 `-O0` 验证**

`xmake f -m debug` 后构建成功！确认问题与**优化级别**有关，`-O2` 触发，`-O0` 不触发。

**阶段 4：最小化复现**

将测试缩减到仅一个测试函数，并且该函数只调用返回错误的路径（不构造 `Package` 对象）。构建**仍然失败**，错误符号为：

```
test_loader.cppm:(.text._ZN8mcpplibs4xpkgW8mcpplibsW4xpkg7PackageD2Ev[...])
  undefined reference to `~_Vector_impl<string>()'
```

说明问题不在于测试逻辑，而在于**模块析构函数本身的代码生成方式**。

### 根因分析

GCC 15 在处理 C++23 模块时，会将模块接口中定义在类体内的析构函数（隐式 `= default` 或显式 inline）的 **BMI（Binary Module Interface）**标记为可内联。

当 `test_loader.cppm` 导入 `mcpplibs.xpkg` 并在 `-O2` 下编译时：

1. GCC 将 `Package::~Package()@mcpplibs.xpkg` **内联展开**进测试 TU
2. 展开后的代码需要调用 `std::_Vector_base<string>::_Vector_impl::~_Vector_impl()`
3. 该函数在 `import std;` 的 BMI 中也是 **inline**，GCC 在优化时选择 **CALL** 而非内联它
4. 最终链接时，没有任何 `.o` 文件提供 `~_Vector_impl` 的 outlined symbol → **链接失败**

其他三个测试目标（`model`、`index`、`executor`）不受影响，因为它们的测试 TU 是普通 `.cpp` 文件，GCC 对 `.cpp` 文件的优化策略与模块 TU 不同。

### 解决方案

在 `src/xpkg.cppm` 中，对所有含 `std::vector` / `std::unordered_map` 成员的结构体，**显式声明**析构函数（不在类体内定义），然后在 export namespace 块**外部**用 `= default` 定义：

```cpp
// 类体内：仅声明，不定义
struct PlatformMatrix {
    std::unordered_map<...> entries;
    std::unordered_map<...> deps;
    std::unordered_map<...> inherits;
    ~PlatformMatrix();   // ← 声明只，不加 = default
};

struct Package {
    // ... 所有成员 ...
    ~Package();          // ← 同上
};

struct PackageIndex {
    std::unordered_map<...> entries;
    std::unordered_map<...> mutex_groups;
    ~PackageIndex();     // ← 同上
};

struct IndexRepos {
    std::vector<RepoConfig> sub_repos;
    ~IndexRepos();       // ← 同上
};

} // namespace mcpplibs::xpkg  ← export namespace 结束

// 模块 TU 的文件作用域，export namespace 之外
namespace mcpplibs::xpkg {
PlatformMatrix::~PlatformMatrix() = default;
Package::~Package()               = default;
PackageIndex::~PackageIndex()     = default;
IndexRepos::~IndexRepos()         = default;
}
```

**为什么有效？**

- 析构函数在类体外定义 → GCC 不将其标记为 inline，而是生成 **outlined symbol** 写入 `libmcpplibs-xpkg.a`
- 导入方（测试 TU）在 `-O2` 下遇到 `~Package()` 时，读取 BMI 知道这是 outlined 函数 → 生成 **CALL** 指令
- 链接时从 `libmcpplibs-xpkg.a` 找到符号 → 链接成功

### 关键结论

> **GCC 15 C++23 模块中，若结构体含 `std::vector` / `std::unordered_map` 等模板容器成员，析构函数必须在类体外（export namespace 块外）定义为 `= default`，否则在 `-O2` 下导入方链接会出现 `undefined reference` 到 std 内部 inline 函数。**

---

## 问题四：修改 xpkg.cppm 后缓存失效

### 现象

修改 `xpkg.cppm`（添加析构函数声明）后，`xmake build` 报错：

```
error: failed to read compiled module cluster: Bad file data
```

### 根因

xmake 的增量构建缓存中存有旧版本的 BMI 文件，与新的模块接口不兼容。

### 解决

```bash
xmake clean --all
xmake build xpkg_loader_test
```

**原则**：修改模块接口文件（`.cppm`）后，尤其是改变导出接口时，必须先 `xmake clean --all` 再构建。

---

## 最终结果

| 测试目标 | 测试数 | 结果 |
|---|---|---|
| `xpkg_model_test` | 5 | ✅ PASSED |
| `xpkg_index_test` | 18 | ✅ PASSED |
| `xpkg_loader_test` | 4 | ✅ PASSED |
| `xpkg_executor_test` | 6 | ✅ PASSED |
| **合计** | **33** | **✅ 全部通过** |

---

## 经验总结

1. **模块文件禁止 `#include` 标准头**：统一用 `import std;`，`#include` 仅用于全局模块分段中的 C 库头文件
2. **C++23 结构化绑定不支持匿名占位符**：用具名变量 + `(void)` 代替
3. **含容器成员的结构体析构函数必须 outlined**：在类体外 `= default` 定义，防止 GCC 15 `-O2` 内联后产生悬空 CALL
4. **修改模块接口后必须 `xmake clean --all`**：避免 BMI 缓存不一致导致的神秘错误
5. **分离测试目标便于定位构建问题**：每个模块独立一个测试二进制，能快速缩小出错范围
