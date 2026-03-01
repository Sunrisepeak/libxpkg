module;

export module mcpplibs.xpkg.executor;
import mcpplibs.xpkg;
import mcpplibs.xpkg.lua_stdlib;
import mcpplibs.capi.lua;
import std;

namespace lua = mcpplibs::capi::lua;
namespace fs  = std::filesystem;

export namespace mcpplibs::xpkg {

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
    std::string version;  // non-empty when installed() returns a version string
};

struct XvmOp {
    std::string op;         // "add" | "remove" | "headers" | "remove_headers"
    std::string name;
    std::string version;
    std::string bindir;
    std::string alias;
    std::string type;       // "program" | "lib"
    std::string filename;
    std::string binding;
    std::string includedir; // for headers/remove_headers ops
};

enum class HookType { Installed, Build, Install, Config, Uninstall };

} // export namespace mcpplibs::xpkg

// Implementation detail (not exported)
namespace mcpplibs::xpkg::detail {

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

// Set a string field on the table at top of stack
void set_string_field(lua::State* L, std::string_view key, std::string_view val) {
    lua::pushstring(L, std::string(val).c_str());
    lua::setfield(L, -2, std::string(key).c_str());
}

// Load all xim.libxpkg.* modules into _LIBXPKG_MODULES table, then run prelude
bool load_stdlib(lua::State* L, std::string& err_out) {
    // Create empty _LIBXPKG_MODULES table
    lua::newtable(L);
    lua::setglobal(L, "_LIBXPKG_MODULES");

    // Each module script returns a table; store it into _LIBXPKG_MODULES[name]
    struct ModEntry { const char* name; std::string_view src; };
    const ModEntry mods[] = {
        { "log",     detail::log_lua    },
        { "pkginfo", detail::pkginfo_lua },
        { "system",  detail::system_lua },
        { "xvm",     detail::xvm_lua    },
        { "utils",   detail::utils_lua  },
    };

    for (auto& m : mods) {
        // Load and compile the module source
        if (lua::L_loadstring(L, m.src.data()) != lua::OK) {
            err_out = std::string("failed to compile module ") + m.name + ": "
                    + lua::tostring(L, -1);
            lua::pop(L, 1);
            return false;
        }
        // Execute the chunk, requesting 1 return value (the module table)
        if (lua::pcall(L, 0, 1, 0) != lua::OK) {
            err_out = std::string("failed to run module ") + m.name + ": "
                    + lua::tostring(L, -1);
            lua::pop(L, 1);
            return false;
        }
        // Stack: [module_table]
        // Store into _LIBXPKG_MODULES[name]
        lua::getglobal(L, "_LIBXPKG_MODULES");  // stack: [module_table, modules]
        lua::insert(L, -2);                      // stack: [modules, module_table]
        lua::setfield(L, -2, m.name);            // modules[name] = module_table; stack: [modules]
        lua::pop(L, 1);                          // stack: []
    }

    // Load prelude: defines import(), os.*, path.*, etc.
    if (lua::L_loadstring(L, detail::prelude_lua.data()) != lua::OK) {
        err_out = "failed to load prelude: " + std::string(lua::tostring(L, -1));
        lua::pop(L, 1);
        return false;
    }
    if (lua::pcall(L, 0, 0, 0) != lua::OK) {
        err_out = "failed to run prelude: " + std::string(lua::tostring(L, -1));
        lua::pop(L, 1);
        return false;
    }

    return true;
}

// Inject ExecutionContext into Lua as _RUNTIME global table
void inject_context(lua::State* L, const mcpplibs::xpkg::ExecutionContext& ctx) {
    lua::newtable(L);

    set_string_field(L, "pkg_name",        ctx.pkg_name);
    set_string_field(L, "version",          ctx.version);
    set_string_field(L, "platform",         ctx.platform);
    set_string_field(L, "arch",             ctx.arch);
    set_string_field(L, "install_file",     ctx.install_file.string());
    set_string_field(L, "install_dir",      ctx.install_dir.string());
    set_string_field(L, "run_dir",          ctx.run_dir.string());
    set_string_field(L, "xpkg_dir",         ctx.xpkg_dir.string());
    set_string_field(L, "bin_dir",          ctx.bin_dir.string());
    set_string_field(L, "subos_sysrootdir", ctx.subos_sysrootdir);

    // deps_list as array table
    lua::newtable(L);
    for (int i = 0; i < (int)ctx.deps_list.size(); ++i) {
        lua::pushstring(L, ctx.deps_list[i].c_str());
        lua::rawseti(L, -2, i + 1);
    }
    lua::setfield(L, -2, "deps_list");

    // args as array table
    lua::newtable(L);
    for (int i = 0; i < (int)ctx.args.size(); ++i) {
        lua::pushstring(L, ctx.args[i].c_str());
        lua::rawseti(L, -2, i + 1);
    }
    lua::setfield(L, -2, "args");

    lua::setglobal(L, "_RUNTIME");
}

} // namespace mcpplibs::xpkg::detail

