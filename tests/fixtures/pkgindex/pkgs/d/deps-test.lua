package = {
    spec = "1",
    name = "deps-test",
    description = "Test platform-level deps",

    xpm = {
        linux = {
            deps = {"glibc@2.39", "zlib@1.3"},
            ["1.0.0"] = {
                url    = "https://example.com/deps-test-1.0.0-linux.tar.gz",
                sha256 = "bbbb",
            },
        },
    },
}
