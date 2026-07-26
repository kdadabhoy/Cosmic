// test_audio.cpp
//
// A1/A2 acceptance-adjacent unit tests (docs/plans/08-audio-plan.md test note):
// the ONLY things CI may assert about audio are the no-device path and the
// Sound::Create failure policy — no audible assertions. AudioEngine must be
// headless-safe: on a CI runner/RDP session Init() may fail internally, and
// every subsequent call must be a harmless no-op.

#include "doctest.h"

#include <Cosmic.h>

#ifdef _WIN32
// objbase.h explicitly: the engine builds with WIN32_LEAN_AND_MEAN, which strips
// it out of windows.h, and the apartment test below needs CoInitializeEx.
#include <windows.h>
#include <objbase.h>
#endif

TEST_CASE("AudioEngine: headless-safe lifecycle and no-op surface")
{
    // Init must never crash or block, even with no audio device.
    Cosmic::AudioEngine::Init();

    // Whole control surface must be callable regardless of device presence.
    Cosmic::AudioEngine::SetMasterVolume(0.0f);   // keep CI silent if a device DOES exist
    Cosmic::AudioEngine::SetGroupVolume(Cosmic::AudioGroup::Alerts, 0.5f);
    Cosmic::AudioEngine::PauseGroup(Cosmic::AudioGroup::Sfx, true);
    Cosmic::AudioEngine::PauseGroup(Cosmic::AudioGroup::Sfx, false);

    // Invalid-handle control paths are harmless.
    Cosmic::AudioEngine::Stop(Cosmic::InvalidSoundHandle);
    Cosmic::AudioEngine::Stop(12345u);
    Cosmic::AudioEngine::SetVolume(12345u, 0.5f);
    Cosmic::AudioEngine::SetPitch(12345u, 2.0f);
    CHECK_FALSE(Cosmic::AudioEngine::IsPlaying(12345u));

    // Null-sound playback is a no-op, not a crash.
    Cosmic::AudioEngine::Play(nullptr);
    CHECK(Cosmic::AudioEngine::PlayLooping(nullptr) == Cosmic::InvalidSoundHandle);

    Cosmic::AudioEngine::Shutdown();
    Cosmic::AudioEngine::Shutdown();   // double-shutdown is safe
}

TEST_CASE("Sound::Create failure policy — degraded silent object, never null")
{
    Cosmic::AudioEngine::Init();

    // Missing file: logs CS_CORE_ERROR and returns a degraded object.
    auto missing = Cosmic::Sound::Create("does/not/exist_ever.wav");
    REQUIRE(missing != nullptr);
    CHECK_FALSE(missing->IsValid());
    CHECK(missing->GetDuration() == doctest::Approx(0.0f));

    // Degraded sounds no-op through every playback path.
    Cosmic::AudioEngine::Play(missing);
    auto h = Cosmic::AudioEngine::PlayLooping(missing, 0.5f);
    CHECK(h == Cosmic::InvalidSoundHandle);

    Cosmic::AudioEngine::Shutdown();

    // Creating a sound AFTER shutdown degrades gracefully too (headless path).
    auto late = Cosmic::Sound::Create("also/missing.mp3");
    REQUIRE(late != nullptr);
    CHECK_FALSE(late->IsValid());
}

#ifdef _WIN32

// W9 (plan doc 28 §9.6) — pins the MA_COINIT_VALUE fix in
// audio/MiniaudioImpl.cpp:23. ma_context_init calls CoInitializeEx on the
// CALLING thread, and miniaudio's default is COINIT_MULTITHREADED. A thread's
// COM apartment is set by whoever gets there first and cannot be changed
// afterwards, so booting audio used to drop the main thread into the MTA — and
// every native modal opened later (IFileDialog::Show behind the telemetry replay
// Browse button, and in fact EVERY native dialog in the app) deadlocked against
// a thread that could no longer pump messages.
//
// The regression is silent from the audio side: audio keeps working perfectly.
// It only shows up as a frozen UI, which is why it survived from 7/02 until it
// was tracked down. This test is the tripwire — it asks COM what apartment the
// thread ended up in, which is exactly the question the file dialog asks.
TEST_CASE("AudioEngine::Init leaves the calling thread in the STA (file-dialog safety)")
{
    // Runs on doctest's main thread, the same role Application::Initialize plays.
    Cosmic::AudioEngine::Init();

    // RPC_E_CHANGED_MODE is COM's "this thread is already in the OTHER
    // apartment" — the precise failure the fix exists to prevent. S_OK means we
    // just entered STA, S_FALSE means we were already in it; both are correct.
    const long hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CHECK(hr != RPC_E_CHANGED_MODE);

    if (hr == S_OK || hr == S_FALSE)
        CoUninitialize();   // balance only the reference this test added

    Cosmic::AudioEngine::Shutdown();
}

#endif // _WIN32
