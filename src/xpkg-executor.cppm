module;

export module mcpplibs.xpkg.executor;
import mcpplibs.xpkg;
import mcpplibs.xpkg.lua_stdlib;
import mcpplibs.capi.lua;
import std;

namespace lua = mcpplibs::capi::lua;
namespace fs  = std::filesystem;

export namespace mcpplibs::xpkg {

// Slim per-dep export info pre-resolved by xlings: paths are already
// joined with the dep's install_dir, so the Lua side gets ready-to-use
// absolute paths. Only the fields elfpatch.lua actually needs are
// surfaced — additional `exports.*` capabilities (data, build) will be
// injected via separate _RUNTIME tables when those land.
struct DepExport {
    std::string loader;                       // absolute path or empty
    std::vector<std::string> libdirs;         // absolute paths (already joined)
    std::string abi;                          // e.g. "linux-x86_64-glibc"
};

// One runtime dependency, as the RESOLVER settled it — not as the recipe
// spelled it.
//
// This is the record that makes "which version is this dependency" have one
// answer. `DepExport` below cannot serve: it carries only what a dep
// explicitly declared, so a dep that declared nothing is absent from it, and
// absence was defined to mean "fall back to convention" — which is a second
// answerer wearing a different hat. Every runtime dep appears here, declared
// or not.
struct ResolvedDep {
    std::string spec;         // the recipe's own text, e.g. "xim:glibc@>=2.38"
    std::string name;         // canonical, e.g. "xim:glibc"
    std::string version;      // what it resolved TO, e.g. "2.44"
    std::string install_dir;  // absolute payload directory — the authority
    std::vector<std::string> libdirs;  // absolute; convention-filled when the
                                       // dep declared none, so no consumer
                                       // has to re-derive it
    std::string source;       // why this one: "plan" | "pinned-active" | ...
};

struct ExecutionContext {
    std::string pkg_name, version, platform, arch;
    fs::path install_file, install_dir;
    fs::path run_dir, xpkg_dir, bin_dir;
    fs::path project_data_dir;  // project-local data root (empty when no project config)
    // `deps_list` retained as the legacy union (runtime ∪ build) for
    // backward compat with old install hooks; new code should consult
    // `runtime_deps_list` / `build_deps_list` to get the split.
    std::vector<std::string> deps_list, args;
    std::vector<std::string> runtime_deps_list;
    std::vector<std::string> build_deps_list;
    // Pre-resolved exports of each runtime dep. Key is the dep spec as
    // it appears in runtime_deps_list (e.g. "xim:glibc@2.39"). Only
    // deps that actually declare exports show up; missing entries mean
    // "this dep declared nothing — fall back to convention".
    std::unordered_map<std::string, DepExport> deps_exports;
    // Keyed by the same spec string as deps_exports. Authoritative for runtime
    // deps resolved in this plan, but not total across separate host dependency
    // domains; those stores are supplied explicitly below.
    std::unordered_map<std::string, ResolvedDep> resolved_deps;
    // Ordered host store roots for dependencies outside this resolver plan.
    // Presence in _RUNTIME marks a modern context even when the vector is empty.
    std::vector<fs::path> dependency_store_roots;
    // The current package's own exports (rule 2 in the predicate trigger).
    DepExport self_exports;
    std::string subos_sysrootdir;
    std::string pkgindex_dir;    // package index repo root (for custom module loading)
};

inline constexpr std::size_t kMaxHookOutputBytes = 16 * 1024;

struct HookResult {
    bool success = false;
    std::string output, error;
    std::string version;  // non-empty when installed() returns a version string
};

struct XvmOp {
    std::string op;         // "add" | "remove" | "headers" | "remove_headers"
                            // | "subos_env"
    std::string name;
    std::string version;
    std::string bindir;
    std::string alias;
    std::string type;       // "program" | "lib" | "files"
    std::string filename;
    std::string binding;
    std::string includedir; // for headers/remove_headers ops

    // type = "files": one asset the package places into the subos.
    //
    // Both ends are relative, and that is a requirement rather than a
    // convention. A payload is shared between subos and reference-counted,
    // so an absolute destination recorded against it would be wrong for
    // every subos but the one that installed it. `src` is relative to the
    // payload root, `dst` to the subos root; the consumer resolves them and
    // rejects anything absolute or escaping.
    //
    // Exists because `includedir` can only say "this one directory becomes
    // sysroot include". It cannot express a destination, an asset that is
    // not a header, or a source and destination that differ in name --
    // openssl's `lib64/` -> `usr/lib/` is all three at once. Without a way
    // to say it, package indexes grow their own file-placing helpers, and
    // the tool managing versions cannot see or undo any of them.
    std::string src;
    std::string dst;

    // Arguments injected ahead of the user's own when a program shim
    // dispatches. Separate from `alias` on purpose: the only way to inject
    // anything today is to append it to the alias string, which consumers
    // then split on the first space. That breaks on any path containing one,
    // and it makes every reader of `alias` -- version listings, diagnostics
    // -- report a command line where a name belongs.
    std::vector<std::string> args;

    // op = "subos_env": one environment variable the subos exports to every
    // process that enters it.
    //
    // Distinct from `envs` above, which is the environment a single program
    // shim sets for itself when dispatched. That scope cannot serve a
    // discovery protocol: the program that has to see LIBGL_DRIVERS_PATH is
    // the user's own binary, which xlings never wraps. Same reason `src`/`dst`
    // exist rather than another meaning piled onto `includedir` -- a new scope
    // needs its own fields, or every reader has to know which op it is looking
    // at before it can trust a value.
    //
    // `mode` carries the recipe's `op = "set"|"prepend"` argument, because
    // `op` itself is already the category the consumer dispatches on.
    std::string var;
    std::string value;
    std::string mode;

