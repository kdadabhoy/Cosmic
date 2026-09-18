// test_wo10_n04_determinism.cpp — N04 (2D stability, WO-10): the canonical PCG32
// vector, 1,000,000 outputs from the same seed/stream, and exact same-build
// reproducibility.
//
//   * the canonical pcg32 demo vector (seed 42, stream 54) — the same six words
//     test_random.cpp pins, re-asserted under the N04 name;
//   * 1,000,000 outputs: two generators seeded identically agree word for word,
//     and the FNV-1a-64 of the little-endian stream equals the value computed by
//     an INDEPENDENT Python PCG32 (tests/fixtures/wo10/pcg32_oracle.py) — the
//     oracle is not the code under test;
//   * same-build reproducibility: the case writes the 1,000,000-word stream, a
//     100,000-sample seeded Gaussian stream and a 100,000-sample transcendental
//     stream (sin / exp / log of seeded inputs) as binary files into
//     COSMIC_WO10_EVIDENCE_DIR (or the CWD); the runner wrapper launches this case
//     TWICE, in two processes, and byte-compares the files. The Gaussian and
//     transcendental streams are ALSO pinned by hash — under the declared
//     same-toolchain scope (MSVC 19.5x x64, the vendored <cmath>): a different
//     toolchain may legitimately change them, a different run of this build may not.
//   * filters / lookup tables / the deterministic 2D scene state keep their
//     retained suites (Filters (E12), LookupTable (E13), 2D scene determinism (W2))
//     — the N04 runner case runs those suites explicitly next to this one.
// Never a cross-GPU image or MP4 bit-equality requirement.
#include <doctest.h>

#include "math/Random.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    uint64_t Fnv1a64(const void* data, size_t bytes, uint64_t h = 0xcbf29ce484222325ULL)
    {
        const uint8_t* p = (const uint8_t*)data;
        for (size_t i = 0; i < bytes; ++i) { h ^= p[i]; h *= 0x100000001b3ULL; }
        return h;
    }
    std::string OutDir()
    {
        char* v = nullptr; size_t n = 0; _dupenv_s(&v, &n, "COSMIC_WO10_EVIDENCE_DIR");
        std::string s = v ? v : ""; if (v) free(v);
        if (s.empty()) s = std::filesystem::current_path().string();
        return s;
    }
    bool WriteBinary(const std::filesystem::path& p, const void* data, size_t bytes)
    {
        std::error_code ec; std::filesystem::create_directories(p.parent_path(), ec);
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f.write((const char*)data, (std::streamsize)bytes);
        return (bool)f;
    }
    std::string Hex(uint64_t v) { char b[32]; std::snprintf(b, sizeof(b), "0x%016llx", (unsigned long long)v); return b; }
}

TEST_SUITE("WO-10 N04 determinism")
{
    TEST_CASE("WO-10 N04: canonical PCG32 vector (seed 42, stream 54)")
    {
        Cosmic::Random rng(42, 54);
        const uint32_t expected[] = { 0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu };
        for (uint32_t e : expected) CHECK(rng.NextUInt32() == e);
    }

    TEST_CASE("WO-10 N04: 1,000,000 outputs from the same seed/stream agree and hash to the independent oracle")
    {
        const int n = 1000000;
        Cosmic::Random a(42, 54), b(42, 54);
        std::vector<uint32_t> stream((size_t)n);
        int mismatches = 0;
        for (int i = 0; i < n; ++i)
        {
            stream[(size_t)i] = a.NextUInt32();
            if (b.NextUInt32() != stream[(size_t)i]) ++mismatches;
        }
        CHECK(mismatches == 0);
        const uint64_t h = Fnv1a64(stream.data(), stream.size() * sizeof(uint32_t));
        // tests/fixtures/wo10/pcg32_oracle.py (an independent PCG32 in Python) — 2026-09-18:
        //   fnv1a64 of 1,000,000 outputs (seed 42, stream 54): 0x3654ce49c351b391, last 0xef1e2afa
        CHECK(h == 0x3654ce49c351b391ULL);
        CHECK(stream.back() == 0xef1e2afau);
        MESSAGE((std::string("PCG32 1,000,000 outputs: FNV-1a-64 ") + Hex(h) + " (oracle 0x3654ce49c351b391), last word 0x" + Hex(stream.back()).substr(10)));

        // Same-build reproducibility artefacts (the wrapper runs this case twice and byte-compares).
        const std::filesystem::path out = std::filesystem::path(OutDir());
        CHECK(WriteBinary(out / "n04-pcg32-1e6.bin", stream.data(), stream.size() * sizeof(uint32_t)));
    }

    TEST_CASE("WO-10 N04: seeded Gaussian and transcendental streams — same-toolchain reproducibility")
    {
        const int n = 100000;
        Cosmic::Random g(2026, 9);
        std::vector<float> gauss((size_t)n), trans((size_t)n);
        Cosmic::Random u(0x0A10, 3);
        for (int i = 0; i < n; ++i)
        {
            gauss[(size_t)i] = g.Gaussian(0.0f, 1.0f);
            const float x = u.Range(0.01f, 10.0f);
            trans[(size_t)i] = std::sin(x) + std::exp(-x) + std::log(x);
        }
        for (float v : gauss) REQUIRE(std::isfinite(v));
        for (float v : trans) REQUIRE(std::isfinite(v));
        const uint64_t hg = Fnv1a64(gauss.data(), gauss.size() * sizeof(float));
        const uint64_t ht = Fnv1a64(trans.data(), trans.size() * sizeof(float));
        MESSAGE((std::string("Gaussian(2026,9) x100,000 FNV-1a-64 ") + Hex(hg) + "; transcendental x100,000 " + Hex(ht)
            + " — same-toolchain scope (MSVC x64, vendored <cmath>); a change here means the toolchain changed, not the seed"));
        // Pinned on the reference toolchain (MSVC 19.51 x64, VS18 2026, 2026-09-18; identical in
        // Debug and Release on that toolchain). Declared scope: same toolchain/build.
        CHECK(hg == 0xea0fddffaacaf6b4ULL);
        CHECK(ht == 0x9540463af85ebf45ULL);
        // Two generators, same seed: identical Gaussian sequences (the spare-caching path included).
        Cosmic::Random g2(2026, 9); int mism = 0;
        for (int i = 0; i < n; ++i) if (g2.Gaussian(0.0f, 1.0f) != gauss[(size_t)i]) ++mism;
        CHECK(mism == 0);
        const std::filesystem::path out = std::filesystem::path(OutDir());
        CHECK(WriteBinary(out / "n04-gaussian-1e5.bin", gauss.data(), gauss.size() * sizeof(float)));
        CHECK(WriteBinary(out / "n04-transcendental-1e5.bin", trans.data(), trans.size() * sizeof(float)));
        std::ofstream hashes(out / "n04-hashes.txt", std::ios::trunc);
        hashes << "pcg32-1e6 see n04-pcg32-1e6.bin\ngaussian-1e5 " << Hex(hg) << "\ntranscendental-1e5 " << Hex(ht) << "\n";
    }
}
