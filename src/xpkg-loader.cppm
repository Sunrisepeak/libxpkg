module;

export module mcpplibs.xpkg.loader;
import mcpplibs.xpkg;
import mcpplibs.capi.lua;
import std;

namespace lua = mcpplibs::capi::lua;
namespace fs  = std::filesystem;

namespace mcpplibs::xpkg::loader_detail {

// Register a no-op import() function so package .lua files can be loaded
// without xmake's module system
void register_noop_import(lua::State* L) {
    lua::L_dostring(L,
        "import = function(...) "
        "  return setmetatable({}, { "
        "    __index = function() return function() end end "
        "  }) "
        "end");
}

std::string get_str(lua::State* L, int idx, const char* key) {
    lua::getfield(L, idx, key);
    std::string r;
    if (lua::type(L, -1) == lua::TSTRING)
        r = lua::tostring(L, -1);
    lua::pop(L, 1);
    return r;
}

bool get_bool(lua::State* L, int idx, const char* key) {
    lua::getfield(L, idx, key);
    bool r = lua::toboolean(L, -1);
    lua::pop(L, 1);
    return r;
}

std::vector<std::string> get_str_array(lua::State* L, int idx, const char* key) {
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

// Parse xpm platform matrix from the package table at idx
PlatformMatrix parse_xpm(lua::State* L, int pkg_idx) {
    PlatformMatrix xpm;
    lua::getfield(L, pkg_idx, "xpm");
    if (lua::type(L, -1) != lua::TTABLE) {
        lua::pop(L, 1);
        return xpm;
    }

    // Iterate platforms
    int xpm_idx = lua::gettop(L);
    lua::pushnil(L);
    while (lua::next(L, xpm_idx)) {
        // key = platform name (at -2), value = version table (at -1)
        std::string platform;
        if (lua::type(L, -2) == lua::TSTRING)
            platform = lua::tostring(L, -2);

        if (!platform.empty() && lua::type(L, -1) == lua::TTABLE) {
            int plat_idx = lua::gettop(L);
            lua::pushnil(L);
            while (lua::next(L, plat_idx)) {
                // key = version string (at -2), value = resource (at -1)
                std::string version;
                if (lua::type(L, -2) == lua::TSTRING)
                    version = lua::tostring(L, -2);

                if (!version.empty()) {
                    PlatformResource res;
                    if (lua::type(L, -1) == lua::TTABLE) {
                        int res_idx = lua::gettop(L);
                        res.url    = get_str(L, res_idx, "url");
                        res.sha256 = get_str(L, res_idx, "sha256");
                        res.ref    = get_str(L, res_idx, "ref");
                    } else if (lua::type(L, -1) == lua::TSTRING) {
                        // e.g. "XLINGS_RES" — treat as url placeholder
                        res.url = lua::tostring(L, -1);
                    }
                    xpm.entries[platform][version] = std::move(res);
                }
                lua::pop(L, 1);  // pop value, keep key for next()
            }
        }
        lua::pop(L, 1);  // pop platform value, keep platform key for next()
    }

    lua::pop(L, 1);  // pop xpm table
    return xpm;
}

} // namespace mcpplibs::xpkg::loader_detail

export namespace mcpplibs::xpkg {

std::expected<Package, std::string>
load_package(const fs::path& pkg_path) {
    if (!fs::exists(pkg_path))
        return std::unexpected("file not found: " + pkg_path.string());

    lua::State* L = lua::L_newstate();
    if (!L) return std::unexpected("failed to create lua state");
    lua::L_openlibs(L);

    // Register no-op import() so package scripts don't fail on import calls
    loader_detail::register_noop_import(L);

    if (lua::L_dofile(L, pkg_path.string().c_str()) != lua::OK) {
        std::string err = lua::tostring(L, -1);
        lua::close(L);
        return std::unexpected("lua error: " + err);
    }

    lua::getglobal(L, "package");
    if (lua::type(L, -1) != lua::TTABLE) {
        lua::close(L);
        return std::unexpected("'package' global not found in " + pkg_path.string());
    }

    int pkg_idx = lua::gettop(L);
    Package p;
    p.spec        = loader_detail::get_str(L, pkg_idx, "spec");
    p.name        = loader_detail::get_str(L, pkg_idx, "name");
    p.description = loader_detail::get_str(L, pkg_idx, "description");
    p.type        = loader_detail::parse_type(
                        loader_detail::get_str(L, pkg_idx, "type"));
    p.status      = loader_detail::parse_status(
                        loader_detail::get_str(L, pkg_idx, "status"));
    p.namespace_  = loader_detail::get_str(L, pkg_idx, "namespace");
    p.homepage    = loader_detail::get_str(L, pkg_idx, "homepage");
    p.repo        = loader_detail::get_str(L, pkg_idx, "repo");
    p.docs        = loader_detail::get_str(L, pkg_idx, "docs");
    p.xvm_enable  = loader_detail::get_bool(L, pkg_idx, "xvm_enable");
    p.authors     = loader_detail::get_str_array(L, pkg_idx, "authors");
    p.maintainers = loader_detail::get_str_array(L, pkg_idx, "maintainers");
    p.licenses    = loader_detail::get_str_array(L, pkg_idx, "licenses");
    p.categories  = loader_detail::get_str_array(L, pkg_idx, "categories");
    p.keywords    = loader_detail::get_str_array(L, pkg_idx, "keywords");
    p.archs       = loader_detail::get_str_array(L, pkg_idx, "archs");
    p.programs    = loader_detail::get_str_array(L, pkg_idx, "programs");
    p.xpm         = loader_detail::parse_xpm(L, pkg_idx);

    lua::close(L);
    return p;
}

std::expected<PackageIndex, std::string>
build_index(const fs::path& repo_dir, const std::string& namespace_ = "") {
    PackageIndex index;
    auto pkgs_dir = repo_dir / "pkgs";
    if (!fs::is_directory(pkgs_dir))
        return std::unexpected("pkgs/ directory not found in: " + repo_dir.string());

    for (auto& letter_dir : fs::directory_iterator(pkgs_dir)) {
        if (!letter_dir.is_directory()) continue;
        for (auto& entry : fs::directory_iterator(letter_dir)) {
            if (entry.path().extension() != ".lua") continue;
            auto result = load_package(entry.path());
            if (!result) continue;  // skip malformed packages
            auto& pkg = *result;
            std::string key = (namespace_.empty() ? "" : namespace_ + "-x-")
                            + pkg.name;
            IndexEntry ie;
            ie.name        = key;
            ie.path        = entry.path();
            ie.type        = pkg.type;
            ie.description = pkg.description;
            index.entries[key] = std::move(ie);
        }
    }
    return index;
}

std::expected<IndexRepos, std::string>
load_index_repos(const fs::path&) {
    return std::unexpected("load_index_repos: not yet implemented");
}

} // export namespace mcpplibs::xpkg
