// render_wo08_perf.cpp — WO-08 R07: the 10,000-instance performance qualification.
//
// A machine QUALIFICATION, never a correctness test: it is skipped unless the
// harness is launched with --no-skip=true, and the bar it asserts (catalog
// "basic 10,000-instance performance": p95 complete frame <= 16.67 ms, p99
// <= 33.33 ms at 1920x1080, Release, fixed camera/workload, vsync off, 10-s
// warmup + 60-s sample) only means anything for the named machine recorded in
// the evidence — see evidence/WO-08/report.md.
//
// METHOD. Each frame: clear, one PushRenderPass, ONE DrawInstancedCircles (or
// DrawInstancedQuads) of exactly 10,000 instances, PopRenderPass, then
// RenderCommand::FinishGpu() — a full GPU completion fence — before the clock
// stops. "Complete frame" therefore includes the GPU finishing the frame, and
// no pixel read-back is inside the timed region (the read-back is not the
// workload). The engine's GPU timer zone wraps the same submission so GPU-side
// time is reported alongside; zone results resolve a frame or two late, which
// is fine for a 60-s distribution. The target is an offscreen 1920x1080 RGBA8
// framebuffer, so there is no swap-chain present in the loop and vsync cannot
// throttle it (the harness window's vsync is also switched off on the record).
//
// Circles and quads are measured separately; both write a JSON block to
// $COSMIC_WO08_PERF_OUT-<workload>.json when that variable is set.

#include "wo08_common.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>

using namespace Wo08;

namespace
{
    constexpr uint32_t kInstances = 10000;
    constexpr uint32_t kTargetW = 1920, kTargetH = 1080;

    double SecondsBetween(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b)
    {
        return std::chrono::duration<double>(b - a).count();
    }

    struct Dist
    {
        size_t Count = 0;
        double Mean = 0, P50 = 0, P95 = 0, P99 = 0, Max = 0, Min = 0;
    };

    Dist Summarise(std::vector<double> v)
    {
        Dist d;
        if (v.empty()) return d;
        std::sort(v.begin(), v.end());
        d.Count = v.size();
        double sum = 0; for (double x : v) sum += x;
        d.Mean = sum / (double)v.size();
        auto pct = [&](double p) { const size_t i = std::min(v.size() - 1, (size_t)std::ceil(p * (double)v.size()) - 1); return v[std::max<size_t>(i, 0)]; };
        d.P50 = pct(0.50); d.P95 = pct(0.95); d.P99 = pct(0.99);
        d.Min = v.front(); d.Max = v.back();
        return d;
    }

    // Deterministic pseudo-random layout (LCG) so every run measures the same scene.
    struct Lcg
    {
        uint32_t s;
        explicit Lcg(uint32_t seed) : s(seed) {}
        float Next() { s = s * 1664525u + 1013904223u; return (float)(s >> 8) / 16777216.0f; }
    };

    double WarmupSeconds() { if (const char* e = std::getenv("COSMIC_WO08_PERF_WARMUP_SEC")) return std::atof(e); return 10.0; }
    double SampleSeconds() { if (const char* e = std::getenv("COSMIC_WO08_PERF_SAMPLE_SEC")) return std::atof(e); return 60.0; }

