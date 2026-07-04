# nlohmann/json (vendored)

Single-header JSON for Modern C++. Used by the engine's `scene/SceneSerializer`
(Phase 13 / E2) for `.cscene` / `.cprefab` / `.cmat` files.

- **Version pinned:** v3.11.3
- **Source:** https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
- **License:** MIT

**Kept PRIVATE to the engine** — `json.hpp` is included only from engine `.cpp`
files (never a public header), so client DLLs don't pay its compile cost and the
`Cosmic::json` types never cross the DLL boundary. To upgrade, replace `json.hpp`
with a newer single-header release and bump the version above.
