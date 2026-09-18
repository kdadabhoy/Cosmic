#pragma once
// AnalysisFixtures.h — the two synthetic fixtures the analysis sample consumes
// (2D stability catalog, WO-10 / X01; D-9km: synthetic only).
//
//   F-TRAJECTORY  — a 10-s ballistic path in SI units, t = i/120 for i = 0..1200
//                   (1,201 samples): x = 30 t, y = 50 t - 0.5 * 9.80665 * t^2,
//                   vx = 30, vy = 50 - 9.80665 t. Every value is DOUBLE; this is
//                   the scientific reference the float telemetry/plot path is
//                   compared against (never the other way round).
//   F-SERIES-LARGE — 100,000 ordered samples per channel at 1 kHz (t = i/1000),
//                   8 finite channels + time, generated from the equations and the
//                   seed recorded in its metadata; known extrema and discontinuity
//                   markers travel with it. Generated, never committed as a blob.
//
// Header-only and engine-light (math/Random.h for the seeded channels, DataExport
// for the CSV forms) so the sample DLL, the SDK's tests (N03/X01) and the
// out-of-process oracle can all evaluate the same definitions. Scientific source
// values stay double throughout (contracts.md §5); conversion to float happens
// only at the display / telemetry boundary, in the sample.
#include "math/Random.h"
#include "utils/DataExport.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace AnalysisSample
{
    // ------------------------------------------------------------------------
    // F-TRAJECTORY
    // ------------------------------------------------------------------------
    namespace Trajectory
    {
        constexpr int    kSamples = 1201;
        constexpr double kRateHz  = 120.0;
        constexpr double kG       = 9.80665;   // m/s^2 (SI standard gravity)
        constexpr double kVx      = 30.0;      // m/s
        constexpr double kVy0     = 50.0;      // m/s

        struct Row { double t, x, y, vx, vy; };

        // The equations, evaluated in double at sample i.
        inline Row At(int i)
        {
            const double t = (double)i / kRateHz;
            return { t, kVx * t, kVy0 * t - 0.5 * kG * t * t, kVx, kVy0 - kG * t };
        }

        // The equations at an arbitrary time (the double reference for a midpoint scrub).
        inline Row AtTime(double t)
        {
            return { t, kVx * t, kVy0 * t - 0.5 * kG * t * t, kVx, kVy0 - kG * t };
        }

        inline std::vector<Row> All()
        {
            std::vector<Row> rows; rows.reserve(kSamples);
            for (int i = 0; i < kSamples; ++i) rows.push_back(At(i));
            return rows;
        }

        // CSV forms through the engine's restricted-numeric writer/reader (double columns).
        inline bool WriteCsv(const std::string& path)
        {
            std::vector<std::vector<double>> cols(5);
            for (int i = 0; i < kSamples; ++i)
            {
                const Row r = At(i);
                cols[0].push_back(r.t); cols[1].push_back(r.x); cols[2].push_back(r.y);
                cols[3].push_back(r.vx); cols[4].push_back(r.vy);
            }
            return Cosmic::DataExport::WriteCSV(path, { "t", "x", "y", "vx", "vy" }, cols);
        }

        inline bool LoadCsv(const std::string& path, std::vector<Row>& out)
        {
            std::vector<std::vector<double>> cols;
            std::vector<std::string> headers;
            if (!Cosmic::DataExport::LoadCSV(path, cols, &headers)) return false;
            if (cols.size() != 5) return false;
            const size_t n = cols[0].size();
            for (size_t c = 1; c < 5; ++c) if (cols[c].size() != n) return false;
            out.clear(); out.reserve(n);
            for (size_t i = 0; i < n; ++i) out.push_back({ cols[0][i], cols[1][i], cols[2][i], cols[3][i], cols[4][i] });
            return true;
        }
    }

    // ------------------------------------------------------------------------
    // F-SERIES-LARGE
    // ------------------------------------------------------------------------
    namespace SeriesLarge
    {
        constexpr int      kSamples  = 100000;
        constexpr int      kChannels = 8;
        constexpr double   kRateHz   = 1000.0;
        constexpr uint64_t kSeed     = 0x0A105E21ULL;   // PCG32 seed for the two noise channels
        constexpr uint64_t kStreamUniform  = 7;
        constexpr uint64_t kStreamGaussian = 11;

        struct Extremum { double value; int index; };
        struct ChannelMeta
        {
            std::string name;
            std::string equation;     // human-readable definition (also what the oracle re-evaluates)
            Extremum    min, max;     // over the generated samples
            std::vector<int> discontinuities;   // sample indices where the value jumps
            bool        seeded = false;          // true: reproducible only via the seed (not an analytic curve)
        };
        struct Meta
        {
            int samples = kSamples, channels = kChannels;
            double rateHz = kRateHz;
            uint64_t seed = kSeed;
            std::vector<ChannelMeta> channel;
        };

        inline double Time(int i) { return (double)i / kRateHz; }

        // Deterministic channel definitions. Index 0..7.
        inline const char* Name(int ch)
        {
            static const char* names[kChannels] = { "sin_1hz", "cos_1hz", "ramp", "sawtooth", "step", "damped", "noise_uniform", "noise_gaussian" };
            return names[ch];
        }
        inline const char* Equation(int ch)
        {
            static const char* eq[kChannels] = {
                "sin(2*pi*t)", "cos(2*pi*t)", "t", "t - floor(t)", "t < 50 ? -1 : 1",
                "exp(-t/20) * cos(2*pi*0.5*t)",
                "-1 + 2*u, u = PCG32(seed, stream 7).NextFloat()  [(u32 >> 8) * 2^-24]",
                "PCG32(seed, stream 11).Gaussian(0, 1)  [Box-Muller, float log/sin/cos — same-toolchain scope]"
            };
            return eq[ch];
        }

        // Generate every channel (double) + time, and the metadata. The two seeded
        // channels use the engine's PCG32 (math/Random.h) — the same generator the
        // N04 canonical vector pins.
        inline void Generate(std::vector<double>& time, std::vector<std::vector<double>>& channels, Meta& meta)
        {
            const double twoPi = 6.283185307179586476925286766559;
            time.assign(kSamples, 0.0);
            channels.assign(kChannels, std::vector<double>(kSamples, 0.0));
            Cosmic::Random uni(kSeed, kStreamUniform);
            Cosmic::Random gau(kSeed, kStreamGaussian);
            for (int i = 0; i < kSamples; ++i)
            {
                const double t = Time(i);
                time[i] = t;
                channels[0][i] = std::sin(twoPi * t);
                channels[1][i] = std::cos(twoPi * t);
                channels[2][i] = t;
                channels[3][i] = t - std::floor(t);
                channels[4][i] = t < 50.0 ? -1.0 : 1.0;
                channels[5][i] = std::exp(-t / 20.0) * std::cos(twoPi * 0.5 * t);
                channels[6][i] = -1.0 + 2.0 * (double)uni.NextFloat();
                channels[7][i] = (double)gau.Gaussian(0.0f, 1.0f);
            }
            meta = Meta{};
            meta.channel.resize(kChannels);
            for (int c = 0; c < kChannels; ++c)
            {
                ChannelMeta& m = meta.channel[c];
                m.name = Name(c); m.equation = Equation(c); m.seeded = (c >= 6);
                m.min = { channels[c][0], 0 }; m.max = { channels[c][0], 0 };
                for (int i = 1; i < kSamples; ++i)
                {
                    const double v = channels[c][i];
                    if (v < m.min.value) m.min = { v, i };
                    if (v > m.max.value) m.max = { v, i };
                }
            }
            // Discontinuity markers (by definition, not by scanning): the sawtooth
            // wraps at every whole second after 0; the step flips at t = 50.
            for (int i = 1000; i < kSamples; i += 1000) meta.channel[3].discontinuities.push_back(i);
            meta.channel[4].discontinuities.push_back(50000);
        }

        // Every value finite? (the fixture's own contract: "8 finite numeric channels")
        inline bool AllFinite(const std::vector<std::vector<double>>& channels)
        {
            for (const auto& ch : channels) for (double v : ch) if (!std::isfinite(v)) return false;
            return true;
        }

        // The metadata as JSON (equations, seed, extrema, discontinuities) — what the
        // sample writes next to its exports and the out-of-process oracle reads.
        inline std::string MetaJson(const Meta& m)
        {
            auto esc = [](const std::string& s) { std::string o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o; };
            char buf[128];
            std::string j = "{\n";
            std::snprintf(buf, sizeof(buf), "  \"samples\": %d,\n  \"channels\": %d,\n  \"rate_hz\": %.1f,\n", m.samples, m.channels, m.rateHz); j += buf;
            std::snprintf(buf, sizeof(buf), "  \"seed\": \"0x%llX\",\n  \"time\": \"t = i / rate_hz, i = 0..samples-1\",\n", (unsigned long long)m.seed); j += buf;
            j += "  \"channel\": [\n";
            for (size_t c = 0; c < m.channel.size(); ++c)
            {
                const ChannelMeta& ch = m.channel[c];
                j += "    { \"name\": \"" + esc(ch.name) + "\", \"equation\": \"" + esc(ch.equation) + "\", \"seeded\": " + (ch.seeded ? "true" : "false");
                std::snprintf(buf, sizeof(buf), ", \"min\": { \"value\": %.17g, \"index\": %d }, \"max\": { \"value\": %.17g, \"index\": %d }", ch.min.value, ch.min.index, ch.max.value, ch.max.index); j += buf;
                j += ", \"discontinuities\": [";
                for (size_t d = 0; d < ch.discontinuities.size(); ++d) { j += std::to_string(ch.discontinuities[d]); if (d + 1 < ch.discontinuities.size()) j += ", "; }
                j += "] }";
                j += (c + 1 < m.channel.size()) ? ",\n" : "\n";
            }
            j += "  ]\n}\n";
            return j;
        }
    }

    // ------------------------------------------------------------------------
    // Local display frame: scientific values live in double with a large world
    // origin; the float renderer only ever sees the DOUBLE difference.
    // ------------------------------------------------------------------------
    struct LocalFrame
    {
        double originX = 0.0, originY = 0.0;
        float ToLocalX(double worldX) const { return (float)(worldX - originX); }
        float ToLocalY(double worldY) const { return (float)(worldY - originY); }
    };
}
