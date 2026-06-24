#pragma once

// TestLayers.h — the four bench-test screens for SF_TelemTest.
//
//   1. DriveSingleLayer  — verify a single drive ESC streams valid telemetry.
//   2. WeaponSingleLayer — verify the weapon ESC streams valid telemetry.
//   3. DualDriveLayer    — verify BOTH drive ESCs at once.
//   4. SnifferLayer      — detect ANY bytes on each telemetry wire (valid or
//                          not): "is the ESC sending anything at all?".
//
// Each ESC test reuses the dashboard look (hardware photo + overlay readout
// boxes) plus a big PASS / STALE / WAITING banner and frame diagnostics. The
// sniffer is a raw-activity dashboard.

#include <Cosmic.h>
#include "Telemetry.h"   // ESC_RIGHT / ESC_LEFT

namespace Workspace
{
    class TestHub;

    class DriveSingleLayer : public Cosmic::Layer
    {
    public:
        explicit DriveSingleLayer(TestHub* hub) : Cosmic::Layer("DriveSingleLayer"), m_Hub(hub) {}
        void OnAttach() override;
        void OnImGuiRender() override;
    private:
        TestHub* m_Hub = nullptr;
        Cosmic::Ref<Cosmic::Texture2D> m_Tex;
        int      m_Side = ESC_RIGHT;   // which drive ESC this test watches (R or L)
    };

    class WeaponSingleLayer : public Cosmic::Layer
    {
    public:
        explicit WeaponSingleLayer(TestHub* hub) : Cosmic::Layer("WeaponSingleLayer"), m_Hub(hub) {}
        void OnAttach() override;
        void OnImGuiRender() override;
    private:
        TestHub* m_Hub = nullptr;
        Cosmic::Ref<Cosmic::Texture2D> m_Tex;
    };

    class DualDriveLayer : public Cosmic::Layer
    {
    public:
        explicit DualDriveLayer(TestHub* hub) : Cosmic::Layer("DualDriveLayer"), m_Hub(hub) {}
        void OnAttach() override;
        void OnImGuiRender() override;
    private:
        TestHub* m_Hub = nullptr;
        Cosmic::Ref<Cosmic::Texture2D> m_Tex;
    };

    class SnifferLayer : public Cosmic::Layer
    {
    public:
        explicit SnifferLayer(TestHub* hub) : Cosmic::Layer("SnifferLayer"), m_Hub(hub) {}
        void OnImGuiRender() override;
    private:
        TestHub* m_Hub = nullptr;
    };

} // namespace Workspace
