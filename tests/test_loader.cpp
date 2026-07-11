#include <gtest/gtest.h>
#include <filesystem>
#include <string_view>

import mcpplibs.xpkg;
import mcpplibs.xpkg.loader;
import mcpplibs.xpkg.compat;

using namespace mcpplibs::xpkg;
namespace fs = std::filesystem;

#ifndef XPKG_TEST_PKGINDEX
#  define XPKG_TEST_PKGINDEX tests/fixtures/pkgindex
#endif

#ifndef XPKG_TEST_PKGINDEX_BUILD
#  define XPKG_TEST_PKGINDEX_BUILD tests/fixtures/pkgindex-build
#endif

#define XPKG_STRINGIFY_IMPL(x) #x
#define XPKG_STRINGIFY(x) XPKG_STRINGIFY_IMPL(x)

constexpr std::string_view normalize_pkgindex_macro(std::string_view value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

static const fs::path PKGINDEX{
    std::string(normalize_pkgindex_macro(XPKG_STRINGIFY(XPKG_TEST_PKGINDEX)))
};

static const fs::path PKGINDEX_BUILD{
    std::string(normalize_pkgindex_macro(XPKG_STRINGIFY(XPKG_TEST_PKGINDEX_BUILD)))
};

static fs::path copy_pkgindex_build_fixture(std::string_view test_name) {
    auto destination = fs::temp_directory_path()
        / ("libxpkg-loader-" + std::string(test_name));
    fs::remove_all(destination);
    fs::copy(PKGINDEX_BUILD, destination, fs::copy_options::recursive);
    return destination;
}

TEST(LoaderTest, LoadPackage_MissingFile) {
    auto result = load_package("/nonexistent/pkg.lua");
    EXPECT_FALSE(result.has_value());
}

TEST(LoaderTest, LoadPackage_Hello) {
    auto result = load_package(PKGINDEX / "pkgs/h/hello.lua");
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_EQ(result->name, "hello");
    EXPECT_EQ(result->type, PackageType::Package);
    EXPECT_EQ(result->status, PackageStatus::Stable);
    EXPECT_FALSE(result->xpm.entries.empty());
    EXPECT_TRUE(result->xvm_enable);
}

TEST(LoaderTest, LoadPackage_HasLinuxPlatform) {
    auto result = load_package(PKGINDEX / "pkgs/h/hello.lua");
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->xpm.entries.count("linux"), 0u);
}

TEST(LoaderTest, BuildIndex_ReturnsEntries) {
    auto result = build_index(PKGINDEX);
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_GT(result->entries.size(), 0u);
    EXPECT_GT(result->entries.count("hello"), 0u);
}

TEST(LoaderTest, BuildIndex_PkgindexBuild_OsFiles) {
    // Tests that build_index works with pkgindex-build.lua that uses os.files()
    // This validates the C++ std::filesystem implementation works cross-platform
    auto fixture = copy_pkgindex_build_fixture("os-files");
    auto result = build_index(fixture);
    fs::remove_all(fixture);
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_GT(result->entries.count("testbuild"), 0u);
}

TEST(LoaderTest, BuildIndex_PkgindexBuild_TemplateAppended) {
    // After pkgindex-build runs, the testbuild package should have xpm data
    // from the appended template
    auto fixture = copy_pkgindex_build_fixture("template-appended");
    auto result = build_index(fixture);
    ASSERT_TRUE(result.has_value()) << result.error();
    auto pkg = load_package(result->entries.at("testbuild").path);
    fs::remove_all(fixture);
    ASSERT_TRUE(pkg.has_value()) << pkg.error();
    // Template adds xpm with linux/windows/macosx platforms
    EXPECT_FALSE(pkg->xpm.entries.empty()) << "template xpm should have been appended by pkgindex-build";
}

// Legacy array form: `deps = { "node", "npm" }` must populate
// runtime_deps AND build_deps identically (loader fan-out) so
// pre-split consumers keep getting the same dep set.
TEST(LoaderTest, LoadPackage_DepsLegacy_FansOutToBoth) {
    auto result = load_package(PKGINDEX / "pkgs/d/depslegacy.lua");
    ASSERT_TRUE(result.has_value()) << result.error();

    auto& xpm = result->xpm;
    auto rt = xpm.runtime_deps.find("linux");
    auto bd = xpm.build_deps.find("linux");
    auto un = xpm.deps.find("linux");
    ASSERT_NE(rt, xpm.runtime_deps.end());
    ASSERT_NE(bd, xpm.build_deps.end());
    ASSERT_NE(un, xpm.deps.end());

    std::vector<std::string> expected{"node", "npm"};
    EXPECT_EQ(rt->second, expected);
    EXPECT_EQ(bd->second, expected);
    EXPECT_EQ(un->second, expected);
}

