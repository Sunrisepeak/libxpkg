package = {
    spec = "2",
    name = "v2platform-context",
    xpm = {},
}

if is_host("linux") then
    package.xpm.linux = {
        ["1.0.0"] = {
            url = "https://example.test/linux-" .. os.arch() .. ".tar.gz",
        },
    }
elseif is_host("macosx") then
    package.xpm.macosx = {
        ["1.0.0"] = {
            url = "https://example.test/macosx-" .. os.arch() .. ".tar.gz",
        },
    }
end