// ---- PackageExecutor ----

export namespace mcpplibs::xpkg {

class PackageExecutor {
    lua::State* L_   = nullptr;
    fs::path    pkg_ ;

public:
    explicit PackageExecutor(lua::State* L, fs::path pkg)
        : L_(L), pkg_(std::move(pkg)) {}

    ~PackageExecutor() {
        if (L_) { lua::close(L_); L_ = nullptr; }
    }

    PackageExecutor(const PackageExecutor&)            = delete;
    PackageExecutor& operator=(const PackageExecutor&) = delete;

    PackageExecutor(PackageExecutor&& o) noexcept
        : L_(std::exchange(o.L_, nullptr)), pkg_(std::move(o.pkg_)) {}

    PackageExecutor& operator=(PackageExecutor&& o) noexcept {
        if (this != &o) {
            if (L_) lua::close(L_);
            L_   = std::exchange(o.L_, nullptr);
            pkg_ = std::move(o.pkg_);
        }
        return *this;
    }

    bool has_hook(HookType hook) const {
        auto name = detail::hook_name(hook);
        lua::getglobal(L_, std::string(name).c_str());
        bool found = (lua::type(L_, -1) == lua::TFUNCTION);
        lua::pop(L_, 1);
        return found;
    }

    HookResult run_hook(HookType hook, const ExecutionContext& ctx) {
        // Inject context before each hook call
        detail::inject_context(L_, ctx);

        auto name = detail::hook_name(hook);
        lua::getglobal(L_, std::string(name).c_str());

        if (lua::type(L_, -1) != lua::TFUNCTION) {
            lua::pop(L_, 1);
            return HookResult{ .success = false,
                               .error   = "hook not found: " + std::string(name) };
        }

        HookResult result;
        if (lua::pcall(L_, 0, 1, 0) == lua::OK) {
            int t = lua::type(L_, -1);
            if (t == lua::TBOOLEAN) {
                result.success = lua::toboolean(L_, -1);
            } else if (t == lua::TSTRING) {
                result.version = lua::tostring(L_, -1);
                result.success = !result.version.empty();
            } else {
                // nil or anything else: treat as success (hook ran without error)
                result.success = true;
            }
            lua::pop(L_, 1);
        } else {
            result.success = false;
            result.error   = lua::tostring(L_, -1);
            lua::pop(L_, 1);
        }
        return result;
    }

    HookResult check_installed(const ExecutionContext& ctx) {
        return run_hook(HookType::Installed, ctx);
    }

    std::vector<XvmOp> xvm_operations() {
        std::vector<XvmOp> ops;
        lua::getglobal(L_, "_XVM_OPS");
        if (lua::type(L_, -1) != lua::TTABLE) {
            lua::pop(L_, 1);
            return ops;
        }
        int len = (int)lua::rawlen(L_, -1);
        for (int i = 1; i <= len; ++i) {
            lua::rawgeti(L_, -1, i);
            if (lua::type(L_, -1) == lua::TTABLE) {
                XvmOp op;
                auto read_field = [&](const char* key) -> std::string {
                    lua::getfield(L_, -1, key);
                    std::string val;
                    if (lua::type(L_, -1) == lua::TSTRING)
                        val = lua::tostring(L_, -1);
                    lua::pop(L_, 1);
                    return val;
                };
                op.op         = read_field("op");
                op.name       = read_field("name");
                op.version    = read_field("version");
                op.bindir     = read_field("bindir");
                op.alias      = read_field("alias");
                op.type       = read_field("type");
                op.filename   = read_field("filename");
                op.binding    = read_field("binding");
                op.includedir = read_field("includedir");
                ops.push_back(std::move(op));
            }
            lua::pop(L_, 1);
        }
        lua::pop(L_, 1);
        return ops;
    }
};

// Factory
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

} // export namespace mcpplibs::xpkg