// Split form: deps = { runtime = {...}, build = {...} } must keep
// the two lists separate, and the legacy `deps` field must hold
// their union (preserving insertion order: runtime first, then build).
TEST(LoaderTest, LoadPackage_DepsSplit_KeepsSeparation) {
    auto result = load_package(PKGINDEX / "pkgs/d/depssplit.lua");
    ASSERT_TRUE(result.has_value()) << result.error();

    auto& xpm = result->xpm;
    auto rt = xpm.runtime_deps.find("linux");
    auto bd = xpm.build_deps.find("linux");
    auto un = xpm.deps.find("linux");
    ASSERT_NE(rt, xpm.runtime_deps.end());
    ASSERT_NE(bd, xpm.build_deps.end());
    ASSERT_NE(un, xpm.deps.end());

    std::vector<std::string> expectedRt{"node", "npm"};
    std::vector<std::string> expectedBd{"gcc", "patchelf"};
    EXPECT_EQ(rt->second, expectedRt);
    EXPECT_EQ(bd->second, expectedBd);

    std::vector<std::string> expectedUnion{"node", "npm", "gcc", "patchelf"};
    EXPECT_EQ(un->second, expectedUnion);
}

// exports.runtime with all sub-fields populated must round-trip through the
// parser. This is the "happy path" for declarative provider metadata.
TEST(LoaderTest, LoadPackage_ExportsFull) {
    auto result = load_package(PKGINDEX / "pkgs/e/exportsfull.lua");
    ASSERT_TRUE(result.has_value()) << result.error();

    auto& xpm = result->xpm;
    auto eit = xpm.exports.find("linux");
    ASSERT_NE(eit, xpm.exports.end());
    auto& rt = eit->second.runtime;

    EXPECT_EQ(rt.loader, "lib64/ld-linux-x86-64.so.2");
    EXPECT_EQ(rt.abi, "linux-x86_64-glibc");
    std::vector<std::string> expectedLibdirs{"lib64", "lib", "usr/lib"};
    EXPECT_EQ(rt.libdirs, expectedLibdirs);
}

// Partial declaration: only `loader` set, libdirs/abi omitted — must parse
// without error and leave the omitted fields empty (consumers fall back to
// the {lib64, lib} convention for libdirs).
TEST(LoaderTest, LoadPackage_ExportsLoaderOnly) {
    auto result = load_package(PKGINDEX / "pkgs/e/exportsloaderonly.lua");
    ASSERT_TRUE(result.has_value()) << result.error();

    auto& xpm = result->xpm;
    auto eit = xpm.exports.find("linux");
    ASSERT_NE(eit, xpm.exports.end());
    auto& rt = eit->second.runtime;

    EXPECT_EQ(rt.loader, "lib/ld-musl-x86_64.so.1");
    EXPECT_TRUE(rt.libdirs.empty());
    EXPECT_TRUE(rt.abi.empty());
}

// Packages without an `exports` block must remain valid; the platform's
// exports map entry simply doesn't exist (consumers see "no provider
// declared" and the predicate trigger falls through to no-op).
TEST(LoaderTest, LoadPackage_NoExports) {
    auto result = load_package(PKGINDEX / "pkgs/h/hello.lua");
    ASSERT_TRUE(result.has_value()) << result.error();

    auto& xpm = result->xpm;
    EXPECT_EQ(xpm.exports.find("linux"), xpm.exports.end());
}

// ---------------------------------------------------------------------------
// Arch normalization
// ---------------------------------------------------------------------------

TEST(ArchTest, NormalizesAliases) {
    EXPECT_EQ(normalize_arch("amd64"),   "x86_64");
    EXPECT_EQ(normalize_arch("x64"),     "x86_64");
    EXPECT_EQ(normalize_arch("x86-64"),  "x86_64");
    EXPECT_EQ(normalize_arch("x86_64"),  "x86_64");   // canonical passthrough
    EXPECT_EQ(normalize_arch("arm64"),   "aarch64");
    EXPECT_EQ(normalize_arch("armv8"),   "aarch64");
    EXPECT_EQ(normalize_arch("aarch64"), "aarch64");
    EXPECT_EQ(normalize_arch("AArch64"), "aarch64");  // case-insensitive
}

