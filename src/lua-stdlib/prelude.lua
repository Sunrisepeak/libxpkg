-- prelude.lua: xmake compatibility layer + import() for libxpkg runtime
-- Loaded by PackageExecutor before any package script.

-- _LIBXPKG_MODULES is populated by C++ before this file runs
_LIBXPKG_MODULES = _LIBXPKG_MODULES or {}

-- Save Lua's built-in package.config before xpkg scripts overwrite `package` global
local _PATH_SEP = package.config:sub(1,1)

-- import(): maps "xim.libxpkg.X" to preloaded modules
-- Also registers module as global variable (xmake compat: bare import() sets global)
function import(mod_path)
    local name = mod_path:match("xim%.libxpkg%.(.+)")
    if name and _LIBXPKG_MODULES[name] then
        _G[name] = _LIBXPKG_MODULES[name]
        return _LIBXPKG_MODULES[name]
    end
    -- Stub for unknown imports (base.runtime etc.)
    io.write("[libxpkg] WARNING: unknown module '" .. mod_path .. "', returning stub\n")
    io.flush()
    local stub = setmetatable({}, {
        __index = function(_, k) return function(...) end end
    })
    if name then _G[name] = stub end
    return stub
end

-- os.* extensions (xmake compat)
os.isfile = function(p)
    -- io.open succeeds on directories on Linux (fopen quirk), so also check it's not a dir
    local f = io.open(p, "r")
    if not f then return false end
    f:close()
    -- Reject directories: try reading 0 bytes; directories fail with "Is a directory"
    local f2 = io.open(p, "rb")
    if not f2 then return false end
    local ok, _ = f2:read(0)
    f2:close()
    -- read(0) returns "" on regular files, nil on directories
    return ok ~= nil
end
os.isdir = function(p)
    local sep = _PATH_SEP
    if sep == "\\" then
        local ret = os.execute('if exist "' .. p .. '\\" exit 0')
        return ret == 0 or ret == true
    else
        local ret = os.execute('[ -d "' .. p .. '" ]')
        return ret == 0 or ret == true
    end
end
os.host = function()
    return _RUNTIME and _RUNTIME.platform or "linux"
end
os.trymv = function(src, dst)
    local ok = pcall(os.rename, src, dst)
    if ok then return true end
    -- Cross-device or directory: fallback to shell mv
    local ret = os.execute('mv "' .. src .. '" "' .. dst .. '" 2>/dev/null')
    if ret == 0 or ret == true then return true end
    -- Last resort: file copy + remove (files only)
    local inf = io.open(src, "rb")
    if not inf then return false end
    local content = inf:read("*a"); inf:close()
    local outf = io.open(dst, "wb")
    if not outf then return false end
    outf:write(content); outf:close()
    local rm_ok = pcall(os.remove, src)
    return rm_ok
end
os.mv = function(src, dst) return os.trymv(src, dst) end
os.cp = function(src, dst)
    -- Try shell cp first (handles directories, symlinks, etc.)
    local sep = _PATH_SEP
    if sep ~= "\\" then
        local ret = os.execute('cp -a "' .. src .. '" "' .. dst .. '" 2>/dev/null')
        if ret == 0 or ret == true then return true end
    end
    -- Fallback: file copy
    local inf = io.open(src, "rb")
    if not inf then return false end
    local content = inf:read("*a"); inf:close()
    local outf = io.open(dst, "wb")
    if not outf then return false end
    outf:write(content); outf:close()
    return true
end
os.dirs = function(pattern)
    local result = {}
    -- Quote pattern to handle spaces; use platform-appropriate command
    local sep = _PATH_SEP
    local cmd
    if sep == "\\" then
        cmd = 'dir /B /AD "' .. pattern .. '" 2>nul'
    else
        cmd = 'ls -d "' .. pattern .. '" 2>/dev/null'
    end
    local f = io.popen(cmd)
    if f then
        for line in f:lines() do
            local clean = line:gsub("[\r\n]+$", "")  -- strip CRLF
            if clean ~= "" and os.isdir(clean) then
                table.insert(result, clean)
            end
        end
        f:close()
    end
    return result
