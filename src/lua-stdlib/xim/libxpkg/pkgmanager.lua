local M = {}

local function _get_log()
    return _LIBXPKG_MODULES and _LIBXPKG_MODULES["log"]
end

function M.install(target)
    if not target or target == "" then return end
    local log = _get_log()
    if log then log.debug("pkgmanager.install(%s)", tostring(target)) end
    if not _INSTALL_REQUESTS then _INSTALL_REQUESTS = {} end
    table.insert(_INSTALL_REQUESTS, {op = "install", target = tostring(target)})
end

function M.remove(target)
    if not target or target == "" then return end
    local log = _get_log()
    if log then log.debug("pkgmanager.remove(%s)", tostring(target)) end
    if not _INSTALL_REQUESTS then _INSTALL_REQUESTS = {} end
    table.insert(_INSTALL_REQUESTS, {op = "remove", target = tostring(target)})
end

function M.uninstall(target)
    M.remove(target)
end

return M
