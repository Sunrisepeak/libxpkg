module;
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <expected>

export module mcpplibs.xpkg;

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
    // Declared outside struct body → outlined symbol in module object.
    // Prevents test/consumer TUs from inlining the body and then generating
    // unsatisfied calls to std internal inline helpers (e.g. ~_Vector_impl).
    ~PlatformMatrix();
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
    ~Package();
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
    ~PackageIndex();
};

struct RepoConfig {
    std::string name;          // namespace; empty for main repo
    std::string url_global, url_cn;
    std::filesystem::path local_path;
};

struct IndexRepos {
    RepoConfig main_repo;
    std::vector<RepoConfig> sub_repos;
    ~IndexRepos();
};

} // namespace mcpplibs::xpkg

// Out-of-line destructor definitions (not inline, not in-class-body).
// GCC emits these as outlined symbols in the module's compiled object.
// Importing TUs call the outlined versions, so std internals like
// ~_Vector_impl() and ~_Hashtable_alloc() are only needed at the
// xpkg.cppm compilation site — where all headers are in scope.
namespace mcpplibs::xpkg {
PlatformMatrix::~PlatformMatrix() = default;
Package::~Package()       = default;
PackageIndex::~PackageIndex() = default;
IndexRepos::~IndexRepos() = default;
}
