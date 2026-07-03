#pragma once

// AudioEngine.h
// Last Modified 7/2/2026
//
// A1+A2 (docs/plans/08-audio-plan.md): the engine's audio subsystem — a thin
// facade over one miniaudio ma_engine (vendored single header, WASAPI backend).
// The engine ships generic verbs (Play / PlayLooping / SetVolume); apps decide
// what sounds MEAN (low-battery warning, mode chime, motor hum).
//
// LIFECYCLE: Application::Initialize() calls Init() right after the JobSystem;
// Application::Shutdown() calls Shutdown() before window teardown. Client code
// never calls these.
//
// HEADLESS-SAFE: if device init fails (CI runner, RDP session, no audio
// hardware) Init() logs a warning and the whole subsystem becomes a no-op —
// never crashes, never blocks startup (doc 08 test note).
//
// A1 — one-shots:      AudioEngine::Play(sound);  Play(sound, vol, pitch);
// A2 — loops/handles:  SoundHandle h = AudioEngine::PlayLooping(hum, 0.5f);
//                      AudioEngine::SetPitch(h, rpmRatio);  AudioEngine::Stop(h);
// A2 — groups/buses:   Master / SFX / UI / Alerts with per-group volume and an
//                      opt-in pause verb (sounds keep playing when TimeScale==0
//                      by default; an app pauses a group explicitly).

#include "core/Core.h"

#include <cstdint>

namespace Cosmic
{
	class Sound;

	// Handle to a live voice started by PlayLooping (or any controllable play).
	// 0 is the invalid handle; handles are never reused within a session.
	using SoundHandle = uint32_t;
	constexpr SoundHandle InvalidSoundHandle = 0;

	// Mixing buses. Master is the engine endpoint; the rest are ma_sound_groups
	// routed through it.
	enum class AudioGroup : int
	{
		Master = 0,
		Sfx,
		Ui,
		Alerts,
	};

	class COSMIC_API AudioEngine
	{
	public:
		// SYSTEM LIFECYCLE — called by Application; see header note.
		static void Init();
		static void Shutdown();

		// False when audio is unavailable (headless) — everything else no-ops.
		static bool IsInitialized();

		////////////////////////////////
		// A1 — one-shot playback
		///////////////////////////////

		// Fire-and-forget. Degraded/invalid sounds no-op silently (already logged
		// at Create time). pitch is a playback-rate multiplier (1 = as recorded).
		static void Play(const Ref<Sound>& sound, float volume = 1.0f, float pitch = 1.0f,
		                 AudioGroup group = AudioGroup::Sfx);

		static void  SetMasterVolume(float volume);   // [0, 1+]
		static float GetMasterVolume();

		////////////////////////////////
		// A2 — loops, handles, live control
		///////////////////////////////

		// Start a looping voice and return a handle for live control (the
		// RPM-tracking motor-loop API). InvalidSoundHandle when unavailable.
		static SoundHandle PlayLooping(const Ref<Sound>& sound, float volume = 1.0f,
		                               float pitch = 1.0f, AudioGroup group = AudioGroup::Sfx);

		static void Stop(SoundHandle handle);
		static bool IsPlaying(SoundHandle handle);
		static void SetVolume(SoundHandle handle, float volume);
		static void SetPitch(SoundHandle handle, float pitch);

		// Stop every live voice (loops included). Loaded Sounds stay valid.
		static void StopAll();

		////////////////////////////////
		// A2 — groups / buses
		///////////////////////////////

		static void  SetGroupVolume(AudioGroup group, float volume);
		static float GetGroupVolume(AudioGroup group);

		// Opt-in pause policy: pausing a group halts its voices (they resume from
		// where they stopped). Master pauses the whole device graph.
		static void PauseGroup(AudioGroup group, bool paused);
		static bool IsGroupPaused(AudioGroup group);

	private:
		// Shared voice-start path for Play/PlayLooping (defined in Audio.cpp;
		// a member so it can reach Sound's internals through the friendship).
		static SoundHandle StartVoice(const Ref<Sound>& sound, float volume, float pitch,
		                              AudioGroup group, bool looping);
	};
}
