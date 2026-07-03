// test_audio.cpp
//
// A1/A2 acceptance-adjacent unit tests (docs/plans/08-audio-plan.md test note):
// the ONLY things CI may assert about audio are the no-device path and the
// Sound::Create failure policy — no audible assertions. AudioEngine must be
// headless-safe: on a CI runner/RDP session Init() may fail internally, and
// every subsequent call must be a harmless no-op.

#include "doctest.h"

#include <Cosmic.h>

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
