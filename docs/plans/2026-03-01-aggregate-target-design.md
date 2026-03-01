# Design: Aggregate `xpkg` Target

## Goal

Add a single `xpkg` target that aggregates all libxpkg sub-modules, so downstream users can write `add_deps("xpkg")` instead of listing every module individually.

## Approach

Add a `phony` target named `xpkg` in root `xmake.lua` that depends on all 5 sub-module targets and publicly forwards `mcpplibs-capi-lua`.

## Changes

1. **xmake.lua** — add `xpkg` phony target before `includes()`
2. **examples/xmake.lua** — simplify `lifecycle` to `add_deps("xpkg")`
3. **README.md** — update xmake integration section to show `add_deps("xpkg")`
4. **docs/architecture.md** — add aggregate target to module overview and dependency graph
