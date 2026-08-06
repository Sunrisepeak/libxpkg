package = {
    spec    = "1",
    name    = "depsmixed",
    description = "Mixed-form deps: a positional runtime list beside build = {...}",
    licenses = {"MIT"},
    repo = "https://example.com/depsmixed",
    type = "package",
    archs = {"x86_64"},

    xpm = {
        linux = {
            -- The shape an author reaches for when they already have a list
            -- of runtime deps and want to add one install-time tool. Before
            -- 0.0.52 this took the array branch: `build` was dropped and the
            -- array was copied into build_deps instead.
            deps = {
                "node",
                "npm",
                build = { "patchelf" },
            },
            ["latest"] = { ref = "3.0.0" },
            ["3.0.0"]  = {
                url = "https://example.com/depsmixed-3.0.0.tar.gz",
                sha256 = "0000000000000000000000000000000000000000000000000000000000000000",
            },
        },
    },
}