TEST(ArchTest, ArchMatchesAcrossSpellings) {
    EXPECT_TRUE(arch_matches("arm64", "aarch64"));
    EXPECT_TRUE(arch_matches("amd64", "x86_64"));
    EXPECT_FALSE(arch_matches("x86_64", "aarch64"));
}

// ---------------------------------------------------------------------------
// V2 multi-arch xpm shapes
// ---------------------------------------------------------------------------

// Scheme B: per-arch resource map. Each arch carries its own url + sha256;
// the single-arch url/sha256 stay empty. Arch keys normalize to canonical.
TEST(LoaderTest, V2_PerArchMap_ParsesBothArches) {
    auto result = load_package(PKGINDEX / "pkgs/v/v2map.lua");
    ASSERT_TRUE(result.has_value()) << result.error();
    auto& r = result->xpm.entries.at("linux").at("1.0.0");
    EXPECT_TRUE(r.url.empty());
    EXPECT_TRUE(r.sha256.empty());
    ASSERT_EQ(r.archs.size(), 2u);
    EXPECT_EQ(r.archs.at("x86_64").url,    "https://ex/v2map-1.0.0-linux-x86_64.tar.gz");
    EXPECT_EQ(r.archs.at("x86_64").sha256, "aaaa");
    EXPECT_EQ(r.archs.at("aarch64").url,   "https://ex/v2map-1.0.0-linux-aarch64.tar.gz");
    EXPECT_EQ(r.archs.at("aarch64").sha256, "bbbb");
}

// Scheme C: a URL template plus a per-arch sha256 table and an arch_alias
// map. The template string is kept verbatim (expanded only at install time).
TEST(LoaderTest, V2_Template_ParsesShaMapAndAlias) {
    auto result = load_package(PKGINDEX / "pkgs/v/v2tmpl.lua");
    ASSERT_TRUE(result.has_value()) << result.error();
    auto& r = result->xpm.entries.at("linux").at("1.0.0");
    EXPECT_EQ(r.url, "https://ex/${name}-${version}-${os}-${arch_alias}.${ext}");
    EXPECT_TRUE(r.sha256.empty());  // sha256 was a table, not a string
    ASSERT_EQ(r.sha256_by_arch.size(), 2u);
    EXPECT_EQ(r.sha256_by_arch.at("x86_64"),  "aaaa");
    EXPECT_EQ(r.sha256_by_arch.at("aarch64"), "bbbb");
    EXPECT_EQ(r.arch_alias.at("x86_64"),  "amd64");
    EXPECT_EQ(r.arch_alias.at("aarch64"), "arm64");
    EXPECT_FALSE(r.is_res);
}

// res shape: XLINGS_RES auto-URL plus per-arch checksums (closes the
// XLINGS_RES "no sha256" gap). is_res flags install-time URL synthesis.
TEST(LoaderTest, V2_Res_ParsesFlagAndShaMap) {
    auto result = load_package(PKGINDEX / "pkgs/v/v2res.lua");
    ASSERT_TRUE(result.has_value()) << result.error();
    auto& r = result->xpm.entries.at("linux").at("1.0.0");
    EXPECT_TRUE(r.is_res);
    ASSERT_EQ(r.sha256_by_arch.size(), 2u);
    EXPECT_EQ(r.sha256_by_arch.at("x86_64"),  "aaaa");
    EXPECT_EQ(r.sha256_by_arch.at("aarch64"), "bbbb");
    EXPECT_TRUE(r.archs.empty());  // res shape is not a per-arch map
}

// Legacy single-arch entries must be entirely unaffected by V2 parsing.
TEST(LoaderTest, V2_LegacySingleArch_Unchanged) {
    auto result = load_package(PKGINDEX / "pkgs/h/hello.lua");
    ASSERT_TRUE(result.has_value()) << result.error();
    auto& r = result->xpm.entries.at("linux").at("1.0.0");
    EXPECT_EQ(r.url, "https://example.com/hello-1.0.0-linux.tar.gz");
    EXPECT_FALSE(r.sha256.empty());
    EXPECT_TRUE(r.archs.empty());
    EXPECT_TRUE(r.sha256_by_arch.empty());
    EXPECT_FALSE(r.is_res);
}

