add_rules("mode.debug", "mode.release")

set_languages("c++23")

target("basic")
    set_kind("binary")
    add_files("basic.cpp")
    add_deps("mcpplibs-xpkg")
    set_policy("build.c++.modules", true)

-- Full lifecycle demo: load → index → search → executor → install → uninstall
target("lifecycle")
    set_kind("binary")
    add_files("lifecycle.cpp")
    add_deps("xpkg")
    set_policy("build.c++.modules", true)
    on_config(function(target)
        local dir = path.join(os.projectdir(), "tests", "fixtures")
        dir = dir:gsub("\\", "/")
        target:add("defines", 'XPKG_FIXTURES_DIR="' .. dir .. '"')
    end)