    void WriteJson(const char* workload, const Dist& complete, const Dist& submit, const Dist& gpu, size_t frames,
                   double warmupSec, double sampleSec, uint32_t drawsPerFrame, bool passed)
    {
        const char* base = std::getenv("COSMIC_WO08_PERF_OUT");
        if (!base || !base[0]) return;
        const std::string path = std::string(base) + "-" + workload + ".json";
        std::ofstream f(path);
        if (!f) return;
        const char* machine = std::getenv("COMPUTERNAME");
        f << "{\n"
          << "  \"case\": \"R07\",\n"
          << "  \"workload\": \"" << workload << "\",\n"
          << "  \"instances\": " << kInstances << ",\n"
          << "  \"target\": \"" << kTargetW << "x" << kTargetH << " RGBA8+D24S8 offscreen\",\n"
          << "  \"config\": \"" <<
#ifdef NDEBUG
             "Release"
#else
             "Debug"
#endif
          << "\",\n"
          << "  \"machine\": \"" << (machine ? machine : "unknown") << "\",\n"
          << "  \"method\": \"per frame: clear + PushRenderPass + one DrawInstanced* of 10000 + PopRenderPass + RenderCommand::FinishGpu() fence; clock stops after the fence; no read-back in the timed region; vsync moot (offscreen target, no present)\",\n"
          << "  \"warmup_sec\": " << warmupSec << ",\n"
          << "  \"sample_sec\": " << sampleSec << ",\n"
          << "  \"frames_sampled\": " << frames << ",\n"
          << "  \"instance_draw_calls_per_frame\": " << drawsPerFrame << ",\n"
          << "  \"complete_frame_ms\": { \"mean\": " << complete.Mean << ", \"p50\": " << complete.P50 << ", \"p95\": " << complete.P95
          << ", \"p99\": " << complete.P99 << ", \"max\": " << complete.Max << ", \"min\": " << complete.Min << " },\n"
          << "  \"cpu_submit_ms\": { \"mean\": " << submit.Mean << ", \"p50\": " << submit.P50 << ", \"p95\": " << submit.P95
          << ", \"p99\": " << submit.P99 << ", \"max\": " << submit.Max << " },\n"
          << "  \"gpu_zone_ms\": { \"samples\": " << gpu.Count << ", \"mean\": " << gpu.Mean << ", \"p50\": " << gpu.P50 << ", \"p95\": " << gpu.P95
          << ", \"p99\": " << gpu.P99 << ", \"max\": " << gpu.Max << " },\n"
          << "  \"bar\": { \"p95_ms\": 16.67, \"p99_ms\": 33.33 },\n"
          << "  \"passed\": " << (passed ? "true" : "false") << "\n"
          << "}\n";
    }

    template <typename Submit>
    void RunWorkload(const char* workload, Submit submit)
    {
        Ref<FrameBuffer> fbo = MakeRgba8Target(kTargetW, kTargetH);
        REQUIRE(fbo != nullptr);
        const glm::mat4 cam = PixelOrtho(kTargetW, kTargetH);

        const double warmupSec = WarmupSeconds();
        const double sampleSec = SampleSeconds();
        std::vector<double> completeMs, submitMs, gpuMs;
        completeMs.reserve(200000); submitMs.reserve(200000); gpuMs.reserve(200000);

        StatsScope stats;
        uint32_t drawsPerFrame = 0;
        bool drawsConsistent = true;

        auto frame = [&](bool record)
        {
            stats.Reset();
            const auto t0 = std::chrono::steady_clock::now();
            BeginFrame(fbo);
            RenderCommand::BeginGpuZone("wo08-r07");
            Renderer2D::PushRenderPass(cam, { 0.0f, 0.0f, (float)kTargetW, (float)kTargetH });
            submit();
            Renderer2D::PopRenderPass();
            RenderCommand::EndGpuZone();
            const auto t1 = std::chrono::steady_clock::now();
            RenderCommand::FinishGpu();
            const auto t2 = std::chrono::steady_clock::now();
            RenderCommand::GpuFrameMark();

            const Renderer2D::Statistics st = stats.Get();
            if (drawsPerFrame == 0) drawsPerFrame = st.InstanceDrawCalls;
            else if (st.InstanceDrawCalls != drawsPerFrame) drawsConsistent = false;

            if (record)
            {
                submitMs.push_back(SecondsBetween(t0, t1) * 1000.0);
                completeMs.push_back(SecondsBetween(t0, t2) * 1000.0);
                for (const GpuZoneResult& z : RenderCommand::GetGpuZoneResults())
                    if (z.Name == "wo08-r07") gpuMs.push_back((double)z.Milliseconds);
            }
        };

        const auto start = std::chrono::steady_clock::now();
        while (SecondsBetween(start, std::chrono::steady_clock::now()) < warmupSec)
            frame(false);
        const auto sampleStart = std::chrono::steady_clock::now();
        while (SecondsBetween(sampleStart, std::chrono::steady_clock::now()) < sampleSec)
            frame(true);

        // Correctness of the workload itself (kept separate from the timing): the
        // last frame really did draw 10,000 instances in one chunk, and it is not blank.
        CHECK(drawsPerFrame == 1);
        CHECK(drawsConsistent);
        CHECK(stats.Get().InstanceCount == kInstances);
        Image img;
        REQUIRE(Capture(fbo, img));
        CHECK(CountInk(img, 0, 0, kTargetW, kTargetH, kClearU8) > 10000);

        const Dist complete = Summarise(completeMs), sub = Summarise(submitMs), gpu = Summarise(gpuMs);
        const bool passed = complete.P95 <= 16.67 && complete.P99 <= 33.33;
        WriteJson(workload, complete, sub, gpu, completeMs.size(), warmupSec, sampleSec, drawsPerFrame, passed);

        MESSAGE("R07 " << std::string(workload) << ": " << completeMs.size() << " frames in " << sampleSec << " s after " << warmupSec
                << " s warmup; complete-frame ms mean " << complete.Mean << " p50 " << complete.P50 << " p95 " << complete.P95
                << " p99 " << complete.P99 << " max " << complete.Max << "; cpu-submit p95 " << sub.P95
                << "; gpu-zone samples " << gpu.Count << " p95 " << gpu.P95 << " p99 " << gpu.P99
                << "; instance draws/frame " << drawsPerFrame);
        REQUIRE(completeMs.size() >= 60);
        CHECK_MESSAGE(complete.P95 <= 16.67, std::string(workload), ": p95 complete frame ", complete.P95, " ms exceeds the 16.67 ms bar");
        CHECK_MESSAGE(complete.P99 <= 33.33, std::string(workload), ": p99 complete frame ", complete.P99, " ms exceeds the 33.33 ms bar");
    }
}

