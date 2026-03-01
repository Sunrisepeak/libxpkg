-- xim.libxpkg.xvm: version management integration (calls xvm CLI)
local M = {}
local _log_enabled = true

local function _xvm_cmd(...)
    local args = {...}
    local cmd = "xvm " .. table.concat(args, " ")
    if _log_enabled then
        io.write("[xim:xpkg]: " .. cmd .. "\n")
    end
    return os.execute(cmd)
end

function M.add(name, opt)
    opt = opt or {}
    local ver    = opt.version or (_RUNTIME and _RUNTIME.version) or ""
    local bindir = opt.bindir  or (_RUNTIME and _RUNTIME.install_dir) or ""
    local args = {"add", name, "--version=" .. ver, "--bindir=" .. bindir}
    if opt.alias then table.insert(args, "--alias=" .. opt.alias) end
    if opt.type  then table.insert(args, "--type="  .. opt.type)  end
    _xvm_cmd(table.unpack(args))
end

function M.remove(name, version)
    _xvm_cmd("remove", name, version or "")
end

function M.use(name, version)
    _xvm_cmd("use", name, version or "")
end

function M.has(name, version)
    local ret = os.execute("xvm has " .. name .. " " .. (version or ""))
    return ret == 0 or ret == true
end

function M.info(name, version)
    return nil  -- stub: full implementation requires xvm query output parsing
end

function M.log_tag(enable)
    local old = _log_enabled
    _log_enabled = enable
    return old
end

return M
