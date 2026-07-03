#pragma once

// FrontierApp.h
//
// ============================================================================
// Frontier — the Phase 11 flagship 3D showcase (root manager).
// ============================================================================
//
// One application, many explorable WORLDS (see World.h): a homescreen tile
// menu (SF_Telem pattern) selects between them; the root layer owns the world
// registry, the shared exploration camera, the dock layout, and the shared
// panels. Each world owns its content and drives its own frame.
//
//   * Frontier Island  — seamless volcano + snowy range + lake + ocean (F11/F12)
//   * Night Volcano    — the "realistic volcano" money shot          (F13)
//   * Blizzard Peak    — dynamic snow accumulation whiteout           (F14)
//   * Dawn Mirror Lake — the "realistic water" money shot             (F15)
//   * Storm Ocean      — heavy swell + rain + lightning               (F16)
//
// SKELETON STATUS (docs/plans/10-phase11-frontier-plan.md): ships with
// placeholder worlds and an OrbitCameraController stand-in. The work orders
// replace the stand-ins:
//   [F1 DONE]: FlyCameraController is the exploration camera (WASD + mouse-look);
//             the orbit rig stays as an inspect fallback toggle.
//   [F2 DONE]: worlds render through the engine SceneRenderer; the root owns one
//             and hands it to them via WorldContext (lazy Init on first entry).
//   [F3 DONE]: GPU-profiler HUD panel (per-pass ms) docked bottom-right — reads
//             the SceneRenderer's timer zones via RenderCommand::GetGpuZoneResults.
// ============================================================================

#include <Cosmic.h>

#include "World.h"

#include <memory>
#include <vector>

namespace Frontier
{
    class FrontierApp : public Cosmic::Layer
    {
    public:
        FrontierApp();
        virtual ~FrontierApp() override = default;

        virtual void OnAttach()                override;
        virtual void OnDetach()                override;
        virtual void OnUpdate(float ts)        override;
        virtual void OnImGuiRender()           override;
        virtual void OnEvent(Cosmic::Event& e) override;

    private:
        void SetWorld(int index);      // -1 = homescreen
        void DrawHomescreen();
        void DrawNavPanel();
        void ApplyDockLayout();
        int  DockStateKey() const;

        std::vector<std::unique_ptr<World>> m_Worlds;
        std::vector<bool>                   m_Attached;   // lazy OnAttach per world
        int   m_ActiveWorld = -1;
        int   m_AppliedDock = -99;
        float m_WorldTime   = 0.0f;    // resets when a world is entered

        // Exploration cameras: the fly camera (F1, default) and the orbit inspect
        // fallback. m_FlyCamera selects which one drives the active world.
        Cosmic::FlyCameraController   m_Fly{ 16.0f / 9.0f };
        Cosmic::OrbitCameraController m_Orbit{ 16.0f / 9.0f };
        bool m_FlyCamera = true;       // nav-panel "Fly / Orbit" toggle (fly default)

        // Engine frame orchestrator (F2): shared by every world through
        // WorldContext::Renderer. Lazily Init'd on the first world entry with the
        // live viewport size; Shutdown in OnDetach.
        Cosmic::SceneRenderer m_SceneRenderer;

        WorldContext m_LastCtx;        // snapshot for OnPanels
    };

} // namespace Frontier
