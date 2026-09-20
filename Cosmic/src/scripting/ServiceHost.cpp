// scripting/ServiceHost.cpp — the per-run app-service driver + the hosted-panel
// registry (App Platform / AP-01). See ServiceHost.h and AppService.h.

#include "scripting/ServiceHost.h"
#include "scripting/AppService.h"
#include "scripting/ModuleRegistry.h"

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/EventBus.h"
#include "core/Log.h"

#include <algorithm>

namespace Cosmic
{
    // ========================================================================
    // PanelRegistry
    // ========================================================================

    void PanelRegistry::Register(const std::string& name, DrawFn fn, const char* file, int line)
    {
        Entry& e = m_Panels[name];            // re-register replaces
        e.Fn       = std::move(fn);
        e.Src.File = file ? file : "";
        e.Src.Line = line;
    }

    void PanelRegistry::Unregister(const std::string& name)
    {
        m_Panels.erase(name);
    }

    bool PanelRegistry::Has(const std::string& name) const
    {
        return m_Panels.find(name) != m_Panels.end();
    }

    bool PanelRegistry::Draw(const std::string& name, const UiRect& rect) const
    {
        auto it = m_Panels.find(name);
        if (it == m_Panels.end() || !it->second.Fn)
            return false;
        it->second.Fn(rect);
        return true;
    }

    PanelRegistry::Source PanelRegistry::SourceOf(const std::string& name) const
    {
        auto it = m_Panels.find(name);
        return it == m_Panels.end() ? Source{} : it->second.Src;
    }

    std::vector<std::string> PanelRegistry::Names() const
    {
        std::vector<std::string> out;
        out.reserve(m_Panels.size());
        for (const auto& [name, e] : m_Panels)   // std::map: already sorted
            out.push_back(name);
        return out;
    }

    void PanelRegistry::Clear()
    {
        m_Panels.clear();
    }

    // ========================================================================
    // ServiceHost
    // ========================================================================

    ServiceHost::ProducerScope::ProducerScope(ServiceHost& host, const std::string& name)
        : Host(host), Previous(host.m_ActiveProducer)
    {
        Host.m_ActiveProducer = name;
        if (Host.m_Ctx) Host.m_Ctx->Bus.SetProducer(name);
    }

    ServiceHost::ProducerScope::~ProducerScope()
    {
        Host.m_ActiveProducer = Previous;
        if (Host.m_Ctx) Host.m_Ctx->Bus.SetProducer(Previous);
    }

    ServiceHost::~ServiceHost()
    {
        Destroy();
    }

    void ServiceHost::Instantiate(const std::string& module, const AppContext& ctx)
    {
        Destroy();                     // idempotent re-entry
        m_Ctx.emplace(ctx);
        m_Instantiated = true;

        // Registration order (the registry keeps a vector), stable-sorted by Order.
        auto& reg = ModuleRegistry::Get();
        std::vector<const ServiceDescriptor*> descs;
        for (const std::string& name : reg.ServiceNames(module))
            if (const ServiceDescriptor* d = reg.FindService(name))
                descs.push_back(d);
        std::stable_sort(descs.begin(), descs.end(),
            [](const ServiceDescriptor* a, const ServiceDescriptor* b) { return a->Order < b->Order; });

        // Pass 1 — construct + inject the context (nothing runs yet).
        for (const ServiceDescriptor* d : descs)
        {
            AppService* inst = d->Factory ? d->Factory() : nullptr;
            if (!inst)
            {
                CS_CORE_WARN("ServiceHost: service '{0}' has no factory — skipped.", d->Name);
                continue;
            }
            inst->m_Ctx = &*m_Ctx;
            m_Live.push_back({ inst, *d });
        }

        // Pass 2 — OnAttach in instantiation order, after EVERY service exists and
        // before the first BindScene.
        for (size_t i = 0; i < m_Live.size(); ++i)
        {
            ProducerScope scope(*this, m_Live[i].Desc.Name);
            m_Live[i].Instance->OnAttach(*m_Ctx);
        }
    }

    void ServiceHost::BindScene(Scene* scene)
    {
        if (scene == m_BoundScene)
            return;
        Scene* old = m_BoundScene;
        if (old && m_BusHandle)
            old->Events().Disconnect(m_BusHandle);
        m_BusHandle  = 0;
        m_BoundScene = scene;
        if (scene)
            m_BusHandle = scene->Events().ConnectAny(
                [this](const std::string& signal, Entity source) { DispatchSignal(signal, source); });
        if (m_Ctx)
            m_Ctx->ActiveScene = scene;
        for (size_t i = 0; i < m_Live.size(); ++i)
        {
            ProducerScope scope(*this, m_Live[i].Desc.Name);
            m_Live[i].Instance->OnSceneChanged(old, scene);
        }
    }

    void ServiceHost::Tick(float ts)
    {
        for (size_t i = 0; i < m_Live.size(); ++i)
        {
            ProducerScope scope(*this, m_Live[i].Desc.Name);
            m_Live[i].Instance->OnUpdate(ts);
        }
    }

    void ServiceHost::FixedTick(float fixedDt)
    {
        for (size_t i = 0; i < m_Live.size(); ++i)
        {
            ProducerScope scope(*this, m_Live[i].Desc.Name);
            m_Live[i].Instance->OnFixedUpdate(fixedDt);
        }
    }

    void ServiceHost::DispatchEvent(Event& e)
    {
        for (size_t i = 0; i < m_Live.size(); ++i)
        {
            ProducerScope scope(*this, m_Live[i].Desc.Name);
            m_Live[i].Instance->OnEvent(e);
        }
    }

    void ServiceHost::DispatchSignal(const std::string& signal, Entity source)
    {
        for (size_t i = 0; i < m_Live.size(); ++i)
        {
            ProducerScope scope(*this, m_Live[i].Desc.Name);
            m_Live[i].Instance->OnSignal(signal, source);
        }
    }

    void ServiceHost::Destroy()
    {
        // OnDetach in REVERSE instantiation order, every instance still alive.
        for (size_t i = m_Live.size(); i-- > 0; )
        {
            ProducerScope scope(*this, m_Live[i].Desc.Name);
            m_Live[i].Instance->OnDetach();
        }
        // Delete (reverse order too); the vtables are module code — this runs
        // before the host's UnregisterModule / FreeLibrary (KI-29).
        for (size_t i = m_Live.size(); i-- > 0; )
        {
            m_Live[i].Instance->m_Ctx = nullptr;
            delete m_Live[i].Instance;
        }
        m_Live.clear();

        // Drop the scene-bus route (the host keeps the bound scene alive until here).
        if (m_BoundScene && m_BusHandle)
            m_BoundScene->Events().Disconnect(m_BusHandle);
        m_BusHandle  = 0;
        m_BoundScene = nullptr;

        if (m_Ctx)
        {
            m_Ctx->Panels.Clear();
            m_Ctx->Bus.SetProducer(m_ActiveProducer);   // restore whatever tag the host had (normally "")
        }
        m_Ctx.reset();
        m_Instantiated = false;
    }

    std::vector<std::string> ServiceHost::Names() const
    {
        std::vector<std::string> out;
        out.reserve(m_Live.size());
        for (const Live& l : m_Live)
            out.push_back(l.Desc.Name);
        return out;
    }

    const ServiceDescriptor* ServiceHost::DescriptorOf(const std::string& name) const
    {
        for (const Live& l : m_Live)
            if (l.Desc.Name == name)
                return &l.Desc;
        return nullptr;
    }
}
