# imgui-node-editor — vendor notes

- **Source:** https://github.com/thedmd/imgui-node-editor (MIT — see `LICENSE`)
- **Vendored:** 2026-07-11, master commit `021aa0ea4da13fed864bafb2a92d4c5205076866`
  (2026-03-29 "minor styling change") — master carries the current-imgui
  compatibility fixes; the last tag (v0.9.3) predates them.
- **Files:** the library sources only (`imgui_node_editor*`, `imgui_canvas.*`,
  `imgui_extra_math.*`, `imgui_bezier_math.*`, `crude_json.*`). Examples, docs
  and the bundled imgui copy are NOT vendored — it compiles against the
  engine's `dependencies/imgui` (1.92.8 WIP at vendor time).
- **Build:** compiled into the Starforge editor DLL (the only consumer today);
  the sources are appended in `Projects/Starforge/CMakeLists.txt` with warnings
  suppressed for the vendored files (external code; the engine/app zero-warning
  bar is unchanged). Phase 25 (doc 24, Q1) extracts the editor's generic canvas
  wrapper (`Projects/Starforge/src/widgets/NodeCanvas.*`) into a reusable
  widget when the Story Graph lands.
- **Local patches:**
  1. `imgui_extra_math.inl` — `operator*(const float, const ImVec2&)` wrapped in
     `#if IMGUI_VERSION_NUM < 19270`: imgui 1.92.x ships that operator itself
     (its math operators are always-on since 1.90), so the unguarded definition
     collided (C2084) against the engine's imgui 1.92.8.
