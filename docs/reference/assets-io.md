# API Reference — Assets, Files & Config

> **STATUS: SKELETON** — to be filled by work order **D16** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/assets/AssetLibrary.h`,
`utils/FileSystem.h`, `utils/Config.h`, `utils/DataExport.h`.

**Read first:** root README §17 (VFS); systems explainer
[assets-vfs](../systems/assets-vfs.md).

## Coverage checklist *(starting point — headers are authoritative)*

- [ ] `FileSystem` — `Resolve` + every scheme (`engine://`, `project://`, `user://` — resolution rules, and the **DLL-side resolution rule: `project://` must be resolved in the calling DLL**), `SetActiveProject`, existence/dir helpers
- [ ] `AssetLibrary` (S4.4a) — cached `Get`/load for textures/shaders/meshes/models, cache key semantics, eviction/reset, cross-DLL cautions
- [ ] `Config` (E10, TOML) — load/parse, typed getters with defaults, error behavior on missing file/key, save if supported
- [ ] `DataExport` — CSV/data export API, formatting rules, where files land (`user://`)

## Sections to write

1. VFS scheme table with concrete resolved-path examples (dev tree vs packaged app — paths differ; `user://` is the writable root). <!-- TODO(D16) -->
2. Entries per checklist. <!-- TODO(D16) -->

---
*Changelog:*
