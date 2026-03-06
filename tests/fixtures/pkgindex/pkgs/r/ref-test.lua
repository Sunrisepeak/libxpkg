package = {
    spec = "1",
    name = "ref-test",
    description = "Test platform-level ref inheritance",

    xpm = {
        debian = {
            ["1.0.0"] = {
                url    = "https://example.com/ref-test-1.0.0-debian.tar.gz",
                sha256 = "aaaa",
            },
        },
        ubuntu = { ref = "debian" },
    },
}
