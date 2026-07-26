# API Reference — Assets, Files & Config

> **STATUS: SKELETON** — to be filled by work order **D16** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Entry format: [reference/README.md → Entry format](README.md#entry-format-mandatory--copy-this-shape).

**Scope (headers are the truth):** `Cosmic/src/assets/AssetLibrary.h`,
`utils/FileSystem.h`, `utils/Config.h`, `utils/DataExport.h`.
**Add when writing (D58/D60/D61):** `utils/FileWatcher.h`, `utils/FileDialog.h`, `utils/ImageIO.h`
and `utils/ExeResources.h` are included by `Cosmic.h` **directly and unfenced** but have **no row in
the [coverage manifest](README.md#coverage-manifest--every-public-header-maps-to-a-chapter)** — they
belong here. `utils/Branding.h` is a fifth and a worse case: `COSMIC_API`-exported, unit-tested
(`tests/test_branding.cpp`) and called from a project DLL, yet **not included by `Cosmic.h` at all**.
`assets/MeshImport.h` (3D-only) is already flagged in the manifest prose.
`ExeResources` and `Branding` are packaging/branding surface — the client-facing sources are
[`../guide/building-and-shipping.md`](../guide/building-and-shipping.md) (D61) and
[`../guide/windowing-and-viewport.md`](../guide/windowing-and-viewport.md) (D60) respectively.

**Read first:** the guide chapter
[`../guide/assets-and-vfs.md`](../guide/assets-and-vfs.md) (D58 — written from source; it is the
client-facing source for this material until this chapter lands); systems explainer
[assets-vfs](../systems/assets-vfs.md).

## Coverage checklist *(starting point — headers are authoritative)*

- [ ] `FileSystem` — `Resolve` + every scheme (`engine://`, `project://`, `user://`), the two `project://` mount modes (`SetActiveProject` NAME vs `SetActiveProjectPath` PATH, last-setter-wins), `SetAppIdentity`/`AppIdentity`/`GetUserDataRoot` (per-app isolation + portable mode, S6) and `ActiveProjectPath`
  - **The old "DLL-side resolution rule" is OBSOLETE (D58).** The mount state moved into the engine DLL in Phase 20 / A1, so there is one active project per *process* and `project://` resolves the same from any module. Four in-tree comments still teach the old rule (`TemplateProject.cpp`, `SimHub.cpp`, `Sound.h`, `LookupTable.h`) — do not carry it into this chapter.
- [ ] `AssetLibrary` (S4.4a) — cached `Get`/load for textures/shaders/materials/meshes/models/clips, `NormalizeKey` semantics, `SetDefaultTextureSampling`, `Reload`, `Enumerate`, `Clear` (GL-context precondition), and the **per-type miss policy** (a failed texture is cached as a degraded object; a failed shader/material/mesh/model is not cached and retries)
- [ ] `Config` (E10, TOML) — load/parse, typed getters with defaults, error behavior on missing file/key, save if supported
- [ ] `DataExport` — CSV/data export API, formatting rules, where files land (`user://`)

## Sections to write

1. VFS scheme table with concrete resolved-path examples (dev tree vs packaged app — paths differ; `user://` is the writable root). <!-- TODO(D16) -->
2. Entries per checklist. <!-- TODO(D16) -->

---
*Changelog:*
