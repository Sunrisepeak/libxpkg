local M = {}

function M.install(target)
    if not target or target == "" then return end
    io.write("[xim:xpkg]: pkgmanager.install(" .. tostring(target) .. ")\n")
    io.flush()
    if not _INSTALL_REQUESTS then _INSTALL_REQUESTS = {} end
    table.insert(_INSTALL_REQUESTS, {op = "install", target = tostring(target)})
end

function M.remove(target)
    if not target or target == "" then return end
    io.write("[xim:xpkg]: pkgmanager.remove(" .. tostring(target) .. ")\n")
    io.flush()
    if not _INSTALL_REQUESTS then _INSTALL_REQUESTS = {} end
    table.insert(_INSTALL_REQUESTS, {op = "remove", target = tostring(target)})
end

function M.uninstall(target)
    M.remove(target)
end

return M
