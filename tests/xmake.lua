add_rules("mode.debug", "mode.release")

set_languages("c++23")

add_requires("gtest")

target("templates_test")
    set_kind("binary")
    add_files("*.cpp")
    add_deps("mcpplibs-templates")
    add_packages("gtest")
    set_policy("build.c++.modules", true)
