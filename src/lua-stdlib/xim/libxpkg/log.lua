-- xim.libxpkg.log: logging API for xpkg scripts
local M = {}

local PREFIX = "[xim:xpkg]: "

-- Log levels: 0=debug, 1=info, 2=warn, 3=error, 4=silent
local LEVEL_DEBUG = 0
local LEVEL_INFO  = 1
local LEVEL_WARN  = 2
local LEVEL_ERROR = 3

local _level = LEVEL_INFO  -- default: show info and above

local function _log(text, ...)
    if not text then return end
    local ok, msg = pcall(string.format, text, ...)
    msg = ok and msg or tostring(text)
    msg = msg:gsub("%${%w+}", "")
    io.write(PREFIX .. msg .. "\n")
    io.flush()
end

function M.debug(text, ...)
    if _level <= LEVEL_DEBUG then _log(text, ...) end
end

function M.info(text, ...)
    if _level <= LEVEL_INFO then _log(text, ...) end
end

function M.warn(text, ...)
    if _level <= LEVEL_WARN then _log("[WARN] " .. (text or ""), ...) end
end

function M.error(text, ...)
    if _level <= LEVEL_ERROR then _log("[ERROR] " .. (text or ""), ...) end
end

-- Set log level: "debug", "info", "warn", "error", "silent"
function M.set_level(level)
    if level == "debug" or level == 0 then _level = LEVEL_DEBUG
    elseif level == "info" or level == 1 then _level = LEVEL_INFO
    elseif level == "warn" or level == 2 then _level = LEVEL_WARN
    elseif level == "error" or level == 3 then _level = LEVEL_ERROR
    elseif level == "silent" or level == 4 then _level = 4
    end
end

function M.get_level()
    return _level
end

return M
