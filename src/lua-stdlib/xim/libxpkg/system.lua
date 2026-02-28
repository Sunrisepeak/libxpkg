-- xim.libxpkg.system: system operations API
local M = {}

function M.exec(cmd, opt)
    opt = opt or {}
    if opt.retry then
        local retries = opt.retry
        while retries > 0 do
            local ret = os.execute(cmd)
            if ret == 0 or ret == true then return end
            retries = retries - 1
        end
    end
    local ret = os.execute(cmd)
    if ret ~= 0 and ret ~= true then
        error("exec failed: " .. tostring(cmd))
    end
end

function M.rundir()        return _RUNTIME.run_dir end
function M.xpkgdir()       return _RUNTIME.xpkg_dir end
function M.bindir()        return _RUNTIME.bin_dir end
function M.xpkg_args()     return _RUNTIME.args or {} end
function M.subos_sysrootdir() return _RUNTIME.subos_sysrootdir end

function M.run_in_script(content, admin)
    local tmpfile = os.tmpname() .. ".sh"
    io.writefile(tmpfile, content)
    os.execute("chmod +x " .. tmpfile)
    local prefix = (admin == true) and "sudo " or ""
    os.execute(prefix .. tmpfile)
    os.remove(tmpfile)
end

function M.unix_api()
    return {
        append_to_shell_profile = function(config)
            if not config then return end
            if type(config) == "string" then
                config = { posix = config, fish = config }
            end
            local profile_dir = _RUNTIME.run_dir or "/tmp"
            local posix = path.join(profile_dir, "xlings-profile.sh")
            local fish  = path.join(profile_dir, "xlings-profile.fish")
            if config.posix and os.isfile(posix) then
                local cur = io.readfile(posix) or ""
                if not cur:find(config.posix, 1, true) then
                    io.writefile(posix, cur .. "\n" .. config.posix)
                end
            end
            if config.fish and os.isfile(fish) then
                local cur = io.readfile(fish) or ""
                if not cur:find(config.fish, 1, true) then
                    io.writefile(fish, cur .. "\n" .. config.fish)
                end
            end
        end
    }
end

return M
