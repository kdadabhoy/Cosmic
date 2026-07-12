// MiniaudioImpl.cpp
// Last Modified 7/2/2026
//
// The ONE translation unit that compiles the vendored miniaudio implementation
// (docs/plans/08-audio-plan.md — single header, public-domain/MIT-0, WASAPI
// backend on Windows, no extra linker deps). Everything else includes
// miniaudio.h declarations only.

#ifdef _WIN32
// Keep the engine MAIN THREAD in the single-threaded COM apartment (STA).
// ma_context_init runs CoInitializeEx on the CALLING thread (Application::
// Initialize -> AudioEngine::Init -> main thread at boot), and miniaudio's
// default MA_COINIT_VALUE is COINIT_MULTITHREADED. A thread's apartment is
// first-call-wins, so that default silently made the UI thread MTA — and every
// native modal shown after boot (IFileDialog::Show in utils/FileDialog.cpp,
// e.g. the telemetry replay Browse button) deadlocked: the dialog object lives
// on a COM host-STA thread and SendMessages to its owner window, whose (main)
// thread sits blocked in the marshalled Show() call and never pumps. WASAPI is
// apartment-agnostic (the device thread CoInitializes itself), so STA is the
// right model for the thread that owns windows and shows dialogs.
// 0x2 = COINIT_APARTMENTTHREADED, 0x4 = COINIT_DISABLE_OLE1DDE (same flags as
// FileDialog's ComInit; numeric because WIN32_LEAN_AND_MEAN strips objbase.h).
#define MA_COINIT_VALUE 0x6
#endif

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
