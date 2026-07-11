package = {
    spec = "2",
    name = "v2source-template",
    xpm = {
        source = "https://example.test/${name}/${version}/${name}-${os}-${arch}.${ext}",
        linux = {
            source = "https://linux.example.test/${version}/tool-${arch_alias}.tar.xz",
            ["1.0.0"] = {
                sha256 = {
                    x86_64 = "linux-amd64-hash",
                    aarch64 = "linux-arm64-hash",
                },
                arch_alias = {
                    x86_64 = "amd64",
                    aarch64 = "arm64",
                },
            },
            ["custom"] = {
                url = "https://override.test/custom.tar.gz",
                sha256 = "custom-hash",
            },
        },
        windows = {
            ["1.0.0"] = {},
        },
    },
}
