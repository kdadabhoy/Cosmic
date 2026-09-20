// AP01ServiceFixture.cpp — AP-01 V02 (W): a REAL generated-style game module that
// registers an app service (CS_SERVICE) which registers a hosted panel (CS_PANEL)
// from OnAttach, plus a CS_SCRIPT, through the shipped CS_MODULE_BEGIN/END macros —
// exactly what a scaffolded project's Module.cpp does. Two hosts load and unload it
// 20 times each (tests/test_servicehost.cpp):
//   * the editor path: the real Starforge GameModule TU (LoadLibrary +
//     CosmicModule_Register / UnregisterModule + FreeLibrary) with a ServiceHost and
//     a DataBus the TEST owns, so the bus outlives every reload;
//   * the player path: the real Application / PlayerLayer (CreatePluginLayer from
//     CS_MODULE_END), TransitionToLauncher / TransitionFromLauncherToWorkspace.
// Everything it observes lands in the exe-owned AP01ServiceReport (env-var pointer,
// the WO-07 F-LIFETIME pattern) so the counters survive FreeLibrary.
#include "AP01ServiceReport.h"

#include <Cosmic.h>
#include "scripting/AppService.h"
#include "scripting/ServiceHost.h"
#include "data/DataBus.h"

#include <cstdlib>
#include <string>

namespace
{
    AP01ServiceReport* report = nullptr;

    AP01ServiceReport* ReadReportFromEnv()
    {
        char* value = nullptr; size_t len = 0;
        _dupenv_s(&value, &len, "COSMIC_AP01_REPORT");
        if (!value) return nullptr;
        auto* r = reinterpret_cast<AP01ServiceReport*>(_strtoui64(value, nullptr, 16));
        free(value);
        return r;
    }

    // Static object: constructed at DLL_PROCESS_ATTACH (LoadLibrary), destroyed at
    // DLL_PROCESS_DETACH (inside FreeLibrary) — its destructor stamp is the "the DLL
    // image went away" event every "before FreeLibrary" assertion compares against.
    struct DllLifetimeProbe
    {
        DllLifetimeProbe()
        {
            report = ReadReportFromEnv();
            if (report) ++report->dllLoaded;
        }
        ~DllLifetimeProbe()
        {
            if (report)
            {
                report->seqDllDetach = ++report->seq;
                ++report->dllUnloaded;
            }
        }
    } g_Probe;
}

// The app service under test: publishes channels, subscribes to one, registers a
// hosted panel, and records every callback + ordering stamp.
class AP01Service : public Cosmic::AppService
{
public:
    AP01Service() { if (report) ++report->constructed; }
    ~AP01Service() override
    {
        if (report) { report->seqDeleted = ++report->seq; ++report->destroyed; }
    }

protected:
    void OnAttach(Cosmic::AppContext& ctx) override
    {
        if (!report) return;
        report->seqAttach      = ++report->seq;
        report->attachInEditor = ctx.InEditor ? 1 : 0;
        report->attachHadFlow  = ctx.Flow ? 1 : 0;
        report->attachHadScene = ctx.ActiveScene ? 1 : 0;
        report->unloaded       = 0;

        // Bus persistence across reloads: read what the previous load wrote, bump it.
        const double reloads = Bus().GetNumber("ap01.reloads", 0.0);
        report->reloadsSeen = (int)reloads;
        Bus().Set("ap01.reloads", reloads + 1.0);

        m_Ping = Bus().Subscribe("host.ping", [](const std::string&, const Cosmic::DataValue&)
        {
            if (!report) return;
            if (report->unloaded) ++report->pingAfterUnload;
            else                  ++report->pingWhileLive;
        });

        CS_PANEL("AP01Panel", [](const Cosmic::UiRect&) { if (report) ++report->panelDraws; });
        report->panelRegistered = Panels().Has("AP01Panel") ? 1 : 0;
        ++report->attached;
    }

    void OnDetach() override
    {
        Bus().Unsubscribe(m_Ping);
        m_Ping = 0;
        if (!report) return;
        report->seqDetach = ++report->seq;
        ++report->detached;
        report->unloaded = 1;   // any callback into this module after this point is a defect
    }

    void OnUpdate(float ts) override
    {
        (void)ts;
        ++m_Ticks;
        Bus().Set("ap01.tick", (double)m_Ticks);
        if (!report) return;
        ++report->updates;
        if (Bus().Producer("ap01.tick") == "AP01Service") ++report->producerSeen;
    }

    void OnFixedUpdate(float fixedDt) override { (void)fixedDt; if (report) ++report->fixedUpdates; }
    void OnEvent(Cosmic::Event& e) override { (void)e; if (report) ++report->events; }
    void OnSceneChanged(Cosmic::Scene*, Cosmic::Scene*) override { if (report) ++report->sceneChanges; }

    void OnSignal(const std::string& signal, Cosmic::Entity source) override
    {
        (void)signal; (void)source;
        if (!report) return;
        if (report->unloaded) ++report->signalsAfterUnload;
        else                  ++report->signalsWhileLive;
    }

private:
    Cosmic::DataBus::Handle m_Ping = 0;
    int m_Ticks = 0;
};

// A script in the same module (registry-strip proof + the Data() proxy from a DLL).
class AP01Script : public Cosmic::ScriptableEntity
{
public:
    float Rate = 1.0f;
protected:
    void OnUpdate(float) override
    {
        Data().Set("ap01.script", Data().GetNumber("ap01.script", 0.0) + 1.0);
        if (report) ++report->scriptUpdates;
    }
};

CS_MODULE_BEGIN(AP01ServiceFixture)
    CS_SCRIPT(AP01Script)
        CS_FIELD(Rate)
    CS_END;
    CS_SERVICE(AP01Service).Order(0) CS_END;
CS_MODULE_END()
