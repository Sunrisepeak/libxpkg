-- xim.libxpkg.pkginfo: package info API reading from _RUNTIME global
local M = {}

function M.name()         return _RUNTIME and _RUNTIME.pkg_name or nil end
function M.version()      return _RUNTIME and _RUNTIME.version or nil end
function M.install_file() return _RUNTIME and _RUNTIME.install_file or nil end
function M.deps_list()    return (_RUNTIME and _RUNTIME.deps_list) or {} end

local function _ends_with(s, suffix)
    return suffix == "" or s:sub(-#suffix) == suffix
end

local function _resolve_dep_via_scan(dep_name, dep_version)
    local base = _RUNTIME.xpkg_dir
    if not base then return nil end
    local dirs = os.dirs(path.join(base, "*")) or {}
    for _, dep_root in ipairs(dirs) do
        local dirname = path.filename(dep_root)
        if dirname == dep_name or _ends_with(dirname, "-x-" .. dep_name) then
            local ver = dep_version
            if not ver then
                local vers = os.dirs(path.join(dep_root, "*")) or {}
                table.sort(vers)
                if #vers > 0 then ver = path.filename(vers[#vers]) end
            end
            if ver then
                local install_dir = path.join(dep_root, ver)
                if os.isdir(install_dir) then return install_dir end
            end
        end
    end
    return nil
end

function M.dep_install_dir(dep_name, dep_version)
    return _resolve_dep_via_scan(dep_name, dep_version)
end

function M.install_dir(pkgname, pkgversion)
    if not pkgname then
        return _RUNTIME.install_dir
    end
    local dir = M.dep_install_dir(pkgname, pkgversion)
    if dir then return dir end
    io.write(string.format("[xim:xpkg]: cannot get install dir for %s@%s\n",
        tostring(pkgname), tostring(pkgversion or "latest")))
    return nil
end

return M