    std::vector<std::pair<std::string, std::string>> envs; // environment variables
};

struct InstallRequest {
    std::string op;      // "install" | "remove"
    std::string target;  // e.g. "scode:linux-headers@5.11.1"
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

// Register C++ std::filesystem implementations of os.isdir and os.dirs.
// Called after the Lua prelude to override the shell-based versions.
void register_os_funcs(lua::State* L) {
    lua::getglobal(L, "os");
    if (lua::type(L, -1) != lua::TTABLE) {
        lua::pop(L, 1);
        lua::newtable(L);
        lua::setglobal(L, "os");
        lua::getglobal(L, "os");
    }

    // os.isdir(path) -> bool
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* p = lua::tostring(L, 1);
        if (!p) { lua::pushboolean(L, 0); return 1; }
        std::error_code ec;
        lua::pushboolean(L, fs::is_directory(fs::path(p), ec) ? 1 : 0);
        return 1;
    });
    lua::setfield(L, -2, "isdir");

    // os.dirs(pattern) -> table of absolute dir paths
    // Accepts "base/*" style pattern; strips trailing /* to get the base directory.
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* pat = lua::tostring(L, 1);
        lua::newtable(L);
        if (!pat) return 1;
        std::string s(pat);
        // Strip trailing /* or \*
        if (s.size() >= 2 && s.back() == '*' &&
            (s[s.size()-2] == '/' || s[s.size()-2] == '\\')) {
            s.pop_back(); s.pop_back();
        }
        while (!s.empty() && (s.back() == '/' || s.back() == '\\'))
            s.pop_back();
        fs::path base(s);
        std::error_code ec;
        if (!fs::is_directory(base, ec)) return 1;
        int idx = 1;
        for (auto& entry : fs::directory_iterator(base, ec)) {
            if (entry.is_directory(ec)) {
                lua::pushstring(L, entry.path().string().c_str());
                lua::rawseti(L, -2, idx++);
            }
        }
        return 1;
    });
    lua::setfield(L, -2, "dirs");

    // os.cd(dir) -> bool
    // Real chdir so that subsequent os.execute / system.exec inherit the new CWD.
    // Safe: hook calls are wrapped in ScopedCurrentDir_ which restores CWD on return.
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* p = lua::tostring(L, 1);
        if (!p) { lua::pushboolean(L, 0); return 1; }
        std::error_code ec;
        fs::current_path(fs::path(p), ec);
        lua::pushboolean(L, ec ? 0 : 1);
        return 1;
    });
    lua::setfield(L, -2, "cd");

    // os.cp(src, dst) -> bool
    // Mimics `cp -a`: when src is a dir and dst is an existing dir,
    // copies src as a subdirectory of dst (i.e. dst/src_name/...).
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* src = lua::tostring(L, 1);
        const char* dst = lua::tostring(L, 2);
        if (!src || !dst) { lua::pushboolean(L, 0); return 1; }
        fs::path sp(src), dp(dst);
        std::error_code ec;
        // cp -a semantics: dir into existing dir → dst/basename(src)/...
        if (fs::is_directory(sp, ec) && fs::is_directory(dp, ec))
            dp /= sp.filename();
        fs::copy(sp, dp,
                 fs::copy_options::recursive |
                 fs::copy_options::copy_symlinks |
                 fs::copy_options::overwrite_existing, ec);
        lua::pushboolean(L, ec ? 0 : 1);
        return 1;
    });
    lua::setfield(L, -2, "cp");

    // os.trymv(src, dst) -> bool  (rename, fallback to copy+remove)
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* s = lua::tostring(L, 1);
        const char* d = lua::tostring(L, 2);
        if (!s || !d) { lua::pushboolean(L, 0); return 1; }
        fs::path src(s), dst(d);
        std::error_code ec;
        // If dst is existing dir, move into it (unix mv semantics)
        if (fs::is_directory(dst, ec)) dst /= src.filename();
        fs::rename(src, dst, ec);
        if (!ec) { lua::pushboolean(L, 1); return 1; }
        // Cross-device: copy + remove
        ec.clear();
        fs::copy(src, dst,
                 fs::copy_options::recursive |
                 fs::copy_options::copy_symlinks |
                 fs::copy_options::overwrite_existing, ec);
        if (ec) { lua::pushboolean(L, 0); return 1; }
        fs::remove_all(src, ec);
        lua::pushboolean(L, 1);
        return 1;
    });
    lua::setfield(L, -2, "trymv");
    // Also set os.mv = os.trymv
    lua::getfield(L, -1, "trymv");
    lua::setfield(L, -2, "mv");

    // os.tryrm(path) -> bool
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* p = lua::tostring(L, 1);
        if (!p) { lua::pushboolean(L, 0); return 1; }
        std::error_code ec;
        fs::remove_all(fs::path(p), ec);
        lua::pushboolean(L, 1);
        return 1;
    });
    lua::setfield(L, -2, "tryrm");

    // os.mkdir(path) -> bool
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* p = lua::tostring(L, 1);
        if (!p) { lua::pushboolean(L, 0); return 1; }
        std::error_code ec;
        fs::create_directories(fs::path(p), ec);
        lua::pushboolean(L, ec ? 0 : 1);
        return 1;
    });
    lua::setfield(L, -2, "mkdir");

    // os.isfile(path) -> bool
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* p = lua::tostring(L, 1);
        if (!p) { lua::pushboolean(L, 0); return 1; }
        std::error_code ec;
        lua::pushboolean(L, fs::is_regular_file(fs::path(p), ec) ? 1 : 0);
        return 1;
    });
    lua::setfield(L, -2, "isfile");

    // os.iorun(cmd) -> string  (capture stdout, discard stderr)
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* cmd = lua::tostring(L, 1);
        if (!cmd) { lua::pushstring(L, ""); return 1; }
        // Build a temp path for capturing output
        std::error_code ec;
        auto tmp = fs::temp_directory_path(ec) / ("xpkg_iorun_" +
            std::to_string(std::hash<std::string>{}(std::string(cmd) +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))));
        std::string full(cmd);