TEST(LoaderTest, SourceDefaultsAreParsedAsMetadataNotVersions) {
    auto result = load_package(PKGINDEX / "pkgs/v/v2source_template.lua");
    ASSERT_TRUE(result.has_value()) << result.error();
    EXPECT_EQ(
        result->xpm.source,
        "https://example.test/${name}/${version}/${name}-${os}-${arch}.${ext}");
    EXPECT_EQ(
        result->xpm.platform_sources.at("linux"),
        "https://linux.example.test/${version}/tool-${arch_alias}.tar.xz");
    EXPECT_FALSE(result->xpm.entries.at("linux").contains("source"));
    EXPECT_FALSE(result->xpm.entries.contains("source"));
}

TEST(LoaderTest, PlatformContextEvaluatesLegacyHostConditionals) {
    auto linux = load_package(
        PKGINDEX / "pkgs/v/v2platform_context.lua",
        LoaderContext{.platform = "linux", .arch = "x86_64"});
    ASSERT_TRUE(linux.has_value()) << linux.error();
    EXPECT_EQ(
        linux->xpm.entries.at("linux").at("1.0.0").url,
        "https://example.test/linux-x86_64.tar.gz");

    auto macos = load_package(
        PKGINDEX / "pkgs/v/v2platform_context.lua",
        LoaderContext{.platform = "macosx", .arch = "aarch64"});
    ASSERT_TRUE(macos.has_value()) << macos.error();
    EXPECT_EQ(
        macos->xpm.entries.at("macosx").at("1.0.0").url,
        "https://example.test/macosx-aarch64.tar.gz");
}

TEST(CompatTest, XlingsResSourceFollowsRefAndSelectsArchHash) {
    auto package = load_package(PKGINDEX / "pkgs/v/v2source_res.lua");
    ASSERT_TRUE(package.has_value()) << package.error();
    auto resolved = resolve_resource(package->xpm, {
        .name = package->name,
        .version = "latest",
        .platform = "linux",
        .arch = "amd64",
    });
    ASSERT_TRUE(resolved.has_value()) << resolved.error();
    EXPECT_EQ(resolved->version, "1.0.0");
    EXPECT_EQ(resolved->kind, SourceKind::XlingsRes);
    EXPECT_EQ(resolved->sha256, "linux-amd64-hash");
    EXPECT_TRUE(resolved->url.empty());
}

TEST(CompatTest, PlatformTemplateOverridesRootAndExpandsAlias) {
    auto package = load_package(PKGINDEX / "pkgs/v/v2source_template.lua");
    ASSERT_TRUE(package.has_value()) << package.error();
    auto resolved = resolve_resource(package->xpm, {
        .name = package->name,
        .version = "1.0.0",
        .platform = "linux",
        .arch = "x86_64",
    });
    ASSERT_TRUE(resolved.has_value()) << resolved.error();
    EXPECT_EQ(resolved->kind, SourceKind::UrlTemplate);
    EXPECT_EQ(
        resolved->url,
        "https://linux.example.test/1.0.0/tool-amd64.tar.xz");
    EXPECT_EQ(resolved->sha256, "linux-amd64-hash");
}

TEST(CompatTest, ExplicitVersionUrlOverridesSource) {
    auto package = load_package(PKGINDEX / "pkgs/v/v2source_template.lua");
    ASSERT_TRUE(package.has_value()) << package.error();
    auto resolved = resolve_resource(package->xpm, {
        .name = package->name,
        .version = "custom",
        .platform = "linux",
        .arch = "x86_64",
    });
    ASSERT_TRUE(resolved.has_value()) << resolved.error();
    EXPECT_EQ(resolved->kind, SourceKind::ExplicitUrl);
    EXPECT_EQ(resolved->url, "https://override.test/custom.tar.gz");
    EXPECT_EQ(resolved->sha256, "custom-hash");
}

TEST(CompatTest, LegacyXlingsResStringRemainsSupported) {
    PlatformMatrix matrix;
    matrix.entries["linux"]["1.0.0"].url = "XLINGS_RES";
    auto resolved = resolve_resource(matrix, {
        .name = "legacy",
        .version = "1.0.0",
        .platform = "linux",
        .arch = "x86_64",
    });
    ASSERT_TRUE(resolved.has_value()) << resolved.error();
    EXPECT_EQ(resolved->kind, SourceKind::XlingsRes);
    EXPECT_TRUE(resolved->url.empty());
}

