// StarforgeAppServices.cpp — editor-Play app services (App Platform / AP-01, contract §2).
//
// The logic behind the hook lines StarforgeApp.cpp carries in PlayScene / StopScene /
// TickPlay (that TU is at the /bigobj limit, so anything beyond a one-line call lives
// here). Editor Play mirrors the standalone PlayerLayer's frame order with two
// differences the contract fixes: services see InEditor = true, and the bus is
// EDITOR-owned — StopScene and ReloadModule destroy the scripts and the services but
// leave m_PlayBus intact, so the D-LIVE resume (AP-03) can hand a rebuilt module the
// values, history and producers of the previous run.

#include "StarforgeApp.h"

namespace Starforge
{
    void StarforgeApp::PlayServicesStart(Cosmic::Scene* runtime)
    {
        // A fresh Play clears the previous run's channels (values + history + producer
        // tags; subscriptions are kept — there are none from the editor side). The
        // D-LIVE resume (AP-03) skips this Clear to carry the bus across a rebuild.
        if (!m_PlayKeepBus) m_PlayBus.Clear();   // AP-03: the live-loop resume keeps values + history
        m_PlayKeepBus = false;
        m_PlayLastAbsTime = Cosmic::Application::Get().GetAbsoluteTime();

        // Services first (after all are constructed, OnAttach runs in order), on the
        // played scene with the play flow when one drives this session.
        m_PlayServices.Instantiate(m_Ctx.ProjectName,
            Cosmic::AppContext{ m_PlayBus, m_PlayPanels, runtime,
                                m_PlayFlowActive ? &m_PlayFlow : nullptr,
                                m_Ctx.ProjectName, /*InEditor=*/true });

        // Scripts reach the same bus through Data(); set before m_Scripts.Instantiate.
        m_Scripts.SetDataBus(&m_PlayBus);
    }

    void StarforgeApp::PlayServicesBindScene(Cosmic::Scene* scene)
    {
        m_PlayServices.BindScene(scene);
    }

    void StarforgeApp::PlayServicesStop()
    {
        // Scripts were destroyed by the caller; services go next (OnDetach in reverse
        // order, Panels cleared), then the scripts' bus pointer is dropped. The bus
        // itself is untouched — its channels outlive the run.
        m_PlayServices.Destroy();
        m_Scripts.SetDataBus(nullptr);
        m_PlayKeyBridge.Clear();
        m_PlayFlow.SetDataBus(nullptr);
    }

    void StarforgeApp::PlayServicesAdvanceBus()
    {
        // The bus clock runs on the UNSCALED frame delta (Age() is staleness in wall
        // time), sampled from the application's never-paused absolute clock so the
        // editor's own Pause / Step controls do not stall it.
        const float now = Cosmic::Application::Get().GetAbsoluteTime();
        m_PlayBus.Advance((double)(now - m_PlayLastAbsTime));
        m_PlayLastAbsTime = now;
    }

    void StarforgeApp::PlayFlowBindKeys(const Cosmic::FlowAsset& asset)
    {
        // Channel guards read the play bus; every "key:<Name>" transition the flow
        // names becomes a bound key (unknown names warn once). Bound BEFORE Start so
        // the first OnUpdate already polls them.
        m_PlayFlow.SetDataBus(&m_PlayBus);
        m_PlayKeyBridge.Bind(asset);
    }
}
