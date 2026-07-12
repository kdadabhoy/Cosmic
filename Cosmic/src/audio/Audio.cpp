// Audio.cpp
// Last Modified 7/2/2026
//
// Implementation of Sound + AudioEngine (docs/plans/08-audio-plan.md A1/A2).
// All miniaudio types stay inside this file — the public headers expose none.

#include "audio/AudioEngine.h"
#include "audio/Sound.h"

#include "core/Log.h"
#include "utils/FileSystem.h"

#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace Cosmic
{
	// =========================================================================
	// Module state (audio device + groups + live voices + live sounds)
	// =========================================================================
	namespace
	{
		struct Voice
		{
			ma_sound    snd{};
			SoundHandle id      = InvalidSoundHandle;
			bool        looping = false;
		};

		bool        s_Initialized = false;
		ma_engine   s_Engine{};

		// Sfx / Ui / Alerts buses (Master == the engine endpoint itself).
		constexpr int kGroupCount = 3;
		ma_sound_group s_Groups[kGroupCount]{};
		bool           s_GroupInited[kGroupCount] = { false, false, false };
		bool           s_GroupPaused[kGroupCount] = { false, false, false };
		bool           s_MasterPaused = false;

		std::vector<std::unique_ptr<Voice>> s_Voices;
		SoundHandle                          s_NextHandle = 1;

		// mutex guards the containers only — miniaudio's mixer runs on its own
		// thread but we never touch these containers from it.
		std::mutex s_Mutex;

		ma_sound_group* GroupPtr(AudioGroup g)
		{
			const int i = static_cast<int>(g) - 1;   // Master (0) → nullptr
			if (i < 0 || i >= kGroupCount || !s_GroupInited[i])
				return nullptr;
			return &s_Groups[i];
		}

		// Sweep finished one-shots (lock must be held).
		void CollectFinishedVoices()
		{
			for (auto it = s_Voices.begin(); it != s_Voices.end();)
			{
				if (!(*it)->looping && ma_sound_at_end(&(*it)->snd))
				{
					ma_sound_uninit(&(*it)->snd);
					it = s_Voices.erase(it);
				}
				else
					++it;
			}
		}

		Voice* FindVoice(SoundHandle h)
		{
			for (auto& v : s_Voices)
				if (v->id == h)
					return v.get();
			return nullptr;
		}
	}

	// =========================================================================
	// Sound
	// =========================================================================

	struct Sound::Impl
	{
		ma_sound    src{};        // the decoded template voice (never started)
		bool        valid = false;
		std::string path;
		float       duration = 0.0f;
	};

	namespace
	{
		// Registry of live Sound::Impl so Shutdown can invalidate them before the
		// ma_engine (and its resource manager) is torn down.
		std::unordered_set<Sound::Impl*> s_LiveSounds;
	}

	Sound::Sound(Scope<Impl> impl) : m_Impl(std::move(impl)) {}

	Sound::~Sound()
	{
		std::lock_guard<std::mutex> lock(s_Mutex);
		s_LiveSounds.erase(m_Impl.get());
		if (m_Impl->valid && s_Initialized)
			ma_sound_uninit(&m_Impl->src);
	}

	bool               Sound::IsValid() const     { return m_Impl->valid; }
	const std::string& Sound::GetPath() const     { return m_Impl->path; }
	float              Sound::GetDuration() const { return m_Impl->duration; }

	size_t Sound::CopyPcm(std::vector<float>& out, size_t maxSamples) const
	{
		out.clear();
		if (maxSamples == 0 || m_Impl->path.empty())
			return 0;

		// Decode from the file with a standalone decoder (mono f32, source rate):
		// device-independent, so this works even when the AudioEngine has no
		// device, and it never touches the live playback template.
		ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 0);
		ma_decoder dec;
		if (ma_decoder_init_file(m_Impl->path.c_str(), &cfg, &dec) != MA_SUCCESS)
			return 0;

		float block[4096];

		ma_uint64 totalFrames = 0;
		ma_decoder_get_length_in_pcm_frames(&dec, &totalFrames);

		if (totalFrames > 0)
		{
			// Known length: peak-decimate on the fly into <= maxSamples buckets
			// (O(buckets) memory regardless of song length).
			const size_t buckets = (size_t)std::min<ma_uint64>((ma_uint64)maxSamples, totalFrames);
			out.assign(buckets, 0.0f);

			ma_uint64 frameIndex = 0;
			for (;;)
			{
				ma_uint64 got = 0;
				if (ma_decoder_read_pcm_frames(&dec, block, 4096, &got) != MA_SUCCESS || got == 0)
					break;
				for (ma_uint64 i = 0; i < got; ++i, ++frameIndex)
				{
					size_t b = (size_t)((frameIndex * buckets) / totalFrames);
					if (b >= buckets) b = buckets - 1;
					if (std::fabs(block[i]) > std::fabs(out[b]))
						out[b] = block[i];
				}
			}
		}
		else
		{
			// Unknown length (rare streaming case): accumulate up to a safety cap,
			// then peak-decimate the collected samples down to maxSamples.
			std::vector<float> all;
			const size_t cap = maxSamples * 64;
			for (;;)
			{
				ma_uint64 got = 0;
				if (ma_decoder_read_pcm_frames(&dec, block, 4096, &got) != MA_SUCCESS || got == 0)
					break;
				all.insert(all.end(), block, block + (size_t)got);
				if (all.size() >= cap)
					break;
			}
			if (all.size() <= maxSamples)
			{
				out = std::move(all);
			}
			else
			{
				const size_t buckets = maxSamples;
				out.assign(buckets, 0.0f);
				for (size_t i = 0; i < all.size(); ++i)
				{
					size_t b = (i * buckets) / all.size();
					if (b >= buckets) b = buckets - 1;
					if (std::fabs(all[i]) > std::fabs(out[b]))
						out[b] = all[i];
				}
			}
		}

		ma_decoder_uninit(&dec);
		return out.size();
	}

	Ref<Sound> Sound::Create(const std::string& path)
	{
		auto impl = CreateScope<Impl>();
		impl->path = FileSystem::Resolve(path);

		if (!s_Initialized)
		{
			// Headless / device-less session: every sound is degraded-silent.
			CS_CORE_WARN("Sound::Create('{0}'): audio engine unavailable — sound will be silent.", path);
			return Ref<Sound>(new Sound(std::move(impl)));
		}

		const ma_result r = ma_sound_init_from_file(&s_Engine, impl->path.c_str(),
			MA_SOUND_FLAG_DECODE, nullptr, nullptr, &impl->src);
		if (r != MA_SUCCESS)
		{
			// Missing/unreadable file → degraded silent object (texture policy).
			CS_CORE_ERROR("Sound::Create: failed to load '{0}' (miniaudio result {1}) — degraded silent sound.",
				impl->path, static_cast<int>(r));
			return Ref<Sound>(new Sound(std::move(impl)));
		}

		float len = 0.0f;
		if (ma_sound_get_length_in_seconds(&impl->src, &len) == MA_SUCCESS)
			impl->duration = len;
		impl->valid = true;

		{
			std::lock_guard<std::mutex> lock(s_Mutex);
			s_LiveSounds.insert(impl.get());
		}

		return Ref<Sound>(new Sound(std::move(impl)));
	}

	// =========================================================================
	// AudioEngine — lifecycle
	// =========================================================================

	void AudioEngine::Init()
	{
		if (s_Initialized)
			return;

		const ma_result r = ma_engine_init(nullptr, &s_Engine);
		if (r != MA_SUCCESS)
		{
			// Headless-safe policy (doc 08): warn and become a no-op.
			CS_CORE_WARN("AudioEngine: device init failed (miniaudio result {0}) — audio disabled for this session.",
				static_cast<int>(r));
			s_Initialized = false;
			return;
		}
		s_Initialized = true;

		for (int i = 0; i < kGroupCount; ++i)
		{
			s_GroupInited[i] = (ma_sound_group_init(&s_Engine, 0, nullptr, &s_Groups[i]) == MA_SUCCESS);
			s_GroupPaused[i] = false;
			if (!s_GroupInited[i])
				CS_CORE_WARN("AudioEngine: sound group {0} failed to init.", i);
		}

		CS_CORE_INFO("AudioEngine initialized ({0} Hz, {1} channels).",
			ma_engine_get_sample_rate(&s_Engine), ma_engine_get_channels(&s_Engine));
	}

	void AudioEngine::Shutdown()
	{
		if (!s_Initialized)
			return;

		std::lock_guard<std::mutex> lock(s_Mutex);

		// Voices first (they reference groups + sounds)...
		for (auto& v : s_Voices)
			ma_sound_uninit(&v->snd);
		s_Voices.clear();

		// ...then loaded sounds (they reference the engine's resource manager)...
		for (Sound::Impl* impl : s_LiveSounds)
		{
			if (impl->valid)
			{
				ma_sound_uninit(&impl->src);
				impl->valid = false;
			}
		}
		s_LiveSounds.clear();

		// ...then groups, then the engine itself.
		for (int i = 0; i < kGroupCount; ++i)
		{
			if (s_GroupInited[i])
			{
				ma_sound_group_uninit(&s_Groups[i]);
				s_GroupInited[i] = false;
			}
		}

		ma_engine_uninit(&s_Engine);
		s_Initialized = false;
		CS_CORE_TRACE("AudioEngine shut down.");
	}

	bool AudioEngine::IsInitialized() { return s_Initialized; }

	// =========================================================================
	// AudioEngine — playback
	// =========================================================================

	// Shared voice-start path for Play / PlayLooping (lock must be held).
	SoundHandle AudioEngine::StartVoice(const Ref<Sound>& sound, float volume, float pitch,
	                                    AudioGroup group, bool looping)
	{
			if (!s_Initialized || !sound || !sound->IsValid())
				return InvalidSoundHandle;

			CollectFinishedVoices();

			auto voice = std::make_unique<Voice>();
			const ma_result r = ma_sound_init_copy(&s_Engine, &sound->m_Impl->src, 0,
				GroupPtr(group), &voice->snd);
			if (r != MA_SUCCESS)
			{
				CS_CORE_WARN("AudioEngine: voice copy failed for '{0}' (result {1}).",
					sound->GetPath(), static_cast<int>(r));
				return InvalidSoundHandle;
			}

			ma_sound_set_volume(&voice->snd, volume);
			ma_sound_set_pitch(&voice->snd, pitch > 0.0f ? pitch : 1.0f);
			ma_sound_set_looping(&voice->snd, looping ? MA_TRUE : MA_FALSE);
			ma_sound_start(&voice->snd);

			voice->looping = looping;
			voice->id      = s_NextHandle++;

			const SoundHandle h = voice->id;
			s_Voices.push_back(std::move(voice));
			return h;
	}

	void AudioEngine::Play(const Ref<Sound>& sound, float volume, float pitch, AudioGroup group)
	{
		std::lock_guard<std::mutex> lock(s_Mutex);
		StartVoice(sound, volume, pitch, group, /*looping*/ false);
	}

	SoundHandle AudioEngine::PlayLooping(const Ref<Sound>& sound, float volume, float pitch, AudioGroup group)
	{
		std::lock_guard<std::mutex> lock(s_Mutex);
		return StartVoice(sound, volume, pitch, group, /*looping*/ true);
	}

	void AudioEngine::Stop(SoundHandle handle)
	{
		std::lock_guard<std::mutex> lock(s_Mutex);
		for (auto it = s_Voices.begin(); it != s_Voices.end(); ++it)
		{
			if ((*it)->id == handle)
			{
				ma_sound_uninit(&(*it)->snd);
				s_Voices.erase(it);
				return;
			}
		}
	}

	bool AudioEngine::IsPlaying(SoundHandle handle)
	{
		std::lock_guard<std::mutex> lock(s_Mutex);
		Voice* v = FindVoice(handle);
		return v && ma_sound_is_playing(&v->snd);
	}

	void AudioEngine::SetVolume(SoundHandle handle, float volume)
	{
		std::lock_guard<std::mutex> lock(s_Mutex);
		if (Voice* v = FindVoice(handle))
			ma_sound_set_volume(&v->snd, volume);
	}

	void AudioEngine::SetPitch(SoundHandle handle, float pitch)
	{
		std::lock_guard<std::mutex> lock(s_Mutex);
		if (Voice* v = FindVoice(handle))
			ma_sound_set_pitch(&v->snd, pitch > 0.0f ? pitch : 1.0f);
	}

	void AudioEngine::StopAll()
	{
		std::lock_guard<std::mutex> lock(s_Mutex);
		for (auto& v : s_Voices)
			ma_sound_uninit(&v->snd);
		s_Voices.clear();
	}

	// =========================================================================
	// AudioEngine — master + groups
	// =========================================================================

	void AudioEngine::SetMasterVolume(float volume)
	{
		if (s_Initialized)
			ma_engine_set_volume(&s_Engine, volume);
	}

	float AudioEngine::GetMasterVolume()
	{
		return s_Initialized ? ma_engine_get_volume(&s_Engine) : 0.0f;
	}

	void AudioEngine::SetGroupVolume(AudioGroup group, float volume)
	{
		if (!s_Initialized)
			return;
		if (group == AudioGroup::Master) { ma_engine_set_volume(&s_Engine, volume); return; }
		if (ma_sound_group* g = GroupPtr(group))
			ma_sound_group_set_volume(g, volume);
	}

	float AudioEngine::GetGroupVolume(AudioGroup group)
	{
		if (!s_Initialized)
			return 0.0f;
		if (group == AudioGroup::Master)
			return ma_engine_get_volume(&s_Engine);
		if (ma_sound_group* g = GroupPtr(group))
			return ma_sound_group_get_volume(g);
		return 0.0f;
	}

	void AudioEngine::PauseGroup(AudioGroup group, bool paused)
	{
		if (!s_Initialized)
			return;

		if (group == AudioGroup::Master)
		{
			if (paused) ma_engine_stop(&s_Engine);
			else        ma_engine_start(&s_Engine);
			s_MasterPaused = paused;
			return;
		}

		if (ma_sound_group* g = GroupPtr(group))
		{
			if (paused) ma_sound_group_stop(g);
			else        ma_sound_group_start(g);
			s_GroupPaused[static_cast<int>(group) - 1] = paused;
		}
	}

	bool AudioEngine::IsGroupPaused(AudioGroup group)
	{
		if (group == AudioGroup::Master)
			return s_MasterPaused;
		const int i = static_cast<int>(group) - 1;
		return (i >= 0 && i < kGroupCount) ? s_GroupPaused[i] : false;
	}
}
