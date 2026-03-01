-- xim.libxpkg.utils: utility functions
local M = {}

function M.filepath_to_absolute(filepath)
    if path.is_absolute(filepath) then return filepath end
    return path.join(os.getenv("PWD") or ".", filepath)
end

function M.try_download_and_check(url, dir, sha256)
    local filename = url:match("[^/]+$") or "download"
    local dest = path.join(dir, filename)
    local ret = os.execute(string.format('curl -fsSL -o "%s" "%s"', dest, url))
    if ret ~= 0 and ret ~= true then
        io.write("[xim:xpkg]: download failed: " .. url .. "\n")
        return false
    end
    if sha256 then
        local f = io.popen("sha256sum " .. dest)
        local out = f and f:read("*l") or ""
        if f then f:close() end
        local actual = out:match("^(%x+)")
        if actual ~= sha256 then
            io.write("[xim:xpkg]: sha256 mismatch for " .. dest .. "\n")
            return false
        end
    end
    return true
end

function M.input_args_process(cmds_kv, args)
    local result = {}
    for _, arg in ipairs(args or {}) do
        local k, v = arg:match("^%-%-(%w+)=(.+)$")
        if k then result[k] = v end
    end
    return result
end

return M
