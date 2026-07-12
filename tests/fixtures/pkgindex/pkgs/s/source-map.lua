package = {
    spec = "2",
    name = "source-map",
    xpm = {
        source = {
            GLOBAL = "https://github.com/example/tool/releases/download/v${version}/tool-${arch_alias}.tar.gz",
            CN = "https://gitcode.com/xlings-res/tool/releases/download/${version}/tool-${arch_alias}.tar.gz",
        },
        linux = {
            ["1.0.0"] = {
                sha256 = "same-bytes",
                arch_alias = { x86_64 = "amd64" },
            },
        },
    },
}
