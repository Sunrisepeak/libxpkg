-- xim.libxpkg.pkgmanager: sub-dependency installation via CLI delegation
local M = {}

function M.install(target)
    if not target or target == "" then return end
    io.write("[xim:xpkg]: pkgmanager.install(" .. tostring(target) .. ")\n")
    io.flush()
    os.execute("xlings install " .. target .. " -y")
end

function M.remove(target)
    if not target or target == "" then return end
    io.write("[xim:xpkg]: pkgmanager.remove(" .. tostring(target) .. ")\n")
    io.flush()
    os.execute("xlings remove " .. target .. " -y")
end

function M.uninstall(target)
    M.remove(target)
end

return M
