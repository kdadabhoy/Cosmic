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
#ifndef COSMIC_2D_ONLY
    // A proven skinned glTF (mirrors test_animation.cpp's fixture) with a "Lift"
    // clip — imports headlessly via cgltf, so it exercises the clip-set cache.
    std::string WriteClipGltf(const std::filesystem::path& dir)
    {
        namespace fs = std::filesystem;
        fs::create_directories(dir);
        {
            std::ofstream bin(dir / "rig.bin", std::ios::binary);
            const uint16_t idx[4]     = { 0, 1, 2, 0 };
            const float    pos[9]     = { 0,0,0,  1,0,0,  0,1,0 };
            const uint8_t  joints[12] = { 0,0,0,0,  0,0,0,0,  1,0,0,0 };
            const float    weights[12]= { 1,0,0,0,  1,0,0,0,  1,0,0,0 };
            const float    ibm[32]    = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0, 0,0,1,
                                          1,0,0,0, 0,1,0,0, 0,0,1,0, 0,-1,0,1 };
            const float    times[2]   = { 0.0f, 1.0f };
            const float    trans[6]   = { 0,1,0,  0,2,0 };
            bin.write((const char*)idx, 8);
            bin.write((const char*)pos, 36);
            bin.write((const char*)joints, 12);
            bin.write((const char*)weights, 48);
            bin.write((const char*)ibm, 128);
            bin.write((const char*)times, 8);
            bin.write((const char*)trans, 24);
        }
        {
            std::ofstream out(dir / "rig.gltf");
            out << R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0, 1]}],
  "nodes": [
    {"name": "skinned", "mesh": 0, "skin": 0},
    {"name": "root", "children": [2]},
    {"name": "tip", "translation": [0, 1, 0]}
  ],
  "skins": [{"joints": [1, 2], "inverseBindMatrices": 4}],
  "meshes": [{"primitives": [{
     "attributes": {"POSITION": 1, "JOINTS_0": 2, "WEIGHTS_0": 3},
     "indices": 0}]}],
  "animations": [{
    "name": "Lift",
    "channels": [{"sampler": 0, "target": {"node": 2, "path": "translation"}}],
    "samplers": [{"input": 5, "output": 6, "interpolation": "LINEAR"}]
  }],
  "buffers": [{"uri": "rig.bin", "byteLength": 264}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0,   "byteLength": 6},
    {"buffer": 0, "byteOffset": 8,   "byteLength": 36},
    {"buffer": 0, "byteOffset": 44,  "byteLength": 12},
    {"buffer": 0, "byteOffset": 56,  "byteLength": 48},
    {"buffer": 0, "byteOffset": 104, "byteLength": 128},
    {"buffer": 0, "byteOffset": 232, "byteLength": 8},
    {"buffer": 0, "byteOffset": 240, "byteLength": 24}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5123, "count": 3, "type": "SCALAR"},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3",
     "min": [0,0,0], "max": [1,1,0]},
    {"bufferView": 2, "componentType": 5121, "count": 3, "type": "VEC4"},
    {"bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC4"},
    {"bufferView": 4, "componentType": 5126, "count": 2, "type": "MAT4"},
    {"bufferView": 5, "componentType": 5126, "count": 2, "type": "SCALAR",
     "min": [0], "max": [1]},
    {"bufferView": 6, "componentType": 5126, "count": 2, "type": "VEC3"}
  ]
})";
        }
        return (dir / "rig.gltf").string();
    }
#endif   // COSMIC_2D_ONLY

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

#ifndef COSMIC_2D_ONLY
// W6 — the ONE cache entry this test can populate headlessly is the animation
// clip set (see the file header), and clips are a 3D asset family: the 2D engine
// ships no MeshImport, no AnimationClip and therefore no AssetLibrary clip verbs.
// Everything else Enumerate reports needs a GL context, so there is no 2D
// substitute to write here — the golden harness covers the 2D asset paths.
TEST_CASE("T2: AssetLibrary::Enumerate reports cached assets with plausible sizes")
{
    namespace fs = std::filesystem;

    // Isolate: drop any assets other suites cached so the count is deterministic
    // (headless — no GPU assets are ever in the cache, so Clear runs GL-free).
    AssetLibrary::Clear();

    size_t seen = 0;
    AssetLibrary::Enumerate([&](const Cosmic::AssetEntry&) { ++seen; });
    CHECK(seen == 0);   // empty cache → visitor never called

    const fs::path dir = fs::temp_directory_path() / "cosmic_t2_accounting";
    const std::string gltf = WriteClipGltf(dir);

    // Populate the clip-set cache (CPU-only path, no GL). Hold the returned Ref
    // so the aliased-clip owner is live during Enumerate (proves refcounting).
    auto held = AssetLibrary::GetAnimationClip(gltf + "#Lift");
    REQUIRE(held != nullptr);

    Cosmic::AssetEntry clipEntry;
    size_t clipSets = 0, total = 0;
    AssetLibrary::Enumerate([&](const Cosmic::AssetEntry& e)
    {
        ++total;
        if (e.Type == Cosmic::AssetType::AnimationClipSet) { ++clipSets; clipEntry = e; }
    });

    CHECK(total >= 1);
    REQUIRE(clipSets == 1);
    CHECK(clipEntry.Path == AssetLibrary::NormalizeKey(gltf));
    CHECK(clipEntry.CpuBytes > 0);              // keyframe data has a plausible size
    CHECK(clipEntry.Refs >= 2);                 // the cache's ref + the aliased clip Ref held above

    AssetLibrary::Clear();
    fs::remove_all(dir);
}
#endif   // COSMIC_2D_ONLY

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