#ifdef _WIN32
        full += " 2>nul > \"" + tmp.string() + "\"";
#else
        full += " 2>/dev/null > \"" + tmp.string() + "\"";
#endif
        std::system(full.c_str());
        std::ifstream ifs(tmp);
        std::string out;
        if (ifs.good()) {
            std::ostringstream ss;
            ss << ifs.rdbuf();
            out = ss.str();
        }
        ifs.close();
        fs::remove(tmp, ec);
        lua::pushstring(L, out.c_str());
        return 1;
    });
    lua::setfield(L, -2, "iorun");

    lua::pop(L, 1); // pop os table
}

// Register C++-backed fs module into _LIBXPKG_MODULES["fs"].
// Provides filesystem primitives that are missing from the os.* layer:
// symlink creation/reading, file/dir enumeration, single-file copy, etc.
// All functions use std::filesystem — no shell commands.
void register_fs_module(lua::State* L) {
    lua::newtable(L);  // the fs module table

    // fs.symlink(src, dst) -> bool
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* src = lua::tostring(L, 1);
        const char* dst = lua::tostring(L, 2);
        if (!src || !dst) { lua::pushboolean(L, 0); return 1; }
        std::error_code ec;
        fs::path sp(src);
        if (fs::is_directory(sp, ec))
            fs::create_directory_symlink(sp, fs::path(dst), ec);
        else
            fs::create_symlink(sp, fs::path(dst), ec);
        lua::pushboolean(L, ec ? 0 : 1);
        return 1;
    });
    lua::setfield(L, -2, "symlink");

    // fs.readlink(path) -> string | nil
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* p = lua::tostring(L, 1);
        if (!p) { lua::pushnil(L); return 1; }
        std::error_code ec;
        auto target = fs::read_symlink(fs::path(p), ec);
        if (ec) { lua::pushnil(L); return 1; }
        lua::pushstring(L, target.string().c_str());
        return 1;
    });
    lua::setfield(L, -2, "readlink");

    // fs.is_symlink(path) -> bool
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* p = lua::tostring(L, 1);
        if (!p) { lua::pushboolean(L, 0); return 1; }
        std::error_code ec;
        lua::pushboolean(L, fs::is_symlink(fs::path(p), ec) ? 1 : 0);
        return 1;
    });
    lua::setfield(L, -2, "is_symlink");

    // fs.exists(path) -> bool  (follows symlinks)
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* p = lua::tostring(L, 1);
        if (!p) { lua::pushboolean(L, 0); return 1; }
        std::error_code ec;
        lua::pushboolean(L, fs::exists(fs::path(p), ec) ? 1 : 0);
        return 1;
    });
    lua::setfield(L, -2, "exists");

    // fs.is_directory(path) -> bool
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* p = lua::tostring(L, 1);
        if (!p) { lua::pushboolean(L, 0); return 1; }
        std::error_code ec;
        lua::pushboolean(L, fs::is_directory(fs::path(p), ec) ? 1 : 0);
        return 1;
    });
    lua::setfield(L, -2, "is_directory");

    // fs.is_file(path) -> bool
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* p = lua::tostring(L, 1);
        if (!p) { lua::pushboolean(L, 0); return 1; }
        std::error_code ec;
        lua::pushboolean(L, fs::is_regular_file(fs::path(p), ec) ? 1 : 0);
        return 1;
    });
    lua::setfield(L, -2, "is_file");

    // fs.mkdir_p(path) -> bool
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* p = lua::tostring(L, 1);
        if (!p) { lua::pushboolean(L, 0); return 1; }
        std::error_code ec;
        fs::create_directories(fs::path(p), ec);
        lua::pushboolean(L, ec ? 0 : 1);
        return 1;
    });
    lua::setfield(L, -2, "mkdir_p");

    // fs.remove(path) -> bool  (single file or empty dir)
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* p = lua::tostring(L, 1);
        if (!p) { lua::pushboolean(L, 0); return 1; }
        std::error_code ec;
        lua::pushboolean(L, fs::remove(fs::path(p), ec) ? 1 : 0);
        return 1;
    });
    lua::setfield(L, -2, "remove");

    // fs.remove_all(path) -> bool
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* p = lua::tostring(L, 1);
        if (!p) { lua::pushboolean(L, 0); return 1; }
        std::error_code ec;
        fs::remove_all(fs::path(p), ec);
        lua::pushboolean(L, 1);
        return 1;
    });
    lua::setfield(L, -2, "remove_all");

    // fs.copy_file(src, dst) -> bool  (single file, overwrite)
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* src = lua::tostring(L, 1);
        const char* dst = lua::tostring(L, 2);
        if (!src || !dst) { lua::pushboolean(L, 0); return 1; }
        std::error_code ec;
        fs::copy_file(fs::path(src), fs::path(dst),
                      fs::copy_options::overwrite_existing, ec);
        lua::pushboolean(L, ec ? 0 : 1);
        return 1;
    });
    lua::setfield(L, -2, "copy_file");

    // fs.entries(dir) -> table of {path, name, type}
    // type: "file", "directory", "symlink", "other"
    // Non-recursive. Returns full paths.
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* dir = lua::tostring(L, 1);
        lua::newtable(L);
        if (!dir) return 1;
        std::error_code ec;
        fs::path dp(dir);
        if (!fs::is_directory(dp, ec)) return 1;
        int idx = 1;
        for (auto& entry : fs::directory_iterator(dp, ec)) {
            lua::newtable(L);
            lua::pushstring(L, entry.path().string().c_str());
            lua::setfield(L, -2, "path");
            lua::pushstring(L, entry.path().filename().string().c_str());
            lua::setfield(L, -2, "name");
            const char* etype = "other";
            if (entry.is_symlink(ec))          etype = "symlink";
            else if (entry.is_regular_file(ec)) etype = "file";
            else if (entry.is_directory(ec))    etype = "directory";
            lua::pushstring(L, etype);
            lua::setfield(L, -2, "type");
            lua::rawseti(L, -2, idx++);
        }
        return 1;
    });
    lua::setfield(L, -2, "entries");

    // fs.files(dir, recursive?) -> table of file paths
    // recursive: optional bool, default false
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* dir = lua::tostring(L, 1);
        bool recursive = lua::toboolean(L, 2);
        lua::newtable(L);
        if (!dir) return 1;
        std::error_code ec;
        fs::path dp(dir);
        if (!fs::is_directory(dp, ec)) return 1;
        int idx = 1;
        if (recursive) {
            for (auto& entry : fs::recursive_directory_iterator(dp, ec)) {
                if (entry.is_regular_file(ec)) {
                    lua::pushstring(L, entry.path().string().c_str());
                    lua::rawseti(L, -2, idx++);
                }
            }
        } else {
            for (auto& entry : fs::directory_iterator(dp, ec)) {
                if (entry.is_regular_file(ec)) {
                    lua::pushstring(L, entry.path().string().c_str());
                    lua::rawseti(L, -2, idx++);
                }
            }
        }
        return 1;
    });
    lua::setfield(L, -2, "files");

    // fs.dirs(dir) -> table of subdirectory paths (non-recursive)
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* dir = lua::tostring(L, 1);
        lua::newtable(L);
        if (!dir) return 1;
        std::error_code ec;
        fs::path dp(dir);
        if (!fs::is_directory(dp, ec)) return 1;
        int idx = 1;
        for (auto& entry : fs::directory_iterator(dp, ec)) {
            if (entry.is_directory(ec)) {
                lua::pushstring(L, entry.path().string().c_str());
                lua::rawseti(L, -2, idx++);
            }
        }
        return 1;
    });
    lua::setfield(L, -2, "dirs");

    // fs.basename(path) -> string
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* p = lua::tostring(L, 1);
        if (!p) { lua::pushstring(L, ""); return 1; }
        lua::pushstring(L, fs::path(p).filename().string().c_str());
        return 1;
    });
    lua::setfield(L, -2, "basename");

    // fs.dirname(path) -> string
    lua::pushcfunction(L, [](lua::State* L) -> int {
        const char* p = lua::tostring(L, 1);
        if (!p) { lua::pushstring(L, ""); return 1; }
        lua::pushstring(L, fs::path(p).parent_path().string().c_str());
        return 1;
    });
    lua::setfield(L, -2, "dirname");

    // Store into _LIBXPKG_MODULES["fs"]
    lua::getglobal(L, "_LIBXPKG_MODULES");
    lua::insert(L, -2);       // stack: [modules, fs_table]
    lua::setfield(L, -2, "fs");  // modules["fs"] = fs_table
    lua::pop(L, 1);           // pop modules
}

