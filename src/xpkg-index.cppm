module;

export module mcpplibs.xpkg.index;
import mcpplibs.xpkg;
import std;

export namespace mcpplibs::xpkg {

// Fuzzy search: returns names of entries whose name or description contains
// query (case-insensitive). Results are sorted.
std::vector<std::string>
search(const PackageIndex& index, const std::string& query) {
    std::vector<std::string> results;
    std::string q = query;
    std::transform(q.begin(), q.end(), q.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    for (auto& [name, entry] : index.entries) {
        std::string n = name;
        std::transform(n.begin(), n.end(), n.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        std::string d = entry.description;
        std::transform(d.begin(), d.end(), d.begin(),
                       [](unsigned char c){ return std::tolower(c); });

        if (n.find(q) != std::string::npos || d.find(q) != std::string::npos)
            results.push_back(name);
    }
    std::sort(results.begin(), results.end());
    return results;
}

// Resolve alias: if entry has a non-empty ref, return that ref; else return
// the name unchanged.  "vscode" → "vscode@1.85.0"
std::string
resolve(const PackageIndex& index, const std::string& name) {
    auto it = index.entries.find(name);
    if (it == index.entries.end())
        return name;
    return it->second.ref.empty() ? name : it->second.ref;
}

// Find the best-matching entry for a base package name.
// Priority: exact match first, then installed versioned entry, then latest
// versioned entry (lexicographically greatest version string).
std::optional<std::string>
match_version(const PackageIndex& index, const std::string& name) {
    // Exact key match (versioned or unversioned)
    if (index.entries.count(name))
        return name;

    // Collect all versioned entries whose name@ prefix matches
    std::string prefix = name + "@";
    std::vector<std::string> candidates;
    for (auto& [key, entry] : index.entries) {
        if (key.starts_with(prefix))
            candidates.push_back(key);
    }
    if (candidates.empty())
        return std::nullopt;

    // Prefer installed
    for (auto& c : candidates) {
        if (index.entries.at(c).installed)
            return c;
    }
    // Fall back to lexicographically greatest (approximates "latest")
    std::sort(candidates.begin(), candidates.end());
    return candidates.back();
}

// Return all packages in the same mutex group as pkg_name (excluding itself).
std::vector<std::string>
mutex_packages(const PackageIndex& index, const std::string& pkg_name) {
    std::vector<std::string> result;
    for (auto& [gkey, members] : index.mutex_groups) {
        (void)gkey;
        bool in_group = std::ranges::contains(members, pkg_name);
        if (in_group) {
            for (auto& m : members) {
                if (m != pkg_name)
                    result.push_back(m);
            }
        }
    }
    return result;
}

// Merge overlay into base.  Each overlay entry key gets namespace_ prefix if
// namespace_ is non-empty ("cmake" → "extra-x-cmake").  mutex_groups are
// merged by appending members.
PackageIndex
merge(PackageIndex base, const PackageIndex& overlay,
      const std::string& namespace_ = "") {
    for (auto& [name, entry] : overlay.entries) {
        std::string key = namespace_.empty() ? name : namespace_ + "-x-" + name;
        auto e = entry;
        e.name = key;
        base.entries[key] = std::move(e);
    }
    for (auto& [gkey, gmembers] : overlay.mutex_groups) {
        auto& dest = base.mutex_groups[gkey];
        dest.insert(dest.end(), gmembers.begin(), gmembers.end());
    }
    return base;
}

// Update the installed flag for a named entry (no-op if not found).
void
set_installed(PackageIndex& index, const std::string& name, bool installed) {
    auto it = index.entries.find(name);
    if (it != index.entries.end())
        it->second.installed = installed;
}

} // export namespace mcpplibs::xpkg
