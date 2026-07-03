// ProceduralAudio.cpp — Frontier ambience synthesis + cache (doc 10 F10).
// See ProceduralAudio.h. All DSP is here; the header only declares Ensure().

#include "common/ProceduralAudio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frontier
{
    namespace
    {
        constexpr uint32_t kSampleRate = 44100;
        constexpr float    kPi         = 3.14159265358979f;

        // One-pole low-pass state. y += alpha*(x - y).
        struct OnePole { float y = 0.0f; };
        inline float LP(OnePole& s, float x, float alpha) { s.y += alpha * (x - s.y); return s.y; }

        // Per-sample smoothing coefficient for a cutoff frequency (Hz).
        inline float AlphaForCutoff(float fc)
        {
            const float rc = 1.0f / (2.0f * kPi * std::max(fc, 1.0f));
            const float dt = 1.0f / static_cast<float>(kSampleRate);
            return dt / (rc + dt);
        }

        inline uint32_t Seconds(float s) { return static_cast<uint32_t>(s * kSampleRate); }

        // Band-pass a sample through [fLow, fHigh]: high-pass above fLow (x minus its
        // low content) then low-pass below fHigh.
        struct BandPass { OnePole low, high; float aLow, aHigh; };
        inline float BP(BandPass& b, float x)
        {
            const float lowContent = LP(b.low, x, b.aLow);
            const float hp         = x - lowContent;
            return LP(b.high, hp, b.aHigh);
        }

        // Fold the tail crossfade back into the head for a click-free loop, then trim
        // to `loopLen`. `buf` must hold loopLen + xfade samples.
        void MakeSeamless(std::vector<float>& buf, uint32_t loopLen, uint32_t xfade)
        {
            if (xfade == 0 || buf.size() < static_cast<size_t>(loopLen) + xfade)
                return;
            for (uint32_t i = 0; i < xfade; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(xfade);
                buf[i] = buf[i] * t + buf[loopLen + i] * (1.0f - t);
            }
            buf.resize(loopLen);
        }

        // Normalize the buffer to a target peak so recipes are comparably loud.
        void NormalizePeak(std::vector<float>& buf, float target)
        {
            float peak = 1e-6f;
            for (float v : buf) peak = std::max(peak, std::fabs(v));
            const float g = target / peak;
            for (float& v : buf) v = std::clamp(v * g, -1.0f, 1.0f);
        }

        std::vector<float> Synthesize(const std::string& name)
        {
            // Stable per-name seed (deterministic within a run; the WAV is cached to
            // disk after the first generation anyway).
            std::mt19937 rng(1337u + static_cast<uint32_t>(std::hash<std::string>{}(name)));
            std::uniform_real_distribution<float> white(-1.0f, 1.0f);

            if (name == "rumble")
            {
                // Brown noise low-passed ~80 Hz + a slow ±20% amplitude LFO. 8 s loop.
                const uint32_t loopLen = Seconds(8.0f);
                const uint32_t xfade   = Seconds(0.5f);
                std::vector<float> buf(loopLen + xfade);
                OnePole lp; float brown = 0.0f;
                const float a = AlphaForCutoff(80.0f);
                for (uint32_t i = 0; i < buf.size(); ++i)
                {
                    brown = (brown + 0.02f * white(rng)) / 1.02f;
                    const float body = LP(lp, brown * 8.0f, a);
                    const float lfo  = 0.8f + 0.2f * std::sin(2.0f * kPi * 0.15f * i / kSampleRate);
                    buf[i] = body * lfo;
                }
                NormalizePeak(buf, 0.85f);
                MakeSeamless(buf, loopLen, xfade);
                return buf;
            }

            if (name == "wind")
            {
                // Pink-ish noise band 200–800 Hz, slow amplitude wander. 10 s loop.
                const uint32_t loopLen = Seconds(10.0f);
                const uint32_t xfade   = Seconds(0.5f);
                std::vector<float> buf(loopLen + xfade);
                BandPass bp{ {}, {}, AlphaForCutoff(200.0f), AlphaForCutoff(800.0f) };
                for (uint32_t i = 0; i < buf.size(); ++i)
                {
                    const float band = BP(bp, white(rng));
                    const float t    = static_cast<float>(i) / kSampleRate;
                    const float wander = 0.55f + 0.45f * (0.5f + 0.5f * std::sin(2.0f * kPi * 0.08f * t))
                                                * (0.6f + 0.4f * std::sin(2.0f * kPi * 0.21f * t + 1.3f));
                    buf[i] = band * wander;
                }
                NormalizePeak(buf, 0.7f);
                MakeSeamless(buf, loopLen, xfade);
                return buf;
            }

            if (name == "water")
            {
                // White noise band 1–4 kHz with a bubbly amplitude modulation. 6 s loop.
                const uint32_t loopLen = Seconds(6.0f);
                const uint32_t xfade   = Seconds(0.5f);
                std::vector<float> buf(loopLen + xfade);
                BandPass bp{ {}, {}, AlphaForCutoff(1000.0f), AlphaForCutoff(4000.0f) };
                OnePole amLp; const float amA = AlphaForCutoff(12.0f);
                for (uint32_t i = 0; i < buf.size(); ++i)
                {
                    const float band = BP(bp, white(rng));
                    // Smoothed noise AM = "bubbling".
                    const float am = 0.5f + 0.5f * LP(amLp, white(rng), amA) * 2.0f;
                    buf[i] = band * std::clamp(am, 0.1f, 1.2f);
                }
                NormalizePeak(buf, 0.6f);
                MakeSeamless(buf, loopLen, xfade);
                return buf;
            }

            // "thunder" (default): a brown-noise burst with a 2 s exponential decay.
            {
                const uint32_t len = Seconds(2.2f);
                std::vector<float> buf(len);
                OnePole lp; float brown = 0.0f;
                const float a = AlphaForCutoff(400.0f);
                for (uint32_t i = 0; i < len; ++i)
                {
                    brown = (brown + 0.05f * white(rng)) / 1.05f;
                    const float body  = LP(lp, brown * 8.0f + white(rng) * 0.3f, a);
                    const float decay = std::exp(-3.0f * i / static_cast<float>(len));
                    buf[i] = body * decay;
                }
                NormalizePeak(buf, 0.95f);
                return buf;
            }
        }

        bool WriteWav(const std::string& path, const std::vector<float>& samples)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::create_directories(fs::path(path).parent_path(), ec);

            std::ofstream out(path, std::ios::binary);
            if (!out)
                return false;

            const uint16_t channels   = 1, bits = 16;
            const uint32_t byteRate    = kSampleRate * channels * (bits / 8);
            const uint16_t blockAlign  = channels * (bits / 8);
            const uint32_t dataSize    = static_cast<uint32_t>(samples.size()) * (bits / 8);

            auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
            auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };

            out.write("RIFF", 4); u32(36 + dataSize); out.write("WAVE", 4);
            out.write("fmt ", 4); u32(16); u16(1); u16(channels); u32(kSampleRate);
            u32(byteRate); u16(blockAlign); u16(bits);
            out.write("data", 4); u32(dataSize);

            for (float f : samples)
            {
                const int   v = static_cast<int>(std::lround(std::clamp(f, -1.0f, 1.0f) * 32767.0f));
                const int16_t s = static_cast<int16_t>(v);
                out.write(reinterpret_cast<const char*>(&s), 2);
            }
            return static_cast<bool>(out);
        }
    }

    Cosmic::Ref<Cosmic::Sound> ProceduralAudio::Ensure(const char* name)
    {
        static std::unordered_map<std::string, Cosmic::Ref<Cosmic::Sound>> s_Cache;

        const std::string key(name);
        auto it = s_Cache.find(key);
        if (it != s_Cache.end())
            return it->second;

        Cosmic::Ref<Cosmic::Sound> sound;

        // 1) User-dropped asset wins (resolve project:// in THIS DLL — VFS header note).
        const std::string projPath = Cosmic::FileSystem::Resolve("project://sounds/" + key + ".wav");
        std::error_code ec;
        if (std::filesystem::exists(projPath, ec))
        {
            sound = Cosmic::Sound::Create(projPath);
        }
        else
        {
            // 2) Synthesized cache under the writable user root.
            const std::string userPath = Cosmic::FileSystem::Resolve("user://frontier/audio/" + key + ".wav");
            if (!std::filesystem::exists(userPath, ec))
                WriteWav(userPath, Synthesize(key));
            sound = Cosmic::Sound::Create(userPath);
        }

        s_Cache.emplace(key, sound);
        return sound;
    }
}