// Load all xim.libxpkg.* modules into _LIBXPKG_MODULES table, then run prelude
bool load_stdlib(lua::State* L, std::string& err_out) {
    // Create empty _LIBXPKG_MODULES table
    lua::newtable(L);
    lua::setglobal(L, "_LIBXPKG_MODULES");

    // Each module script returns a table; store it into _LIBXPKG_MODULES[name]
    struct ModEntry { const char* name; std::string_view src; };
    const ModEntry mods[] = {
        { "log",        detail::log_lua        },
        { "pkginfo",    detail::pkginfo_lua    },
        { "system",     detail::system_lua     },
        { "subos",      detail::subos_lua      },
        { "xvm",        detail::xvm_lua        },
        { "utils",      detail::utils_lua      },
        { "pkgmanager", detail::pkgmanager_lua },
        { "elfpatch",   detail::elfpatch_lua   },
        { "json",       detail::json_lua       },
        { "base64",     detail::base64_lua     },
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

    // Override shell-based os.* with C++ std::filesystem implementations
    register_os_funcs(L);

    // Register C++-backed fs module (symlink, entries, etc.)
    register_fs_module(L);

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
    set_string_field(L, "xpkg_dir",          ctx.xpkg_dir.string());
    set_string_field(L, "bin_dir",           ctx.bin_dir.string());
    set_string_field(L, "project_data_dir",  ctx.project_data_dir.string());
    set_string_field(L, "subos_sysrootdir",  ctx.subos_sysrootdir);
    set_string_field(L, "pkgindex_dir",      ctx.pkgindex_dir);

    auto push_string_array = [&](const std::vector<std::string>& v, const char* field) {
        lua::newtable(L);
        for (int i = 0; i < (int)v.size(); ++i) {
            lua::pushstring(L, v[i].c_str());
            lua::rawseti(L, -2, i + 1);
        }
        lua::setfield(L, -2, field);
    };
    push_string_array(ctx.deps_list,         "deps_list");
    push_string_array(ctx.runtime_deps_list, "runtime_deps_list");
    push_string_array(ctx.build_deps_list,   "build_deps_list");
    std::vector<std::string> dependencyStoreRoots;
    dependencyStoreRoots.reserve(ctx.dependency_store_roots.size());
    for (const auto& root : ctx.dependency_store_roots) {
        dependencyStoreRoots.push_back(root.string());
    }
    push_string_array(dependencyStoreRoots, "dependency_store_roots");

    // deps_exports: { [dep_spec] = { loader, libdirs, abi }, ... }
    // Only deps that declared exports show up here.
    lua::newtable(L);
    for (auto& [dep_spec, e] : ctx.deps_exports) {
        lua::newtable(L);
        set_string_field(L, "loader", e.loader);
        set_string_field(L, "abi",    e.abi);
        push_string_array(e.libdirs, "libdirs");
        lua::setfield(L, -2, dep_spec.c_str());
    }
    lua::setfield(L, -2, "deps_exports");

    // resolved_deps: { [spec] = { name, version, install_dir, libdirs, source } }
    // Authoritative for dependencies represented by this resolver plan, unlike
    // deps_exports. Other host dependency domains are represented by the
    // explicit dependency_store_roots above.
    lua::newtable(L);
    for (auto& [dep_spec, r] : ctx.resolved_deps) {
        lua::newtable(L);
        set_string_field(L, "name",        r.name);
        set_string_field(L, "version",     r.version);
        set_string_field(L, "install_dir", r.install_dir);
        set_string_field(L, "source",      r.source);
        push_string_array(r.libdirs, "libdirs");
        lua::setfield(L, -2, dep_spec.c_str());
    }
    lua::setfield(L, -2, "resolved_deps");

    // self_exports: same shape as a single deps_exports entry. Empty
    // strings/arrays when the current package didn't declare exports.
    lua::newtable(L);
    set_string_field(L, "loader", ctx.self_exports.loader);
    set_string_field(L, "abi",    ctx.self_exports.abi);
    push_string_array(ctx.self_exports.libdirs, "libdirs");
    lua::setfield(L, -2, "self_exports");

    // args as array table
    lua::newtable(L);
    for (int i = 0; i < (int)ctx.args.size(); ++i) {
        lua::pushstring(L, ctx.args[i].c_str());
        lua::rawseti(L, -2, i + 1);
    }
    lua::setfield(L, -2, "args");

    lua::setglobal(L, "_RUNTIME");
}

constexpr std::string_view HOOK_OUTPUT_TRUNCATED_MARKER =
    "\n[libxpkg: hook output truncated]\n";

class HookOutput {
    std::string bytes_;
    bool truncated_ = false;

    static bool is_continuation_byte_(unsigned char byte) {
        return (byte & 0xc0) == 0x80;
    }

    static std::size_t valid_sequence_size_(std::string_view bytes,
                                            std::size_t offset) {
        const auto lead = static_cast<unsigned char>(bytes[offset]);
        if (lead <= 0x7f) return 1;

        std::size_t size = 0;
        std::uint32_t codePoint = 0;
        std::uint32_t minimum = 0;
        if ((lead & 0xe0) == 0xc0) {
            size = 2;
            codePoint = lead & 0x1f;
            minimum = 0x80;
        } else if ((lead & 0xf0) == 0xe0) {
            size = 3;
            codePoint = lead & 0x0f;
            minimum = 0x800;
        } else if ((lead & 0xf8) == 0xf0) {
            size = 4;
            codePoint = lead & 0x07;
            minimum = 0x10000;
        } else {
            return 0;
        }

        if (offset + size > bytes.size()) return 0;
        for (std::size_t i = 1; i < size; ++i) {
            const auto byte = static_cast<unsigned char>(bytes[offset + i]);
            if (!is_continuation_byte_(byte)) return 0;
            codePoint = (codePoint << 6) | (byte & 0x3f);
        }
        if (codePoint < minimum || codePoint > 0x10ffff ||
            (codePoint >= 0xd800 && codePoint <= 0xdfff)) {
            return 0;
        }
        return size;
    }

    static std::string replace_invalid_utf8_(std::string_view bytes) {
        constexpr std::string_view replacement = "\xef\xbf\xbd";
        std::string valid;
        valid.reserve(bytes.size());
        for (std::size_t i = 0; i < bytes.size();) {
            const std::size_t sequenceSize = valid_sequence_size_(bytes, i);
            if (sequenceSize == 0) {
                valid.append(replacement);
                ++i;
            } else {
                valid.append(bytes.substr(i, sequenceSize));
                i += sequenceSize;
            }
        }
        return valid;
    }

public:
    void reset() {
        bytes_.clear();
        truncated_ = false;
    }

    void append(std::string_view bytes) {
        if (bytes.empty()) return;
        if (bytes.size() >= kMaxHookOutputBytes) {
            truncated_ = truncated_ || !bytes_.empty() ||
                         bytes.size() > kMaxHookOutputBytes;
            bytes_.assign(bytes.substr(bytes.size() - kMaxHookOutputBytes));
            return;
        }
        if (bytes_.size() > kMaxHookOutputBytes - bytes.size()) {
            const std::size_t overflow =
                bytes_.size() + bytes.size() - kMaxHookOutputBytes;
            bytes_.erase(0, overflow);
            truncated_ = true;
        }
        bytes_.append(bytes);
    }

    std::string finish() const {
        std::string output = replace_invalid_utf8_(bytes_);
        bool truncated = truncated_;
        if (output.size() > kMaxHookOutputBytes) {
            std::size_t offset = output.size() - kMaxHookOutputBytes;
            while (offset < output.size() &&
                   is_continuation_byte_(static_cast<unsigned char>(output[offset]))) {
                ++offset;
            }
            output.erase(0, offset);
            truncated = true;
        }
        if (truncated) output.insert(0, HOOK_OUTPUT_TRUNCATED_MARKER);
        return output;
    }
};

HookOutput* hook_output(lua::State* L) {
    return static_cast<HookOutput*>(
        lua::touserdata(L, lua::upvalueindex(1)));
}

void append_lua_value(lua::State* L, HookOutput& output, int index) {
    unsigned long long size = 0;
    const char* value = lua::L_tolstring(L, index, &size);
    if (value) output.append(std::string_view(value, size));
    lua::pop(L, 1);
}

int capture_print(lua::State* L) {
    auto* output = hook_output(L);
    if (!output) return 0;
    const int count = lua::gettop(L);
    for (int i = 1; i <= count; ++i) {
        if (i > 1) output->append("\t");
        append_lua_value(L, *output, i);
    }
    output->append("\n");
    return 0;
}

int capture_io_write(lua::State* L) {
    auto* output = hook_output(L);
    if (output) {
        const int count = lua::gettop(L);
        for (int i = 1; i <= count; ++i) append_lua_value(L, *output, i);
    }
    lua::getglobal(L, "io");
    lua::getfield(L, -1, "stdout");
    lua::remove(L, -2);
    return 1;
}

int capture_stderr_write(lua::State* L) {
    if (lua::rawequal(L, 1, lua::upvalueindex(2))) {
        auto* output = hook_output(L);
        const int count = lua::gettop(L);
        if (output) {
            for (int i = 2; i <= count; ++i) append_lua_value(L, *output, i);
        }
        lua::pushvalue(L, 1);
        return 1;
    }

    const int argumentCount = lua::gettop(L);
    lua::pushvalue(L, lua::upvalueindex(3));
    lua::insert(L, 1);
    lua::call(L, argumentCount, lua::MULTRET);
    return lua::gettop(L);
}

class HookCapture {
    lua::State* L_ = nullptr;
    HookOutput& output_;
    int printRef_ = 0;
    int ioRef_ = 0;
    int ioWriteRef_ = 0;
    int stderrRef_ = 0;
    int stderrMethodsRef_ = 0;
    int stderrWriteRef_ = 0;

    void restore_() {
        if (!L_) return;

        lua::rawgeti(L_, lua::REGISTRYINDEX, ioRef_);
        lua::rawgeti(L_, lua::REGISTRYINDEX, ioWriteRef_);
        lua::setfield(L_, -2, "write");
        lua::rawgeti(L_, lua::REGISTRYINDEX, stderrRef_);
        lua::setfield(L_, -2, "stderr");
        lua::setglobal(L_, "io");

        lua::rawgeti(L_, lua::REGISTRYINDEX, stderrMethodsRef_);
        lua::rawgeti(L_, lua::REGISTRYINDEX, stderrWriteRef_);
        lua::setfield(L_, -2, "write");
        lua::pop(L_, 1);

        lua::rawgeti(L_, lua::REGISTRYINDEX, printRef_);
        lua::setglobal(L_, "print");
        lua::L_unref(L_, lua::REGISTRYINDEX, printRef_);
        lua::L_unref(L_, lua::REGISTRYINDEX, ioRef_);
        lua::L_unref(L_, lua::REGISTRYINDEX, ioWriteRef_);
        lua::L_unref(L_, lua::REGISTRYINDEX, stderrRef_);
        lua::L_unref(L_, lua::REGISTRYINDEX, stderrMethodsRef_);
        lua::L_unref(L_, lua::REGISTRYINDEX, stderrWriteRef_);
        L_ = nullptr;
    }

public:
    HookCapture(lua::State* L, HookOutput& output)
        : L_(L), output_(output) {
        output_.reset();

        lua::getglobal(L_, "print");
        printRef_ = lua::L_ref(L_, lua::REGISTRYINDEX);
        lua::getglobal(L_, "io");
        ioRef_ = lua::L_ref(L_, lua::REGISTRYINDEX);

        lua::rawgeti(L_, lua::REGISTRYINDEX, ioRef_);
        lua::getfield(L_, -1, "write");
        ioWriteRef_ = lua::L_ref(L_, lua::REGISTRYINDEX);
        lua::getfield(L_, -1, "stderr");
        stderrRef_ = lua::L_ref(L_, lua::REGISTRYINDEX);

        lua::rawgeti(L_, lua::REGISTRYINDEX, stderrRef_);
        lua::getmetatable(L_, -1);
        lua::getfield(L_, -1, "__index");
        stderrMethodsRef_ = lua::L_ref(L_, lua::REGISTRYINDEX);
        lua::pop(L_, 2);

        lua::rawgeti(L_, lua::REGISTRYINDEX, stderrMethodsRef_);
        lua::getfield(L_, -1, "write");
        stderrWriteRef_ = lua::L_ref(L_, lua::REGISTRYINDEX);
        lua::pop(L_, 1);

        lua::pushlightuserdata(L_, &output_);
        lua::pushcclosure(L_, capture_print, 1);
        lua::setglobal(L_, "print");

        lua::pushlightuserdata(L_, &output_);
        lua::pushcclosure(L_, capture_io_write, 1);
        lua::setfield(L_, -2, "write");

        lua::rawgeti(L_, lua::REGISTRYINDEX, stderrMethodsRef_);
        lua::pushlightuserdata(L_, &output_);
        lua::rawgeti(L_, lua::REGISTRYINDEX, stderrRef_);
        lua::rawgeti(L_, lua::REGISTRYINDEX, stderrWriteRef_);
        lua::pushcclosure(L_, capture_stderr_write, 3);
        lua::setfield(L_, -2, "write");
        lua::pop(L_, 2);
    }

    ~HookCapture() { restore_(); }

    HookCapture(const HookCapture&) = delete;
    HookCapture& operator=(const HookCapture&) = delete;

    std::string finish() {
        restore_();
        return output_.finish();
    }
};

} // namespace mcpplibs::xpkg::detail

