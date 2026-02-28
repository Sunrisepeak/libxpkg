module;
export module mcpplibs.xpkg;
import std;

export namespace mcpplibs::xpkg {

enum class PackageType   { Package, Script, Template, Config };
enum class PackageStatus { Dev, Stable, Deprecated };

struct PlatformResource {
    std::string url;
    std::string sha256;
    std::string ref;  // version alias, e.g. "latest" -> "1.0.0"
};

struct PlatformMatrix {
    // platform -> version -> resource
    std::unordered_map<std::string,
        std::unordered_map<std::string, PlatformResource>> entries;
    // platform -> list of dep names
    std::unordered_map<std::string, std::vector<std::string>> deps;
    // platform inheritance, e.g. "ubuntu" -> "linux"
    std::unordered_map<std::string, std::string> inherits;
};

struct Package {
    std::string spec;
    std::string name;
    std::string description;
    PackageType  type   = PackageType::Package;
    PackageStatus status = PackageStatus::Dev;
    std::string namespace_;
    std::string homepage, repo, docs;
    std::vector<std::string> authors, maintainers, licenses;
    std::vector<std::string> categories, keywords, programs, archs;
    bool xvm_enable = false;
    PlatformMatrix xpm;
};

struct IndexEntry {
    std::string name;         // e.g. "vscode@1.85.0"
    std::string version;
    std::filesystem::path path;
    PackageType type  = PackageType::Package;
    std::string description;
    bool installed    = false;
    std::string ref;          // alias target, e.g. "vscode@1.85.0"
};

struct PackageIndex {
    std::unordered_map<std::string, IndexEntry> entries;
    std::unordered_map<std::string, std::vector<std::string>> mutex_groups;
};

struct RepoConfig {
    std::string name;          // namespace; empty for main repo
    std::string url_global, url_cn;
    std::filesystem::path local_path;
};

struct IndexRepos {
    RepoConfig main_repo;
    std::vector<RepoConfig> sub_repos;
};

} // namespace mcpplibs::xpkg
