-- xim.libxpkg.system: system operations API
local M = {}

function M.exec(cmd, opt)
    opt = opt or {}
    local retries = opt.retry or 0
    local attempts = retries + 1
    for i = 1, attempts do
        local ret = os.execute(cmd)
        if ret == 0 or ret == true then return end
        if i == attempts then
            error("exec failed after " .. attempts .. " attempt(s): " .. tostring(cmd))
        end
    end
end

function M.rundir()           return _RUNTIME and _RUNTIME.run_dir or nil end
function M.xpkgdir()          return _RUNTIME and _RUNTIME.xpkg_dir or nil end
function M.bindir()           return _RUNTIME and _RUNTIME.bin_dir or nil end
function M.xpkg_args()        return (_RUNTIME and _RUNTIME.args) or {} end
function M.subos_sysrootdir() return _RUNTIME and _RUNTIME.subos_sysrootdir or nil end

function M.run_in_script(content, admin)
    local tmpfile = os.tmpname()
    -- write content to temp file
    if not io.writefile(tmpfile, content) then
        error("run_in_script: failed to write temp script")
    end
    local ok, err = pcall(function()
        os.execute("chmod +x " .. tmpfile)
        local prefix = (admin == true) and "sudo " or ""
        local ret = os.execute(prefix .. tmpfile)
        if ret ~= 0 and ret ~= true then
            error("script failed with code: " .. tostring(ret))
        end
    end)
    os.remove(tmpfile)  -- always cleanup
    if not ok then error(err) end
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