// ---- PackageExecutor ----

export namespace mcpplibs::xpkg {

class PackageExecutor {
    lua::State* L_   = nullptr;
    fs::path    pkg_ ;
    std::unique_ptr<detail::HookOutput> hookOutput_ =
        std::make_unique<detail::HookOutput>();

public:
    explicit PackageExecutor(lua::State* L, fs::path pkg)
        : L_(L), pkg_(std::move(pkg)) {}

    ~PackageExecutor() {
        if (L_) { lua::close(L_); L_ = nullptr; }
    }

    PackageExecutor(const PackageExecutor&)            = delete;
    PackageExecutor& operator=(const PackageExecutor&) = delete;

    PackageExecutor(PackageExecutor&& o) noexcept
        : L_(std::exchange(o.L_, nullptr)),
          pkg_(std::move(o.pkg_)),
          hookOutput_(std::move(o.hookOutput_)) {}

    PackageExecutor& operator=(PackageExecutor&& o) noexcept {
        if (this != &o) {
            if (L_) lua::close(L_);
            L_   = std::exchange(o.L_, nullptr);
            pkg_ = std::move(o.pkg_);
            hookOutput_ = std::move(o.hookOutput_);
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

        detail::HookCapture capture(L_, *hookOutput_);
        HookResult result;
        if (lua::pcall(L_, 0, 1, 0) == lua::OK) {
            int t = lua::type(L_, -1);
            if (t == lua::TBOOLEAN) {
                result.success = lua::toboolean(L_, -1);
                if (!result.success) {
                    result.error = std::string(name) + " hook returned false";
                }
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
            if (const char* error = lua::tostring(L_, -1)) {
                result.error = error;
            }
            lua::pop(L_, 1);
        }

        if (!result.success && result.error.empty()) {
            result.error = std::string(name) + " hook failed";
        }
        result.output = capture.finish();

        return result;
    }

    // Auto-stamp wrapper packages (linux-headers etc.) whose install hook
    // returns success but leaves install_dir empty because the real
    // payload lives elsewhere (subos sysroot via config(), or a
    // sub-dependency). Without something in install_dir, xlings's catalog
    // probe (`is_directory && !is_empty`) flags the package as not
    // installed on every dependent install, re-running this hook + the
    // expensive config hook every time. Drop a tiny `.xim-installed`
    // stamp so install_dir is non-empty post-install. Authors no longer
    // need to remember to write this themselves.
    //
    // CRITICAL: Call this AFTER all install paths complete (run_hook +
    // any consumer-side stage_extracted_payload / default_install
    // fallback). If invoked from inside run_hook, the stamp is written
    // before the consumer's "is install_dir empty?" check, which would
    // suppress the extracted-payload fallback used by packages whose
    // install hook silently fails (e.g. tarballs without a top-level
    // dir, where the hook's `os.mv(extracted_dir, install_dir)` no-ops
    // because extracted_dir doesn't exist).
    //
    // Policy: only writes when (a) install_dir exists or can be created,
    // (b) install_dir is empty. If anything else has populated it, we
    // don't touch it.
    void apply_install_stamp_if_empty(const ExecutionContext& ctx) {
        if (ctx.install_dir.empty()) return;
        std::error_code ec;
        fs::create_directories(ctx.install_dir, ec);
        ec.clear();
        bool dir_exists = fs::exists(ctx.install_dir, ec)
                       && fs::is_directory(ctx.install_dir, ec);
        ec.clear();
        if (!dir_exists || !fs::is_empty(ctx.install_dir, ec)) return;
        auto stamp_path = ctx.install_dir / ".xim-installed";
        std::ofstream out(stamp_path);
        if (!out.is_open()) return;
        // Key=value INI for human-readable + grep-friendly access.
        // schema=1 reserves room to evolve the format later.
        out << "schema = 1\n"
            << "name = "       << ctx.pkg_name << "\n"
            << "version = "    << ctx.version  << "\n"
            << "platform = "   << ctx.platform << "\n";
    }

    HookResult check_installed(const ExecutionContext& ctx) {
        return run_hook(HookType::Installed, ctx);
    }

    // Run elfpatch.apply_auto() if the install hook set elfpatch_auto flag.
    // Returns {scanned, patched, failed} counts. Safe to call unconditionally.
    HookResult apply_elfpatch_auto() {
        constexpr const char* script = R"__LUA__(
            local ep = _LIBXPKG_MODULES and _LIBXPKG_MODULES["elfpatch"]
            if not ep then return "no-ep 0 0 0" end
            local r = ep.apply_auto()
            return tostring(r.scanned) .. " " .. tostring(r.patched) .. " " .. tostring(r.failed)
        )__LUA__";
        HookResult result;
        if (lua::L_loadstring(L_, script) != lua::OK) {
            result.success = false;
            result.error = lua::tostring(L_, -1);
            lua::pop(L_, 1);
            return result;
        }
        if (lua::pcall(L_, 0, 1, 0) == lua::OK) {
            result.success = true;
            if (lua::type(L_, -1) == lua::TSTRING)
                result.output = lua::tostring(L_, -1);
            lua::pop(L_, 1);
        } else {
            result.success = false;
            result.error = lua::tostring(L_, -1);
            lua::pop(L_, 1);
        }
        return result;
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
                op.var        = read_field("var");
                op.value      = read_field("value");
                op.mode       = read_field("mode");
                op.src        = read_field("src");
                op.dst        = read_field("dst");

                // Read args array (ordered; empty when absent)
                lua::getfield(L_, -1, "args");
                if (lua::type(L_, -1) == lua::TTABLE) {
                    for (int i = 1;; ++i) {
                        lua::rawgeti(L_, -1, i);
                        if (lua::type(L_, -1) != lua::TSTRING) {
                            lua::pop(L_, 1);
                            break;
                        }
                        op.args.emplace_back(lua::tostring(L_, -1));
                        lua::pop(L_, 1);
                    }
                }
                lua::pop(L_, 1);

                // Read envs table (key-value pairs)
                lua::getfield(L_, -1, "envs");
                if (lua::type(L_, -1) == lua::TTABLE) {
                    lua::pushnil(L_);
                    while (lua::next(L_, -2)) {
                        if (lua::type(L_, -2) == lua::TSTRING &&
                            lua::type(L_, -1) == lua::TSTRING) {
                            op.envs.emplace_back(
                                lua::tostring(L_, -2),
                                lua::tostring(L_, -1));
                        }
                        lua::pop(L_, 1);
                    }
                }
                lua::pop(L_, 1);

                ops.push_back(std::move(op));
            }
            lua::pop(L_, 1);
        }
        lua::pop(L_, 1);
        return ops;
    }

