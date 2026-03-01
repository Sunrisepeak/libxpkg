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

static const fs::path PKGINDEX  = fs::path(XPKG_FIXTURES_DIR) / "pkgindex";
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
    auto tmp_root    = fs::temp_directory_path() / "xpkg-lifecycle-demo";
    auto install_dir = tmp_root / "hello" / "1.0.0";
    fs::create_directories(install_dir);

    ExecutionContext ctx;
    ctx.pkg_name    = "hello";
    ctx.version     = "1.0.0";
    ctx.platform    = "linux";
    ctx.arch        = "x86_64";
    ctx.install_dir = install_dir;
    ctx.run_dir     = tmp_root;
    ctx.xpkg_dir    = tmp_root;
    ctx.bin_dir     = tmp_root / "bin";

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
    std::println("  version : {}",
        r_check.version.empty() ? "(not installed)" : r_check.version);

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
    fs::remove_all(tmp_root, ec);

    std::println("\n=== done ===");
    return 0;
}
