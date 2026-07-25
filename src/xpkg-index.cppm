module;

export module mcpplibs.xpkg.index;
import mcpplibs.xpkg;
import std;

export namespace mcpplibs::xpkg {

namespace index_detail_ {

std::string namespace_of(std::string_view entryKey, const IndexEntry& entry) {
    if (!entry.identity.namespaceName.empty()) {
        return entry.identity.namespaceName;
    }
    auto colon = entryKey.find(':');
    if (colon == std::string_view::npos) return {};
    return std::string(entryKey.substr(0, colon));
}

std::string short_name_of(std::string_view entryKey, const IndexEntry& entry) {
    if (!entry.identity.name.empty()) return entry.identity.name;
    if (!entry.name.empty()) {
        auto at = entry.name.find('@');
        return at == std::string::npos ? entry.name : entry.name.substr(0, at);
    }

    auto colon = entryKey.find(':');
    auto base = colon == std::string_view::npos
        ? entryKey
        : entryKey.substr(colon + 1);
    auto at = base.find('@');
    return std::string(at == std::string_view::npos ? base : base.substr(0, at));
}

std::string canonical_name_of(std::string_view entryKey,
                              const IndexEntry& entry) {
    if (!entry.canonicalName.empty()) return entry.canonicalName;
    auto at = entryKey.find('@');
    return std::string(at == std::string_view::npos
        ? entryKey
        : entryKey.substr(0, at));
}

std::vector<std::string>
identity_entries(const PackageIndex& index, std::string_view canonicalName) {
    if (auto it = index.identityEntries.find(std::string(canonicalName));
        it != index.identityEntries.end()) {
        return it->second;
    }

    std::vector<std::string> candidates;
    auto prefix = std::string(canonicalName) + "@";
    for (auto& [entryKey, entry] : index.entries) {
        if (canonical_name_of(entryKey, entry) == canonicalName
            || entryKey.starts_with(prefix)) {
            candidates.push_back(entryKey);
        }
    }
    std::ranges::sort(candidates);
    return candidates;
}

}  // namespace index_detail_

std::vector<std::string>
find_candidates(const PackageIndex& index,
                std::string_view name,
                std::optional<std::string_view> namespaceName = std::nullopt) {
    if (namespaceName) {
        auto canonicalName = namespaceName->empty()
            ? std::string(name)
            : std::string(*namespaceName) + ":" + std::string(name);
        auto entries = index_detail_::identity_entries(index, canonicalName);
        return entries.empty()
            ? std::vector<std::string> {}
            : std::vector<std::string> { canonicalName };
    }

    if (auto it = index.shortNames.find(std::string(name));
        it != index.shortNames.end()) {
        return it->second;
    }

    std::vector<std::string> candidates;
    for (auto& [entryKey, entry] : index.entries) {
        if (index_detail_::short_name_of(entryKey, entry) == name) {
            candidates.push_back(
                index_detail_::canonical_name_of(entryKey, entry));
        }
    }
    std::ranges::sort(candidates);
    auto uniqueEnd = std::ranges::unique(candidates).begin();
    candidates.erase(uniqueEnd, candidates.end());
    return candidates;
}

// Fuzzy search: returns names of entries whose name or description contains
// query (case-insensitive). Results are sorted.
std::vector<std::string>
search(const PackageIndex& index, const std::string& query) {
    std::vector<std::string> results;
    std::string q = query;
    std::transform(q.begin(), q.end(), q.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    for (auto& [entryKey, entry] : index.entries) {
        std::string n = entryKey;
        std::transform(n.begin(), n.end(), n.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        std::string shortName = index_detail_::short_name_of(entryKey, entry);
        std::transform(shortName.begin(), shortName.end(), shortName.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        std::string d = entry.description;
        std::transform(d.begin(), d.end(), d.begin(),
                       [](unsigned char c){ return std::tolower(c); });

        if (n.find(q) != std::string::npos
            || shortName.find(q) != std::string::npos
            || d.find(q) != std::string::npos) {
            results.push_back(entryKey);
        }
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
    if (it->second.ref.empty()) return name;

    auto ref = it->second.ref;
    auto refAt = ref.find('@');
    auto refHead = refAt == std::string::npos ? ref : ref.substr(0, refAt);
    if (refHead.find(':') != std::string::npos) return ref;

    auto namespaceName = index_detail_::namespace_of(name, it->second);
    if (namespaceName.empty()) return ref;
    return namespaceName + ":" + ref;
}

// Find the best-matching entry for a base package name.
// Priority: exact match first, then installed versioned entry, then latest
// versioned entry (lexicographically greatest version string).
std::optional<std::string>
match_version(const PackageIndex& index, const std::string& name) {
    // Exact key match (versioned or unversioned)
    if (index.entries.count(name))
        return name;

    auto candidates = index_detail_::identity_entries(index, name);
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
    for (auto& [entryKey, entry] : overlay.entries) {
        auto e = entry;
        auto shortName = index_detail_::short_name_of(entryKey, e);
        if (e.identity.name.empty()) e.identity.name = shortName;
        if (e.identity.namespaceName.empty()) {
            e.identity.namespaceName = namespace_;
        }
        if (e.version.empty()) {
            auto versionSeparator = entryKey.find('@');
            if (versionSeparator != std::string::npos) {
                e.version = entryKey.substr(versionSeparator + 1);
            }
        }
        auto canonicalName = e.identity.canonical_name();
        auto suffix = e.version.empty() ? std::string {} : "@" + e.version;
        auto key = canonicalName + suffix;
        e.canonicalName = canonicalName;
        e.entryKey = key;
        e.name = shortName;
        if (base.entries.contains(key)) {
            throw std::invalid_argument(
                "duplicate package identity in index merge: '" + key + "'");
        }
        base.entries.emplace(key, std::move(e));
        base.identityEntries[canonicalName].push_back(key);
        base.shortNames[shortName].push_back(canonicalName);
    }
    for (auto& [gkey, gmembers] : overlay.mutex_groups) {
        auto& dest = base.mutex_groups[gkey];
        dest.insert(dest.end(), gmembers.begin(), gmembers.end());
    }
    for (auto& [_, candidates] : base.identityEntries) {
        std::ranges::sort(candidates);
        auto uniqueEnd = std::ranges::unique(candidates).begin();
        candidates.erase(uniqueEnd, candidates.end());
    }
    for (auto& [_, candidates] : base.shortNames) {
        std::ranges::sort(candidates);
        auto uniqueEnd = std::ranges::unique(candidates).begin();
        candidates.erase(uniqueEnd, candidates.end());
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
