package = {
    spec    = "1",
    name    = "hello",
    description = "Minimal fixture package for libxpkg tests",
    authors  = {"libxpkg-test"},
    licenses = {"MIT"},
    repo     = "https://github.com/mcpplibs/libxpkg",

    type     = "package",
    archs    = {"x86_64"},
    status   = "stable",
    categories = {"test"},
    keywords   = {"test", "fixture"},

    xvm_enable = true,

    xpm = {
        linux = {
            ["latest"] = { ref = "1.0.0" },
            ["1.0.0"]  = {
                url    = "https://example.com/hello-1.0.0-linux.tar.gz",
                sha256 = "0000000000000000000000000000000000000000000000000000000000000000" -- intentionally fake: fixture package is never downloaded
            },
        },
        windows = {
            ["latest"] = { ref = "1.0.0" },
            ["1.0.0"]  = {
                url    = "https://example.com/hello-1.0.0-windows.zip",
                sha256 = "0000000000000000000000000000000000000000000000000000000000000000" -- intentionally fake: fixture package is never downloaded
            },
        },
        macosx = {
            ["latest"] = { ref = "1.0.0" },
            ["1.0.0"]  = {
                url    = "https://example.com/hello-1.0.0-macosx.tar.gz",
                sha256 = "0000000000000000000000000000000000000000000000000000000000000000" -- intentionally fake: fixture package is never downloaded
            },
        },
    },
}

local pkginfo = import("xim.libxpkg.pkginfo")
local xvm     = import("xim.libxpkg.xvm")

local MARKER = "hello.installed"

-- installed(): return version string read from marker file, or "1.0.0" as fallback when marker exists but is unreadable, or nil if absent
function installed()
    local dir = pkginfo.install_dir()
    if not dir then return nil end
    local marker = dir .. "/" .. MARKER
    if os.isfile(marker) then
        local f = io.open(marker, "r")
        if f then
            local ver = f:read("*l"); f:close()
            return ver or "1.0.0"
        end
        return "1.0.0"
    end
    return nil
end

-- install(): create install_dir and write marker file
function install()
    local dir = pkginfo.install_dir()
    if not dir then return false end
    local ok = os.execute('mkdir -p "' .. dir .. '"')
    if not ok then return false end
    local f = io.open(dir .. "/" .. MARKER, "w")
    if not f then return false end
    f:write("1.0.0\n"); f:close()
    return true
end

-- config(): register with xvm
function config()
    local dir = pkginfo.install_dir()
    if not dir then return false end
    xvm.add("hello", { bindir = dir })
    return true
end

-- uninstall(): remove marker and deregister from xvm
function uninstall()
    local dir = pkginfo.install_dir()
    if dir then os.remove(dir .. "/" .. MARKER) end
    if xvm.has("hello") then xvm.remove("hello") end
    return true
end
