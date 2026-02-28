add_requires("gtest")

target("xpkg_test")
    set_kind("binary")
    add_files("*.cpp")
    add_deps("mcpplibs-xpkg", "mcpplibs-xpkg-loader",
             "mcpplibs-xpkg-index", "mcpplibs-xpkg-executor")
    add_packages("gtest")
    set_policy("build.c++.modules", true)