    // Run the script's xpkg_main() function with arguments.
    HookResult run_script(const ExecutionContext& ctx) {
        detail::inject_context(L_, ctx);

        lua::newtable(L_);
        lua::setglobal(L_, "_XVM_OPS");
        lua::newtable(L_);
        lua::setglobal(L_, "_INSTALL_REQUESTS");

        lua::getglobal(L_, "xpkg_main");
        if (lua::type(L_, -1) != lua::TFUNCTION) {
            lua::pop(L_, 1);
            return HookResult{ .success = false,
                               .error = "xpkg_main not found in script" };
        }

        int nargs = static_cast<int>(ctx.args.size());
        for (auto& arg : ctx.args) {
            lua::pushstring(L_, arg.c_str());
        }

        HookResult result;
        if (lua::pcall(L_, nargs, 0, 0) == lua::OK) {
            result.success = true;
        } else {
            result.success = false;
            result.error = lua::tostring(L_, -1);
            lua::pop(L_, 1);
        }
        return result;
    }

    // Set log level for Lua scripts: "debug", "info", "warn", "error", "silent"
    void set_log_level(std::string_view level) {
        std::string script = std::string("local log = _LIBXPKG_MODULES and _LIBXPKG_MODULES['log']; ")
            + "if log and log.set_level then log.set_level('" + std::string(level) + "') end";
        if (lua::L_loadstring(L_, script.c_str()) == lua::OK) {
            lua::pcall(L_, 0, 0, 0);
        } else {
            lua::pop(L_, 1);
        }
    }

