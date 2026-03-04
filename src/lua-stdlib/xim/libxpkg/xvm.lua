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

function M.has(name, version)
    for _, entry in ipairs(_XVM_OPS) do
        if entry.op == "add" and entry.name == name then return true end
    end
    return false
end

function M.info(name, version)
    return nil  -- stub
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
