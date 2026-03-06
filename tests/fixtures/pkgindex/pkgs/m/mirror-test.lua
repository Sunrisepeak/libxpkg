package = {
    spec = "1",
    name = "mirror-test",
    description = "Test mirror URL table format",

    xpm = {
        linux = {
            ["1.0.0"] = {
                url = {
                    GLOBAL = "https://global.example.com/mirror-test-1.0.0.tar.gz",
                    CN     = "https://cn.example.com/mirror-test-1.0.0.tar.gz",
                },
                sha256 = "cccc",
            },
        },
    },
}
