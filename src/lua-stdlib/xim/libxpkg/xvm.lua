-- xim.libxpkg.xvm: version management integration (collects ops for C++ processing)
local M = {}
local _log_enabled = true

_XVM_OPS = _XVM_OPS or {}

function M.add(name, opt)
    opt = opt or {}
    local entry = {
        op = "add",
        name = name,
        version  = opt.version or (_RUNTIME and _RUNTIME.version) or "",
        bindir   = opt.bindir  or (_RUNTIME and _RUNTIME.install_dir) or "",
        alias    = opt.alias or "",
        type     = opt.type or "",
        filename = opt.filename or "",
        binding  = opt.binding or "",
        envs     = opt.envs or nil,
    }
    if _log_enabled then
        io.write("[xim:xpkg]: xvm add " .. name .. " version=" .. entry.version .. "\n")
    end
    table.insert(_XVM_OPS, entry)
end

function M.remove(name, version)
    if _log_enabled then
        io.write("[xim:xpkg]: xvm remove " .. name .. " " .. (version or "") .. "\n")
    end
    table.insert(_XVM_OPS, {op = "remove", name = name, version = version or ""})
end

function M.use(name, version)
    -- stub: version switching handled by C++ side
end

-- Load VersionDB from config files (global + project)
local _versions_cache = nil
local function _load_versions()
    if _versions_cache then return _versions_cache end
    local ok_json, json_mod = pcall(require, "xim.libxpkg.json")
    if not ok_json then
        json_mod = _LIBXPKG_MODULES and _LIBXPKG_MODULES["json"]
    end
    if not json_mod then return nil end

    local function load_file(config_path)
        local f = io.open(config_path, "r")
        if not f then return nil end
        local content = f:read("*a"); f:close()
        if not content or content == "" then return nil end
        local ok, data = pcall(json_mod.decode, content)
        if not ok or type(data) ~= "table" then return nil end
        return data.versions or nil
    end

    local merged = {}
    -- 1. Load global versions from ~/.xlings/.xlings.json
    local home = os.getenv("HOME") or os.getenv("USERPROFILE") or ""
    local global_versions = load_file(home .. "/.xlings/.xlings.json")
    if global_versions then
        for k, v in pairs(global_versions) do merged[k] = v end
    end
    -- 2. Load project versions (project_data_dir is 2 levels below project root)
    if _RUNTIME and _RUNTIME.project_data_dir and _RUNTIME.project_data_dir ~= "" then
        local project_dir = path.directory(path.directory(_RUNTIME.project_data_dir))
        local project_versions = load_file(path.join(project_dir, ".xlings.json"))
        if project_versions then
            for k, v in pairs(project_versions) do merged[k] = v end
        end
    end
    _versions_cache = merged
    return _versions_cache
end

function M.has(name, version)
    -- Check pending ops first (current session adds)
    for _, entry in ipairs(_XVM_OPS) do
        if entry.op == "add" and entry.name == name then return true end
    end
    -- Check persisted VersionDB
    local versions = _load_versions()
    if not versions then return false end
    local vinfo = versions[name]
    if not vinfo then return false end
    if not version or version == "" then return true end
    if vinfo.versions then
        for ver_key, _ in pairs(vinfo.versions) do
            if ver_key == version then return true end
        end
    end
    return false
end

function M.info(name, version)
    local versions = _load_versions()
    if not versions then return nil end
    local vinfo = versions[name]
    if not vinfo or not vinfo.versions then return nil end

    -- Find matching version
    local vdata = nil
    local matched_version = version or ""
    if version and version ~= "" then
        vdata = vinfo.versions[version]
    end
    if not vdata then
        -- Try first available version
        for ver_key, ver_val in pairs(vinfo.versions) do
            vdata = ver_val
            matched_version = ver_key
            break
        end
    end
    if not vdata then return nil end

    local info_table = {
        Name = name,
        Version = matched_version,
        Type = vinfo.type or "program",
        Program = vinfo.filename or name,
        SPath = vdata.path or "",
        TPath = vdata.path or "",
        Alias = vdata.alias or nil,
        Envs = vdata.envs or nil,
    }
    return info_table
end

function M.log_tag(enable)
    local old = _log_enabled
    _log_enabled = enable
    return old
end

--- Unified registration of programs, libraries, and headers
-- @param name      package name (root node in binding tree)
-- @param opt       table with:
--   install_dir  install root directory (default _RUNTIME.install_dir)
--   version      version string (default _RUNTIME.version)
--   bindir       programs directory (relative or absolute, default "bin")
--   libdir       library directory (relative or absolute, optional)
--   includedir   header directory (relative or absolute, optional)
--   programs     list of program names (optional)
--   libs         list of library filenames (optional)
function M.setup(name, opt)
    opt = opt or {}
    local install_dir = opt.install_dir or (_RUNTIME and _RUNTIME.install_dir) or ""
    local version = opt.version or (_RUNTIME and _RUNTIME.version) or ""
    local binding = name .. "@" .. version

    local function resolve(dir)
        if not dir then return nil end
        if path.is_absolute(dir) then return dir end
        return path.join(install_dir, dir)
    end

    -- 1. Register root node
    M.add(name)

    -- 2. Batch register programs
    if opt.programs then
        local bindir = resolve(opt.bindir or "bin")
        for _, prog in ipairs(opt.programs) do
            M.add(prog, { bindir = bindir, binding = binding })
        end
    end

    -- 3. Batch register libraries
    if opt.libs then
        local libdir = resolve(opt.libdir or "lib")
        for _, lib in ipairs(opt.libs) do
            M.add(lib, {
                type = "lib", bindir = libdir,
                alias = lib, filename = lib,
                binding = binding,
            })
        end
    end

    -- 4. Header directory -> C++ side creates symlinks to sysroot
    if opt.includedir then
        local includedir = resolve(opt.includedir)
        table.insert(_XVM_OPS, { op = "headers", includedir = includedir })
    end
end

--- Unified unregistration
function M.teardown(name, opt)
    opt = opt or {}
    M.remove(name)
    if opt.programs then
        for _, prog in ipairs(opt.programs) do M.remove(prog) end
    end
    if opt.libs then
        for _, lib in ipairs(opt.libs) do M.remove(lib) end
    end
    if opt.includedir then
        local install_dir = opt.install_dir or (_RUNTIME and _RUNTIME.install_dir) or ""
        local includedir = opt.includedir
        if not path.is_absolute(includedir) then
            includedir = path.join(install_dir, includedir)
        end
        table.insert(_XVM_OPS, { op = "remove_headers", includedir = includedir })
    end
end

return M