    std::vector<InstallRequest> install_requests() {
        std::vector<InstallRequest> reqs;
        lua::getglobal(L_, "_INSTALL_REQUESTS");
        if (lua::type(L_, -1) != lua::TTABLE) {
            lua::pop(L_, 1);
            return reqs;
        }
        int len = (int)lua::rawlen(L_, -1);
        for (int i = 1; i <= len; ++i) {
            lua::rawgeti(L_, -1, i);
            if (lua::type(L_, -1) == lua::TTABLE) {
                InstallRequest req;
                lua::getfield(L_, -1, "op");
                if (lua::type(L_, -1) == lua::TSTRING) req.op = lua::tostring(L_, -1);
                lua::pop(L_, 1);
                lua::getfield(L_, -1, "target");
                if (lua::type(L_, -1) == lua::TSTRING) req.target = lua::tostring(L_, -1);
                lua::pop(L_, 1);
                reqs.push_back(std::move(req));
            }
            lua::pop(L_, 1);
        }
        lua::pop(L_, 1);
        return reqs;
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

    // Derive pkgindex root from pkg_path for top-level import("xim.pkgindex.*").
    // Package files live at <pkgindex>/pkgs/<letter>/<name>.lua — 3 levels up.
    {
        auto pkgindex = pkg_path.parent_path().parent_path().parent_path();
        std::error_code ec;
        if (fs::is_directory(pkgindex / "libs", ec)) {
            lua::pushstring(L, pkgindex.string().c_str());
            lua::setglobal(L, "_PKGINDEX_DIR");
        }
    }

    if (lua::L_dofile(L, pkg_path.string().c_str()) != lua::OK) {
        err = lua::tostring(L, -1);
        lua::close(L);
        return std::unexpected("failed to load package: " + err);
    }

    return PackageExecutor(L, pkg_path);
}

} // export namespace mcpplibs::xpkg
