#pragma once

// Sound.h
// Last Modified 7/2/2026
//
// A1 (docs/plans/08-audio-plan.md): a loaded audio asset — the audio sibling of
// Texture2D. Create() decodes the file (WAV/FLAC/MP3 via the vendored miniaudio)
// into memory; AudioEngine plays lightweight copies of it, so one Sound can
// drive many overlapping voices.
//
// FAILURE POLICY (same as textures): a missing/unreadable file logs
// CS_CORE_ERROR and returns a DEGRADED SILENT object — never nullptr, never a
// crash. IsValid() reports which one you got. If the AudioEngine could not
// initialize (headless CI, RDP), every Sound is degraded and playback no-ops.
//
// VFS NOTE (same caveat as Config/LookupTable): FileSystem is header-only with
// per-DLL static state, so "project://" must be resolved in the CALLING DLL:
//     auto click = Cosmic::Sound::Create(Cosmic::FileSystem::Resolve("project://sounds/click.wav"));
// Create() also runs Resolve() internally, which is a no-op for already-resolved
// and absolute paths, and handles "engine://" / "user://" fine from any module.

#include "core/Core.h"

#include <string>
#include <vector>

namespace Cosmic
{
	class COSMIC_API Sound
	{
	public:
		// Load a sound file (VFS-resolved — see header note). Never returns
		// nullptr: failures yield a silent degraded object with IsValid()==false.
		static Ref<Sound> Create(const std::string& path);

		~Sound();

		Sound(const Sound&) = delete;
		Sound& operator=(const Sound&) = delete;

		bool               IsValid() const;
		const std::string& GetPath() const;
		float              GetDuration() const;   // seconds; 0 when degraded

		/**
		 * @brief Decode this sound's PCM into a mono float envelope for waveform
		 * preview (T2 / gap §4.4). Fills `out` with up to `maxSamples` signed
		 * peak-decimated samples in [-1, 1] spanning the WHOLE file (each output
		 * bucket holds the largest-magnitude sample in its span), so a long song
		 * still previews cheaply. Returns the number of samples written.
		 *
		 * Decodes directly from the file via a standalone decoder, so it is
		 * DEVICE-INDEPENDENT — it works in a headless/device-less session and does
		 * not disturb the live playback template. Returns 0 for a missing /
		 * unreadable file or maxSamples == 0.
		 */
		size_t             CopyPcm(std::vector<float>& out, size_t maxSamples) const;

		// Opaque implementation type — public so translation-unit-local code in
		// Audio.cpp (voice/registry bookkeeping) can name Sound::Impl*.
		struct Impl;

	private:
		friend class AudioEngine;

		explicit Sound(Scope<Impl> impl);
		Scope<Impl> m_Impl;
	};
}
