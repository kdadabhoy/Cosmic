// WO07ModuleFixture.cpp — L04 (2D stability): a REAL generated-style game module.
//
// Uses the shipped CS_MODULE_BEGIN/END macros exactly as a scaffolded project's
// Module.cpp does, so CreatePluginLayer registers a script and a reflected component
// under the module name and returns a real Cosmic::PlayerLayer. The host loads it as
// a runtime plugin, unloads it (TransitionToLauncher) and asserts the module's
// registry entries — whose factories/thunks are code in THIS DLL — are gone before
// FreeLibrary (KI-29), then loads it again to prove repeated host loads stay clean.
#include <Cosmic.h>

struct WO07ModComponent
{
    float Gain = 0.5f;
    int   Mode = 1;
};
CS_REGISTER_COMPONENT(WO07ModComponent)

class WO07ModScript : public Cosmic::ScriptableEntity
{
public:
    float Rate = 1.0f;
protected:
    void OnUpdate(float) override {}
};

CS_MODULE_BEGIN(WO07ModuleFixture)
    CS_SCRIPT(WO07ModScript)
        CS_FIELD(Rate)
    CS_END;
    CS_COMPONENT(WO07ModComponent)
        CS_FIELD(Gain)
        CS_FIELD(Mode)
    CS_END;
CS_MODULE_END()
CS_TEST_FIXTURE()   // UX-03: hidden from the Launcher project scan (KI-77)
