-- xim.libxpkg.pkginfo: package info API reading from _RUNTIME global
local M = {}

function M.name()         return _RUNTIME and _RUNTIME.pkg_name or nil end
function M.version()      return _RUNTIME and _RUNTIME.version or nil end
function M.install_file() return _RUNTIME and _RUNTIME.install_file or nil end
function M.deps_list()    return (_RUNTIME and _RUNTIME.deps_list) or {} end

local function _ends_with(s, suffix)
    return suffix == "" or s:sub(-#suffix) == suffix
end

local function _parse_namespace(name)
    local ns, bare = name:match("^([^:]+):(.+)$")
    if ns then return ns, bare end
    return nil, name
end

local function _match_store_name(dirname, ns, bare)
    if ns then
        -- namespace specified: exact match "ns-x-bare"
        return dirname == ns .. "-x-" .. bare
    else
        -- no namespace: match "bare" or "*-x-bare"
        return dirname == bare or _ends_with(dirname, "-x-" .. bare)
    end
end

local function _scan_dir(base, ns, bare, dep_version)
    if not base or not os.isdir(base) then return nil end
    local dirs = os.dirs(path.join(base, "*")) or {}
    for _, dep_root in ipairs(dirs) do
        local dirname = path.filename(dep_root)
        if _match_store_name(dirname, ns, bare) then
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

local function _resolve_dep_via_scan(dep_name, dep_version)
    local ns, bare = _parse_namespace(dep_name)
    -- 1. Search xpkg_dir (lua package files directory)
    local result = _scan_dir(_RUNTIME and _RUNTIME.xpkg_dir, ns, bare, dep_version)
    if result then return result end
    -- 2. Search xpkgs install root (install_dir's grandparent)
    if _RUNTIME and _RUNTIME.install_dir then
        local xpkgs_root = path.directory(path.directory(_RUNTIME.install_dir))
        result = _scan_dir(xpkgs_root, ns, bare, dep_version)
        if result then return result end
    end
    -- 3. Search project xpkgs (handles global-pkg depending on project-local pkg)
    if _RUNTIME and _RUNTIME.project_data_dir and _RUNTIME.project_data_dir ~= "" then
        result = _scan_dir(path.join(_RUNTIME.project_data_dir, "xpkgs"), ns, bare, dep_version)
        if result then return result end
    end
    return nil
end

-- Try xvm registry: for "ns:name", try "ns-name" first, then bare "name"
local function _resolve_dep_via_xvm(dep_name, dep_version)
    local ok_xvm, xvm_mod = pcall(require, "xim.libxpkg.xvm")
    if not ok_xvm or not xvm_mod then return nil end
    local ns, bare = _parse_namespace(dep_name)
    local candidates = ns and {ns .. "-" .. bare, bare} or {bare}
    for _, xvm_name in ipairs(candidates) do
        local info = xvm_mod.info(xvm_name, dep_version)
        if info and info["SPath"] and info["SPath"] ~= "" then
            local spath = info["SPath"]
            local pver = (info["Version"] or dep_version or ""):gsub("([%(%)%.%%%+%-%*%?%[%]%^%$])", "%%%1")
            if pver ~= "" then
                local head = spath:match("^(.*)" .. pver)
                if head then
                    return path.join(head:gsub("[/\\]+$", ""), info["Version"] or dep_version)
                end
            end
        end
    end
    return nil
end

function M.dep_install_dir(dep_name, dep_version)
    local result = _resolve_dep_via_scan(dep_name, dep_version)
    if result then return result end
    return _resolve_dep_via_xvm(dep_name, dep_version)
end

function M.install_dir(pkgname, pkgversion)
    if not pkgname then
        return _RUNTIME and _RUNTIME.install_dir or nil
    end
    local dir = M.dep_install_dir(pkgname, pkgversion)
    if dir then return dir end
    io.write(string.format("[xim:xpkg]: cannot get install dir for %s@%s\n",
        tostring(pkgname), tostring(pkgversion or "latest")))
    return nil
end

return M
