# Assets & Virtual File System — How It Works

> **STATUS: SKELETON** — to be filled by work order **D32** in
> [`docs/plans/12-documentation-plan.md`](../plans/12-documentation-plan.md).
> Format + writing bar: [systems/README.md](README.md#document-format-mandatory--every-explainer-uses-this-shape).

**One-liner:** paths like `project://shaders/Fire.glsl` resolve to real files whether you're
in the dev tree or a packaged install; `AssetLibrary` caches what's loaded; glTF models come
in through cgltf with PBR materials attached.
**Source:** `Cosmic/src/utils/FileSystem.h`, `assets/AssetLibrary.*`, `graphics/Model.*` + `CgltfImpl.cpp`, `graphics/Shader.cpp` (preprocessor)
**API Reference:** [../reference/assets-io.md](../reference/assets-io.md) · **Guide:** root README §17, §37

## Section plan

1. **Overview** — why hardcoded paths break (dev tree vs installed app vs per-user data) and how schemes fix it. <!-- TODO(D32) -->
2. **Mental model** — the three schemes table (`engine://` read-only engine assets, `project://` per-project assets, `user://` writable data root) with resolved examples in both dev and packaged layouts. <!-- TODO(D32) -->
3. **Step-by-step** — `Resolve("project://…")` decision path, incl. `SetActiveProject` and **the DLL-side resolution rule** (resolve in the calling DLL). <!-- TODO(D32) -->
4. **Technical implementation** — `AssetLibrary` cache keys + lifetime (S4.4a), glTF import path (cgltf, tangents, PBR factor/texture import, winding fixes), shader preprocessing (`#type` blocks — mine README §37), texture decode-from-memory, mip/sRGB policy (S12.6 by-design closure; BCn parked w/ unlock conditions). <!-- TODO(D32) -->
5. **Design decisions** — loose files over pack formats (Starforge anti-goal), cgltf now + assimp later (doc 11 E16). <!-- TODO(D32) -->
6. **Limits & future work.** <!-- TODO(D32) -->

**Truth sources:** `FileSystem.h` (it's header-only — read it whole), `Runtime/Main.cpp`
(CWD/exe-relative rules), README §17/§37 (migrating here), doc 05 §11 S12.6 note.
