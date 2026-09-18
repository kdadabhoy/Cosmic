// WO07NoExport.cpp — L03 (2D stability): a project DLL that loads but exports NEITHER
// InitializePluginContexts NOR CreatePluginLayer. Application::LoadProjectDLL must reject
// it cleanly ("missing required engine export signatures", FreeLibrary, no active layer,
// no crash) and fall back to the launcher. Intentionally empty — the absence of the
// engine export signatures is the whole point.
extern "C" __declspec(dllexport) int WO07NoExportMarker = 1;