end
os.sleep = function(ms) end  -- stub
os.cd = function(dir)
    if not dir then return false end
    -- Lua has no built-in chdir; use lfs if available, else shell fallback
    local ok, lfs = pcall(require, "lfs")
    if ok and lfs and lfs.chdir then
        return lfs.chdir(dir)
    end
    -- Fallback: not truly possible from pure Lua, but we set _CD for scripts
    _CURRENT_DIR = dir
    return true
end
os.iorun = function(cmd)
    local f = io.popen(cmd .. " 2>/dev/null")
    if not f then return "" end
    local output = f:read("*a")
    f:close()
    return output or ""
end
os.exec = function(cmd)
    return os.execute(cmd)
end
os.tryrm = function(p)
    if not p then return false end
    local sep = _PATH_SEP
    local cmd
    if sep == "\\" then
        cmd = 'rmdir /s /q "' .. p .. '" 2>nul'
    else
        cmd = 'rm -rf "' .. p .. '" 2>/dev/null'
    end
    os.execute(cmd)
    return true
end
os.mkdir = function(p)
    if not p then return false end
    local sep = _PATH_SEP
    local cmd
    if sep == "\\" then
        cmd = 'mkdir "' .. p .. '" 2>nul'
    else
        cmd = 'mkdir -p "' .. p .. '" 2>/dev/null'
    end
    os.execute(cmd)
    return true
end

-- path module
path = {}
path.join = function(...)
    local parts = {...}
    local sep = "/"
    local result = parts[1] or ""
    for i = 2, #parts do
        if parts[i] and parts[i] ~= "" then
            result = result:gsub("[/\\]+$", "") .. sep .. parts[i]
        end
    end
    return result
end
path.filename = function(p)
    return (p or ""):match("[^/\\]+$") or ""
end
path.directory = function(p)
    return (p or ""):match("^(.*)[/\\][^/\\]+$") or ""
end
path.is_absolute = function(p)
    return (p or ""):sub(1,1) == "/" or (p or ""):match("^%a:[/\\]") ~= nil
end

-- io extensions
io.readfile = function(p)
    local f = io.open(p, "r")
    if not f then return nil end
    local content = f:read("*a"); f:close()
    return content
end
io.writefile = function(p, content)
    local f = io.open(p, "w")
    if not f then return false end
    f:write(content); f:close()
    return true
end

-- cprint: strip ${color} markers, fallback to print
cprint = function(fmt, ...)
    if type(fmt) == "string" then
        fmt = fmt:gsub("%${%w+}", "")
        local ok, msg = pcall(string.format, fmt, ...)
        print(ok and msg or fmt)
    else
        print(fmt)
    end
end

-- string.split: split string by separator
if not string.split then
    function string.split(s, sep, plain)
        local result = {}
        local i = 1
        while true do
            local j, k = s:find(sep, i, plain)
            if not j then
                table.insert(result, s:sub(i))
                break
            end
            table.insert(result, s:sub(i, j-1))
            i = k + 1
        end
        return result
    end
end

-- string:trim(): remove leading/trailing whitespace
if not string.trim then
    function string.trim(s)
        return s:match("^%s*(.-)%s*$") or s
    end
end

-- xmake compat globals
function is_host(name)
    local host = _RUNTIME and _RUNTIME.platform or os.host()
    return host == name
end
format = string.format
raise = function(msg) error(msg or "raise called", 2) end

-- string.replace: xmake compat (plain text replacement)
if not string.replace then
    function string.replace(s, old, new)
        -- Plain text replacement (not pattern)
        local result = s
        local i = 1
        while true do
            local pos = result:find(old, i, true)
            if not pos then break end
            result = result:sub(1, pos - 1) .. new .. result:sub(pos + #old)
            i = pos + #new
        end
        return result
    end
end

-- try/catch: simulates xmake's try { function, catch { function } } syntax
function try(block)
    local fn = block[1]
    local catch_block = block.catch
    local ok, result = pcall(fn)
    if not ok then
        if catch_block and catch_block[1] then
            catch_block[1](result)
        end
        return nil
    end
    return result
end
