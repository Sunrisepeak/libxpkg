package = {
    name = "pkgindex-update",
    description = "Build script that uses os.files to append template",
    xpm = { linux = { ["latest"] = {} } },
}
local projectdir = os.scriptdir()
local pkgsdir = path.join(projectdir, "pkgs")
local template = path.join(projectdir, "template.lua")
function installed() return false end
function install()
    local files = os.files(path.join(pkgsdir, "**.lua"))
    local template_content = io.readfile(template)
    for _, file in ipairs(files) do
        if not file:endswith("pkgindex-update.lua") then
            io.writefile(file, io.readfile(file) .. template_content)
        end
    end
    return true
end
function uninstall() return true end
