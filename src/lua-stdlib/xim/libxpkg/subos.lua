-- xim.libxpkg.subos: subos manifest declarations (collects ops for C++ processing)
--
-- Slice 1 surface is a single entry point, `subos.env{}`: a package declares an
-- environment variable that its subos must set before user programs run. xlings
-- records the declaration in the subos manifest under the declaring package's
-- binding, expands it at activation time, and drops the whole section when that
-- package is uninstalled -- so a recipe never writes cleanup code for it.
--
-- This exists because PATH and RPATH do not cover every discovery protocol. A
-- GL driver is found through LIBGL_DRIVERS_PATH, an EGL vendor through
-- __EGL_VENDOR_LIBRARY_DIRS, a font config through XDG_DATA_DIRS -- none of
-- which any amount of linking can supply.
--
-- CAPABILITY PROBE -- recipes MUST write:
--
--     if type(subos.env) == "function" then ... end
--
-- and NOT `if subos.env then`. import() hands back a permissive proxy stub for
-- a module the running client does not ship, and every key read off that stub
-- is a truthy callable table. `if subos.env then` is therefore true on every
-- client, including the older ones that will accept the call and silently
-- discard it. type() separates them: the stub is a `table` carrying a __call
-- metamethod, the real entry point is a `function`.
--
-- The rule differs from `if xvm.files then` in the V2 spec only because `xvm`
-- is a module older clients already ship -- there the missing *field* really is
-- nil. A missing *module* never is.

local M = {}

local function _get_log()
    return _LIBXPKG_MODULES and _LIBXPKG_MODULES["log"]
end

_XVM_OPS = _XVM_OPS or {}

-- Slice 1 implements `set` and `prepend`. `set-if-unset` and `append` are
-- deliberately absent rather than silently accepted: a recipe that asks for one
-- gets told, instead of getting `set` and a surprise.
local _VALID_MODES = { set = true, prepend = true }

--- Declare an environment variable for the subos this package installs into.
-- @param opt table:
--   var      variable name (required)
--   op       "set" | "prepend" (default "set")
--   value    value; may contain ${pkgdir} / ${subosdir} / ${home} /
--            ${xlings_home}, expanded by xlings at activation time. Use them --
--            a literal absolute path pins the manifest to one machine.
--   binding  provider identity "<name>@<version>"; defaults to this package's
-- @return    true when the declaration was recorded
function M.env(opt)
    opt = opt or {}
    local log = _get_log()

    local var = opt.var or ""
    if var == "" then
        if log then log.warn("subos.env: missing 'var', declaration ignored") end
        return false
    end

    local mode = opt.op or "set"
    if not _VALID_MODES[mode] then
        if log then
            log.warn("subos.env: unsupported op '%s' for %s "
                     .. "(this client implements set|prepend), declaration ignored",
                     tostring(mode), var)
        end
        return false
    end

    local binding = opt.binding or ""
    if binding == "" and _RUNTIME then
        local n, v = _RUNTIME.pkg_name, _RUNTIME.version
        if n and n ~= "" and v and v ~= "" then binding = n .. "@" .. v end
    end
    if binding == "" then
        if log then
            log.warn("subos.env: cannot determine the declaring package for %s; "
                     .. "pass binding = \"<name>@<version>\"", var)
        end
        return false
    end

    -- `op` is the category the C++ side dispatches on, and it is already taken
    -- by the time this entry is read. The caller's set/prepend choice travels
    -- as `mode` so the two names never collide.
    table.insert(_XVM_OPS, {
        op      = "subos_env",
        var     = var,
        mode    = mode,
        value   = opt.value or "",
        binding = binding,
    })
    if log then
        log.debug("subos env %s %s=%s (%s)", mode, var, opt.value or "", binding)
    end
    return true
end

return M