TEST(CompatTest, LegacyResFlagAndPerArchMapRemainSupported) {
    auto res_package = load_package(PKGINDEX / "pkgs/v/v2res.lua");
    ASSERT_TRUE(res_package.has_value()) << res_package.error();
    auto res = resolve_resource(res_package->xpm, {
        .name = res_package->name,
        .version = "latest",
        .platform = "linux",
        .arch = "arm64",
    });
    ASSERT_TRUE(res.has_value()) << res.error();
    EXPECT_EQ(res->kind, SourceKind::XlingsRes);
    EXPECT_EQ(res->sha256, "bbbb");

    auto map_package = load_package(PKGINDEX / "pkgs/v/v2map.lua");
    ASSERT_TRUE(map_package.has_value()) << map_package.error();
    auto map = resolve_resource(map_package->xpm, {
        .name = map_package->name,
        .version = "latest",
        .platform = "linux",
        .arch = "amd64",
    });
    ASSERT_TRUE(map.has_value()) << map.error();
    EXPECT_EQ(map->kind, SourceKind::ExplicitUrl);
    EXPECT_EQ(map->url, "https://ex/v2map-1.0.0-linux-x86_64.tar.gz");
    EXPECT_EQ(map->sha256, "aaaa");
}

TEST(CompatTest, RootTemplateFallbackUsesPlatformDefaultExtension) {
    auto package = load_package(PKGINDEX / "pkgs/v/v2source_template.lua");
    ASSERT_TRUE(package.has_value()) << package.error();
    auto resolved = resolve_resource(package->xpm, {
        .name = package->name,
        .version = "1.0.0",
        .platform = "windows",
        .arch = "arm64",
    });
    ASSERT_TRUE(resolved.has_value()) << resolved.error();
    EXPECT_EQ(
        resolved->url,
        "https://example.test/v2source-template/1.0.0/"
        "v2source-template-windows-aarch64.zip");
}

TEST(CompatTest, TemplateMirrorsAreExpandedAndPreserved) {
    PlatformMatrix matrix;
    auto& resource = matrix.entries["linux"]["2.0.0"];
    resource.url = "https://origin.test/${name}-${arch_alias}.${ext}";
    resource.arch_alias["x86_64"] = "amd64";
    resource.mirrors["CN"] = "https://mirror.test/${version}/${arch_alias}";
    auto resolved = resolve_resource(matrix, {
        .name = "tool",
        .version = "2.0.0",
        .platform = "linux",
        .arch = "amd64",
        .ext = "tar.xz",
    });
    ASSERT_TRUE(resolved.has_value()) << resolved.error();
    EXPECT_EQ(resolved->url, "https://origin.test/tool-amd64.tar.xz");
    EXPECT_EQ(resolved->mirrors.at("CN"), "https://mirror.test/2.0.0/amd64");
}

TEST(CompatTest, RefCyclesAreRejected) {
    PlatformMatrix matrix;
    matrix.entries["linux"]["a"].ref = "b";
    matrix.entries["linux"]["b"].ref = "a";
    auto resolved = resolve_resource(matrix, {
        .name = "cycle",
        .version = "a",
        .platform = "linux",
        .arch = "x86_64",
    });
    ASSERT_FALSE(resolved.has_value());
    EXPECT_NE(resolved.error().find("cycle"), std::string::npos);
}

TEST(CompatTest, MissingPerArchResourceFailsClosed) {
    PlatformMatrix matrix;
    matrix.source = "https://fallback.test/${arch}.tar.gz";
    matrix.entries["linux"]["1.0.0"].archs["x86_64"] = {
        .url = "https://example.test/x86_64.tar.gz",
        .sha256 = "x86-hash",
    };
    auto resolved = resolve_resource(matrix, {
        .name = "tool",
        .version = "1.0.0",
        .platform = "linux",
        .arch = "aarch64",
    });
    ASSERT_FALSE(resolved.has_value());
    EXPECT_NE(resolved.error().find("no resource for arch"), std::string::npos);
}

TEST(CompatTest, MissingPerArchChecksumFailsClosed) {
    PlatformMatrix matrix;
    matrix.source = "xlings-res";
    matrix.entries["linux"]["1.0.0"].sha256_by_arch["x86_64"] = "x86-hash";
    auto resolved = resolve_resource(matrix, {
        .name = "tool",
        .version = "1.0.0",
        .platform = "linux",
        .arch = "aarch64",
    });
    ASSERT_FALSE(resolved.has_value());
    EXPECT_NE(resolved.error().find("no checksum for arch"), std::string::npos);
}
