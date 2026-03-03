-- xim.libxpkg.elfpatch: ELF/Mach-O binary patching for RPATH and interpreter
local M = {}

local function _trim(s)
    if not s then return s end
    return s:match("^%s*(.-)%s*$")
end

local function _iorun(cmd)
    local f = io.popen(cmd .. " 2>/dev/null")
    if not f then return nil end
    local output = f:read("*a")
    f:close()
    return output
end

local function _tool_exists(name)
    local ret = os.execute("command -v " .. name .. " >/dev/null 2>&1")
    return ret == 0 or ret == true
end

local function _is_elf(filepath)
    -- Check ELF magic bytes directly (faster and more reliable than readelf)
    local f = io.open(filepath, "rb")
    if not f then return false end
    local magic = f:read(4)
    f:close()
    return magic == "\x7fELF"
end

local function _normalize_rpath(rpath)
    if not rpath then return nil end
    if type(rpath) == "string" then return rpath end
    if type(rpath) ~= "table" then return nil end
    local seen, values = {}, {}
    for _, p in ipairs(rpath) do
        if p and p ~= "" and not seen[p] then
            seen[p] = true
            table.insert(values, p)
        end
    end
    if #values == 0 then return nil end
    return table.concat(values, ":")
end

local function _detect_system_loader()
    local candidates = {
        "/lib64/ld-linux-x86-64.so.2",
        "/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2",
        "/lib/ld-musl-x86_64.so.1",
    }
    for _, p in ipairs(candidates) do
        if os.isfile(p) then return p end
    end
    if _tool_exists("readelf") and os.isfile("/bin/sh") then
        local output = _iorun('readelf -l /bin/sh')
        if output then
            local loader = _trim(output:match("Requesting program interpreter:%s*([^%]]+)"))
            if loader and os.isfile(loader) then return loader end
        end
    end
    return nil
end

local function _resolve_loader(loader_opt)
    if not loader_opt then return nil end
    if loader_opt == "system" then return _detect_system_loader() end
    if loader_opt == "subos" then
        local sysroot = _RUNTIME and _RUNTIME.subos_sysrootdir
        if sysroot and sysroot ~= "" then
            for _, sub in ipairs({"lib", "lib64"}) do
                local p = path.join(sysroot, sub, "ld-linux-x86-64.so.2")
                if os.isfile(p) then return p end
            end
            local musl = path.join(sysroot, "lib", "ld-musl-x86_64.so.1")
            if os.isfile(musl) then return musl end
        end
        return nil
    end
    return loader_opt
end

local function _collect_binaries(target, opts)
    if not target then return {} end
    if os.isfile(target) then return {target} end
    if not os.isdir(target) then return {} end

    opts = opts or {}
    local recurse = opts.recurse
    if recurse == nil then recurse = true end
    local include_shared_libs = opts.include_shared_libs
    if include_shared_libs == nil then include_shared_libs = true end

    local find_cmd
    if recurse then
        find_cmd = 'find "' .. target .. '" -type f'
    else
        find_cmd = 'find "' .. target .. '" -maxdepth 1 -type f'
    end

    local binaries = {}
    local f = io.popen(find_cmd .. " 2>/dev/null")
    if f then
        for line in f:lines() do
            local filepath = _trim(line)
            if filepath and filepath ~= "" then
                if not include_shared_libs then
                    local is_shared = filepath:find("%.so", 1, true) ~= nil
                                  or filepath:find("%.dylib", 1, true) ~= nil
                    if is_shared then goto continue end
                end
                if _is_elf(filepath) then
                    table.insert(binaries, filepath)
                end
                ::continue::
            end
        end
        f:close()
    end
    return binaries
end

