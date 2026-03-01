#include <gtest/gtest.h>
import mcpplibs.xpkg;

using namespace mcpplibs::xpkg;

TEST(ModelTest, DefaultPackage) {
    Package p;
    EXPECT_EQ(p.type, PackageType::Package);
    EXPECT_EQ(p.status, PackageStatus::Dev);
    EXPECT_FALSE(p.xvm_enable);
    EXPECT_TRUE(p.name.empty());
}

TEST(ModelTest, PackageFields) {
    Package p;
    p.name = "mdbook";
    p.description = "Create book from markdown files";
    p.type = PackageType::Package;
    p.status = PackageStatus::Stable;
    p.xvm_enable = true;
    EXPECT_EQ(p.name, "mdbook");
    EXPECT_EQ(p.status, PackageStatus::Stable);
    EXPECT_TRUE(p.xvm_enable);
}

TEST(ModelTest, PlatformMatrix) {
    PlatformMatrix xpm;
    xpm.entries["linux"]["0.4.40"] = PlatformResource{
        .url = "https://example.com/mdbook.tar.gz",
        .sha256 = "abc123",
        .ref = ""
    };
    xpm.entries["linux"]["latest"] = PlatformResource{
        .ref = "0.4.40"
    };
    EXPECT_EQ(xpm.entries["linux"].size(), 2u);
    EXPECT_EQ(xpm.entries["linux"]["0.4.40"].url, "https://example.com/mdbook.tar.gz");
    EXPECT_EQ(xpm.entries["linux"]["latest"].ref, "0.4.40");
}

TEST(ModelTest, IndexEntry) {
    IndexEntry e;
    e.name = "vscode@1.85.0";
    e.version = "1.85.0";
    EXPECT_FALSE(e.installed);
    EXPECT_EQ(e.type, PackageType::Package);
}

TEST(ModelTest, PackageIndex) {
    PackageIndex idx;
    idx.entries["mdbook"] = IndexEntry{ .name = "mdbook", .version = "0.4.40" };
    idx.mutex_groups["editors"] = {"vscode", "vim", "emacs"};
    EXPECT_EQ(idx.entries.size(), 1u);
    EXPECT_EQ(idx.mutex_groups["editors"].size(), 3u);
}
