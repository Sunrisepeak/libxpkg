package = {
    spec = "2",
    name = "v2source-res",
    archs = {"x86_64", "aarch64"},
    xpm = {
        source = "xlings-res",
        linux = {
            ["latest"] = { ref = "1.0.0" },
            ["1.0.0"] = {
                sha256 = {
                    x86_64 = "linux-amd64-hash",
                    aarch64 = "linux-arm64-hash",
                },
            },
        },
        macosx = {
            ["1.0.0"] = {},
        },
    },
}