-- Collect library paths from dependencies
function M.closure_lib_paths(opt)
    opt = opt or {}
    local values, seen = {}, {}

    -- Self lib dirs
    local install_dir = _RUNTIME and _RUNTIME.install_dir
    if install_dir then
        for _, sub in ipairs({"lib64", "lib"}) do
            local self_libdir = path.join(install_dir, sub)
            if os.isdir(self_libdir) and not seen[self_libdir] then
                seen[self_libdir] = true
                table.insert(values, self_libdir)
                break
            end
        end
    end

    -- Dependency lib dirs (via pkginfo scan)
    local deps_list = opt.deps_list or (_RUNTIME and _RUNTIME.deps_list) or {}
    for _, dep_spec in ipairs(deps_list) do
        local dep_name = dep_spec:gsub("@.*", ""):gsub("^.+:", "")
        local dep_version = dep_spec:find("@", 1, true) and dep_spec:match("@(.+)") or nil
        local dep_dir = nil
        if _LIBXPKG_MODULES and _LIBXPKG_MODULES.pkginfo then
            dep_dir = _LIBXPKG_MODULES.pkginfo.dep_install_dir(dep_name, dep_version)
        end
        if dep_dir then
            for _, sub in ipairs({"lib64", "lib"}) do
                local libdir = path.join(dep_dir, sub)
                if os.isdir(libdir) and not seen[libdir] then
                    seen[libdir] = true
                    table.insert(values, libdir)
                    break
                end
            end
        end
    end

    -- Subos lib
    local sysroot = _RUNTIME and _RUNTIME.subos_sysrootdir
    if sysroot and sysroot ~= "" then
        local subos_lib = path.join(sysroot, "lib")
        if not seen[subos_lib] then
            seen[subos_lib] = true
            table.insert(values, subos_lib)
        end
    end

    return values
end

-- Main ELF patching function
function M.patch_elf_loader_rpath(target, opts)
    opts = opts or {}
    local result = { scanned = 0, patched = 0, failed = 0, shrinked = 0, shrink_failed = 0 }

    if os.host() ~= "linux" then
        io.write("[xim:xpkg]: elfpatch: skipping on non-Linux platform\n")
        return result
    end

    if not _tool_exists("patchelf") then
        io.write("[xim:xpkg]: WARNING: patchelf not found, skip patching\n")
        return result
    end

    local loader = _resolve_loader(opts.loader)
    local rpath = _normalize_rpath(opts.rpath)

    if opts.loader and not loader then
        io.write("[xim:xpkg]: WARNING: cannot resolve loader: " .. tostring(opts.loader) .. "\n")
    end

    local targets = _collect_binaries(target, opts)
    for _, filepath in ipairs(targets) do
        result.scanned = result.scanned + 1
        local ok = true
        if loader then
            local ret = os.execute('patchelf --set-interpreter "' .. loader .. '" "' .. filepath .. '" 2>/dev/null')
            if ret ~= 0 and ret ~= true then ok = false end
        end
        if ok and rpath and rpath ~= "" then
            local ret = os.execute('patchelf --set-rpath "' .. rpath .. '" "' .. filepath .. '" 2>/dev/null')
            if ret ~= 0 and ret ~= true then ok = false end
        end
        if ok then
            result.patched = result.patched + 1
            if opts.shrink == true then
                local ret = os.execute('patchelf --shrink-rpath "' .. filepath .. '" 2>/dev/null')
                if ret == 0 or ret == true then
                    result.shrinked = result.shrinked + 1
                else
                    result.shrink_failed = result.shrink_failed + 1
                end
            end
        else
            result.failed = result.failed + 1
        end
    end

    return result
end

-- Auto-patch flags stored in _RUNTIME
function M.auto(enable_or_opts)
    _RUNTIME = _RUNTIME or {}
    if type(enable_or_opts) == "table" then
        if enable_or_opts.enable ~= nil then
            _RUNTIME.elfpatch_auto = (enable_or_opts.enable == true)
        end
        if enable_or_opts.shrink ~= nil then
            _RUNTIME.elfpatch_shrink = (enable_or_opts.shrink == true)
        end
    else
        _RUNTIME.elfpatch_auto = (enable_or_opts == true)
    end
    return _RUNTIME.elfpatch_auto
end

function M.is_auto()
    return _RUNTIME and _RUNTIME.elfpatch_auto == true
end

function M.is_shrink()
    return _RUNTIME and _RUNTIME.elfpatch_shrink == true
end

-- Apply auto-patching if enabled
function M.apply_auto(opts)
    opts = opts or {}
    if not M.is_auto() then
        return { scanned = 0, patched = 0, failed = 0 }
    end
    local target = opts.target or (_RUNTIME and _RUNTIME.install_dir)
    local rpath = opts.rpath or M.closure_lib_paths({
        deps_list = _RUNTIME and _RUNTIME.deps_list
    })
    local shrink = opts.shrink
    if shrink == nil then shrink = M.is_shrink() end
    return M.patch_elf_loader_rpath(target, {
        loader = opts.loader or "subos",
        rpath = rpath,
        shrink = shrink,
        include_shared_libs = opts.include_shared_libs,
        recurse = opts.recurse,
    })
end

return M
