#pragma once
// FakeFrameClock.h — WO-10 (2D stability): the scripted time source for the clock
// acceptance cases (N01/N02).
//
// Implements the engine's IFrameClock seam (core/IFrameClock.h) over a fixed
// schedule of absolute timestamps. Application::Run seeds its frame clock with one
// Now() call and RenderSingleFrame samples one Now() per frame; this clock answers
// those samples from the schedule, so the PRODUCTION scheduler (accumulator drain,
// 0.25 s clamp, pause, TimeScale, layer dispatch) runs over exactly the frame
// deltas the case declares — 30/60/144-Hz, irregular, a stall, or a clock origin
// of 0 / 2 h / 24 h. Nothing here schedules anything: it only says what time it is.
//
// Behaviour:
//   * before Go(): every call returns schedule[0] (the origin) — frames "hold",
//     dt = 0, nothing accumulates. The harness holds the clock until the plugin
//     under test is attached, so warm-up frames never count;
//   * after Go(): each call returns the next schedule entry; the k-th call after
//     Go() returns schedule[k] (k >= 1);
//   * past the end: the last entry, forever (dt = 0) — the driver closes the app.
// Lives under tests/ only (never in the engine, never packaged).
#include "core/IFrameClock.h"

#include <atomic>
#include <cstddef>
#include <vector>

class FakeFrameClock final : public Cosmic::IFrameClock
{
public:
    explicit FakeFrameClock(std::vector<double> schedule) : m_Schedule(std::move(schedule)) {}

    double Now() override
    {
        ++m_Calls;
        if (m_Schedule.empty()) return 0.0;
        if (!m_Go.load()) return m_Schedule[0];
        const size_t idx = m_Next.load();
        if (idx >= m_Schedule.size()) return m_Schedule.back();
        m_Next.store(idx + 1);
        return m_Schedule[idx];
    }

    // Start consuming the schedule from entry 1 (entry 0 is the held origin).
    void Go() { m_Next.store(1); m_Go.store(true); }
    bool Started() const { return m_Go.load(); }

    // Index of the LAST entry handed out (0 while holding). The driver reads this
    // in its OnUpdate to know which schedule frame the current frame is.
    size_t Cursor() const { return m_Go.load() ? m_Next.load() - 1 : 0; }
    bool   Exhausted() const { return m_Go.load() && m_Next.load() >= m_Schedule.size(); }

    double At(size_t i) const { return m_Schedule[i]; }
    size_t Size() const { return m_Schedule.size(); }
    long long Calls() const { return m_Calls.load(); }

private:
    std::vector<double>    m_Schedule;
    std::atomic<size_t>    m_Next{ 0 };
    std::atomic<bool>      m_Go{ false };
    std::atomic<long long> m_Calls{ 0 };
};
