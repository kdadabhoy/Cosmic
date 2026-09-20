#pragma once
// ScreenCommon.h — the one helper every PendulumLab screen script shares: find a
// scene entity by its Tag (the screens are authored in Starforge; scripts address
// their elements by name, never by handle).

#include <Cosmic.h>

namespace PendulumLab
{
    inline Cosmic::Entity FindByTag(Cosmic::Scene& scene, const std::string& tag)
    {
        auto& reg = scene.GetRegistry();
        for (auto e : reg.view<Cosmic::TagComponent>())
            if (reg.get<Cosmic::TagComponent>(e).Tag == tag)
                return Cosmic::Entity(e, &scene);
        return {};
    }
}
