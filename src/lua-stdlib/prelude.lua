-- prelude.lua: xmake compatibility layer + import() for libxpkg runtime
-- Loaded by PackageExecutor before any package script.

-- _LIBXPKG_MODULES is populated by C++ before this file runs
_LIBXPKG_MODULES = _LIBXPKG_MODULES or {}

-- import(): maps "xim.libxpkg.X" to preloaded modules
function import(mod_path)
    local name = mod_path:match("xim%.libxpkg%.(.+)")
    if name and _LIBXPKG_MODULES[name] then
        return _LIBXPKG_MODULES[name]
    end
    -- Stub for unknown imports (base.runtime etc.)
    return setmetatable({}, {
        __index = function(_, k) return function(...) end end
    })
end

-- os.* extensions (xmake compat)
os.isfile = function(p)
    local f = io.open(p, "r")
    if f then f:close() return true end
    return false
end
os.isdir = function(p)
    local sep = package.config:sub(1,1)
    if sep == "\\" then
        return os.execute('if exist "' .. p .. '\\" exit 0') == 0
    else
        return os.execute('[ -d "' .. p .. '" ]') == 0
    end
end
os.host = function()
    return _RUNTIME and _RUNTIME.platform or "linux"
end
os.trymv = function(src, dst)
    local ok = pcall(os.rename, src, dst)
    if ok then return true end
    -- Cross-device: fallback to copy + remove
    local inf = io.open(src, "rb")
    if not inf then return false end
    local content = inf:read("*a"); inf:close()
    local outf = io.open(dst, "wb")
    if not outf then return false end
    outf:write(content); outf:close()
    local rm_ok = pcall(os.remove, src)
    return rm_ok  -- only report success if source was removed
end
os.mv = function(src, dst) return os.trymv(src, dst) end
os.cp = function(src, dst)
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
    local sep = package.config:sub(1,1)
    local cmd
    if sep == "\\" then
        cmd = 'dir /B /AD "' .. pattern .. '" 2>nul'
    else
        cmd = 'ls -d "' .. pattern .. '" 2>/dev/null'
    end
    local f = io.popen(cmd)
    if f then
        for line in f:lines() do
            line = line:gsub("[\r\n]+$", "")  -- strip CRLF
            if line ~= "" and os.isdir(line) then
                table.insert(result, line)
            end
        end
        f:close()
    end
    return result
end
os.sleep = function(ms) end  -- stub

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
