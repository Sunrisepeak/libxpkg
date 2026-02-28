add_rules("mode.debug", "mode.release")

set_languages("c++23")

target("basic")
    set_kind("binary")
    add_files("basic.cpp")
    add_deps("mcpplibs-templates")
    set_policy("build.c++.modules", true)
