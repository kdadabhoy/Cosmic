// assets/AssetLibrary.h — S4.4a NormalizeKey equivalences. Purely lexical (no
// disk I/O, no GL), so this is headless-safe. The GPU cache hit/miss behavior is
// accepted via the Engine3DDemo "cache check" button, not here.

#include <doctest.h>

#include "assets/AssetLibrary.h"
#include "audio/AudioEngine.h"
#include "audio/Sound.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Cosmic::AssetLibrary;

TEST_CASE("NormalizeKey: raw and VFS spellings of the same file collapse to one key")
{
    // engine:// resolves to assets/<rest>; the raw equivalent with a ../ detour
    // must normalize to the same key.
    const std::string viaVfs = AssetLibrary::NormalizeKey("engine://models/duck.glb");
    const std::string viaRaw = AssetLibrary::NormalizeKey("assets/models/../models/duck.glb");
    CHECK(viaVfs == viaRaw);
}

TEST_CASE("NormalizeKey: collapses .. segments")
{
    const std::string a = AssetLibrary::NormalizeKey("assets/a/b/../c/file.png");
    const std::string b = AssetLibrary::NormalizeKey("assets/a/c/file.png");
    CHECK(a == b);
}

TEST_CASE("NormalizeKey: backslashes normalize to forward slashes")
{
    const std::string back = AssetLibrary::NormalizeKey("assets\\models\\duck.obj");
    const std::string fwd  = AssetLibrary::NormalizeKey("assets/models/duck.obj");
    CHECK(back == fwd);
    // generic_string() output uses forward slashes only.
    CHECK(fwd.find('\\') == std::string::npos);
}

TEST_CASE("NormalizeKey: is deterministic (idempotent on its own output)")
{
    const std::string once  = AssetLibrary::NormalizeKey("engine://textures/grid.png");
    const std::string twice = AssetLibrary::NormalizeKey(once);
    CHECK(once == twice);
}

// ============================================================================
// T2 — asset accounting & enumeration (gap §14.2). Headless: the only cache
// constructible without GL is the animation clip set (CPU-only import), so the
// Enumerate mechanism is proven over that; CopyPcm decodes independently of the
// audio device.
// ============================================================================

namespace
{

    // Append a value little-endian to a byte vector.
    template<typename T>
    void PutLE(std::vector<uint8_t>& b, T v)
    {
        for (size_t i = 0; i < sizeof(T); ++i) b.push_back((uint8_t)((v >> (8 * i)) & 0xFF));
    }

    // Write a mono 16-bit PCM WAV with `frames` samples; sample `peakFrame` is a
    // half-scale spike (16384 → 0.5 after f32 decode), the rest silence.
    std::string WriteSpikeWav(const std::filesystem::path& path, uint32_t frames,
                              uint32_t peakFrame, uint32_t sampleRate = 8000)
    {
        std::vector<uint8_t> b;
        const uint32_t dataBytes = frames * 2;
        b.insert(b.end(), { 'R','I','F','F' }); PutLE<uint32_t>(b, 36 + dataBytes);
        b.insert(b.end(), { 'W','A','V','E' });
        b.insert(b.end(), { 'f','m','t',' ' }); PutLE<uint32_t>(b, 16);
        PutLE<uint16_t>(b, 1);                 // PCM
        PutLE<uint16_t>(b, 1);                 // mono
        PutLE<uint32_t>(b, sampleRate);
        PutLE<uint32_t>(b, sampleRate * 2);    // byte rate
        PutLE<uint16_t>(b, 2);                 // block align
        PutLE<uint16_t>(b, 16);                // bits
        b.insert(b.end(), { 'd','a','t','a' }); PutLE<uint32_t>(b, dataBytes);
        for (uint32_t i = 0; i < frames; ++i)
            PutLE<int16_t>(b, i == peakFrame ? (int16_t)16384 : (int16_t)0);

        std::ofstream out(path, std::ios::binary);
        out.write((const char*)b.data(), (std::streamsize)b.size());
        out.close();
        return path.string();
    }
}

TEST_CASE("T2: Sound::CopyPcm decodes a peak-decimated mono envelope")
{
    namespace fs = std::filesystem;
    Cosmic::AudioEngine::Init();   // CopyPcm is device-independent, but keep the lifecycle honest

    const fs::path wav = fs::temp_directory_path() / "cosmic_t2_spike.wav";
    WriteSpikeWav(wav, /*frames*/ 200, /*peakFrame*/ 100);

    auto snd = Cosmic::Sound::Create(wav.string());
    REQUIRE(snd != nullptr);

    std::vector<float> pcm;
    const size_t n = snd->CopyPcm(pcm, 64);
    CHECK(n > 0);
    CHECK(n <= 64);
    CHECK(pcm.size() == n);

    // The half-scale spike must survive decimation as the envelope's peak.
    float peak = 0.0f;
    for (float s : pcm) peak = std::max(peak, std::fabs(s));
    CHECK(peak == doctest::Approx(0.5f).epsilon(0.02));

    // A missing file yields no samples (degraded, never a crash).
    auto missing = Cosmic::Sound::Create("does/not/exist_t2.wav");
    std::vector<float> none;
    CHECK(missing->CopyPcm(none, 64) == 0);

    Cosmic::AudioEngine::Shutdown();
    fs::remove_all(wav);
}
