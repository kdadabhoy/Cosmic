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
  2. `imgui_node_editor.cpp` — an optional **link router** (UX-01, 2026-09-24; upstream
     `021aa0ea4da13fed864bafb2a92d4c5205076866` has no routing hook). Two hunks, both at
     `ed::Link::GetCurve` (upstream `imgui_node_editor.cpp:955-982`):
     (a) before it, the declaration + definition of
     `ax::NodeEditor::Detail::g_CosmicLinkRouter`, a function pointer
     `void (*)(ImVec2 start, ImVec2 end, const ImRect& startNode, const ImRect& endNode, float strength, ImVec2& cp0, ImVec2& cp1)`,
     `nullptr` by default (stock behaviour);
     (b) inside it, when the pointer is set and both pins belong to nodes, `cp0` / `cp1` come
     from the router called with `m_Start`, `m_End`, `m_StartPin->m_Node->m_Bounds`,
     `m_EndPin->m_Node->m_Bounds` and `m_StartPin->m_Strength`; otherwise (no router, or the
     node-less cursor pin of a link being dragged out) the upstream lines run unchanged.
     `Link::TestHit` and `Link::GetBounds` call `GetCurve`, so picking and bounds follow.
     **Why:** upstream points `cp0` along the start pin's direction and `cp1` along the end
     pin's, so a link whose target lies left of its source, or a self-loop, is drawn through
     both node boxes (KI-67). Starforge's `NodeCanvas::Begin` installs
     `NodeCanvas::RouteLink` (`Projects/Starforge/src/widgets/NodeCanvasRoute.cpp`), which
     returns upstream's exact points for forward links and a below-the-nodes cubic for
     backward links / self-loops; CosmicTests "UX-01 FE01" pins both. No header of the
     library changed; the Starforge side declares the same pointer `extern` in
     `widgets/NodeCanvas.cpp`. Re-vendoring: re-apply both hunks (or drop them and the
     `NodeCanvas::Begin` line together).