TEST_SUITE("WO-08 R07")
{
    TEST_CASE("R07 perf — 10,000 instanced circles at 1920x1080 (named-machine qualification)" * doctest::skip(true))
    {
        Lcg rng(12345u);
        std::vector<Renderer2D::InstanceCircleData> data(kInstances);
        for (auto& c : data)
        {
            const float s = 8.0f + rng.Next() * 32.0f;
            c.Position  = { rng.Next() * kTargetW, rng.Next() * kTargetH, 0.0f };
            c.Scale     = { s, s };
            c.Color     = { 0.3f + 0.7f * rng.Next(), 0.3f + 0.7f * rng.Next(), 0.3f + 0.7f * rng.Next(), 0.85f };
            c.Thickness = 0.5f + 0.5f * rng.Next();
            c.Fade      = 0.01f + 0.05f * rng.Next();
        }
        RunWorkload("circles", [&]() { Renderer2D::DrawInstancedCircles(data.data(), kInstances); });
    }

    TEST_CASE("R07 perf — 10,000 instanced quads at 1920x1080 (named-machine qualification)" * doctest::skip(true))
    {
        Lcg rng(54321u);
        std::vector<Renderer2D::InstanceQuadData> data(kInstances);
        for (auto& q : data)
        {
            const float s = 8.0f + rng.Next() * 32.0f;
            q.Position       = { rng.Next() * kTargetW, rng.Next() * kTargetH, 0.0f };
            q.Scale          = { s, s * (0.6f + 0.8f * rng.Next()) };
            q.Color          = { 0.3f + 0.7f * rng.Next(), 0.3f + 0.7f * rng.Next(), 0.3f + 0.7f * rng.Next(), 0.85f };
            q.TexCoordOffset = { 0.0f, 0.0f };
            q.TexCoordScale  = { 1.0f, 1.0f };
            q.TexIndex       = 0.0f;
            q.TilingFactor   = 1.0f;
        }
        RunWorkload("quads", [&]() { Renderer2D::DrawInstancedQuads(data.data(), kInstances); });
    }
}
