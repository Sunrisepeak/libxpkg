add_rules("mode.release", "mode.debug")
set_languages("c++23")

add_requires("lua")

local lua_dep_path = os.getenv("MCPPLIBS_LUA_PATH") or "../lua"
if os.isdir(lua_dep_path) then
    includes(lua_dep_path)
end

target("mcpplibs-xpkg")
    set_kind("static")
    add_files("src/xpkg.cppm", {public = true, install = true})
    set_policy("build.c++.modules", true)

target("mcpplibs-xpkg-loader")
    set_kind("static")
    add_deps("mcpplibs-xpkg", "mcpplibs-capi-lua")
    add_packages("lua", {public = true})
    add_files("src/xpkg-loader.cppm", {public = true, install = true})
    set_policy("build.c++.modules", true)

target("mcpplibs-xpkg-index")
    set_kind("static")
    add_deps("mcpplibs-xpkg")
    add_files("src/xpkg-index.cppm", {public = true, install = true})
    set_policy("build.c++.modules", true)

target("mcpplibs-xpkg-executor")
    set_kind("static")
    add_deps("mcpplibs-xpkg", "mcpplibs-capi-lua")
    add_packages("lua", {public = true})
    add_files("src/xpkg-executor.cppm", {public = true, install = true})
    set_policy("build.c++.modules", true)

if not is_host("macosx") then
    includes("examples", "tests")
end
