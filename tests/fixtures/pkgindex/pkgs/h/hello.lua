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
                sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
            },
        },
        windows = {
            ["latest"] = { ref = "1.0.0" },
            ["1.0.0"]  = {
                url    = "https://example.com/hello-1.0.0-windows.zip",
                sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
            },
        },
        macosx = {
            ["latest"] = { ref = "1.0.0" },
            ["1.0.0"]  = {
                url    = "https://example.com/hello-1.0.0-macosx.tar.gz",
                sha256 = "0000000000000000000000000000000000000000000000000000000000000000"
            },
        },
    },
}

import("xim.libxpkg.pkginfo")
import("xim.libxpkg.xvm")

local MARKER = "hello.installed"

-- installed(): return version string if marker file exists, else nil
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
    os.execute("mkdir -p " .. dir)
    local f = io.open(dir .. "/" .. MARKER, "w")
    if not f then return false end
    f:write("1.0.0\n"); f:close()
    return true
end

-- config(): register with xvm
function config()
    xvm.add("hello")
    return true
end

-- uninstall(): remove marker and deregister from xvm
function uninstall()
    local dir = pkginfo.install_dir()
    if dir then os.remove(dir .. "/" .. MARKER) end
    xvm.remove("hello")
    return true
end
