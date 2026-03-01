-- xim.libxpkg.log: logging API for xpkg scripts
local M = {}

local PREFIX = "[xim:xpkg]: "

local function _log(text, ...)
    if not text then return end
    local ok, msg = pcall(string.format, text, ...)
    msg = ok and msg or tostring(text)
    msg = msg:gsub("%${%w+}", "")
    io.write(PREFIX .. msg .. "\n")
    io.flush()
end

function M.info(text, ...)  _log(text, ...) end
function M.debug(text, ...) _log(text, ...) end
function M.warn(text, ...)  _log("[WARN] " .. (text or ""), ...) end
function M.error(text, ...) _log("[ERROR] " .. (text or ""), ...) end

return M
