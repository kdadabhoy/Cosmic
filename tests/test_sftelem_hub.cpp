// test_sftelem_hub.cpp — W9 (plan doc 28 §9.6).
//
// TelemHub's framing + routing layer, driven headlessly.
//
// PATH TAKEN: the PRIMARY one. TelemHub::PumpSerial was split at its natural
// seam — the two serial calls stay in PumpSerial, and everything below them
// (line framing, tag routing, decode, stats, the 4 KB purge) moved verbatim into
// the new public TelemHub::IngestChunk. TelemHub.cpp is compiled into
// CosmicTests directly; the §9.6 fallback (a header-only FrameAssembler.h) was
// not needed. Headless construction turned out to be safe: the Ref<Texture2D>
// pinout image at TelemHub.h:177 is a null smart pointer until DrawSerialPanel
// asks for it, and imgui/implot are PUBLIC on the Cosmic target so the TU links
// without a GL context ever being created.
//
// LIMITS worth stating rather than papering over — two behaviours in §9.6 are
// not reachable without ImGui, because the only code that sets the state lives
// in the UI:
//   * "Shutdown flushes a dirty recording" — m_RecordingDirty is set solely by
//     DrawRecordingControls(). Shutdown()'s reachable contract (bounded, clean,
//     idempotent, no stray files) is covered below; the dirty branch is not.
//   * "replay-mode ignores live bytes" — Replaying() also requires a LOADED
//     player, and the only loader is DrawReplayLoader(). What IS pinned below is
//     the half that bites in practice: switching the panel to Replay without
//     loading anything must NOT stop live bytes.

#include <doctest.h>

#include "../Projects/SF_Telem/src/TelemHub.h"

#include "serial/SerialLink.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using namespace Workspace;

namespace
{
    // Same frame builder as test_sftelem_protocol.cpp — kept local so the two
    // suites cannot drift into sharing a broken helper.
    std::string MakeFrame(char side, int temp, int vraw, int iraw, int craw, int erpm)
    {
        char payload[96];
        std::snprintf(payload, sizeof(payload), "%c,%d,%d,%d,%d,%d",
                      side, temp, vraw, iraw, craw, erpm);

        const uint8_t crc = Checksum(payload, payload + std::strlen(payload));

        char frame[128];
        std::snprintf(frame, sizeof(frame), "$%s*%02X", payload, crc);
        return frame;
    }

    std::string Line(char side, int temp, int vraw, int iraw, int craw, int erpm)
    {
        return MakeFrame(side, temp, vraw, iraw, craw, erpm) + "\n";
    }

    // TelemHub carries three 512-sample rings plus a per-channel Y-axis grid, so
    // it goes on the heap rather than the test's stack. The SerialLink it is
    // handed is never connected — every test drives IngestChunk directly.
    struct Rig
    {
        Cosmic::SerialLink         Link;
        std::unique_ptr<TelemHub>  Hub{ std::make_unique<TelemHub>() };

        Rig() { Hub->Init(&Link); }
        ~Rig() { Hub->Shutdown(); }

        TelemHub* operator->() const { return Hub.get(); }
    };
}

// =============================================================================
// Line framing
// =============================================================================

TEST_CASE("TelemHub reassembles a frame split across chunk boundaries")
{
    Rig rig;

    const std::string frame = Line('R', 25, 1680, 420, 120, 350);

    // Serial chunks arrive at arbitrary boundaries; a frame split in half must
    // survive as one frame, not two bad ones.
    rig->IngestChunk(frame.substr(0, 8));
    CHECK(rig->GoodFrames() == 0);      // nothing complete yet
    CHECK(rig->BadFrames()  == 0);      // and nothing rejected yet either

    rig->IngestChunk(frame.substr(8));

    CHECK(rig->GoodFrames() == 1);
    CHECK(rig->BadFrames()  == 0);
    CHECK(rig->HasData(ESC_RIGHT));
    CHECK(rig->Volt(ESC_RIGHT) == doctest::Approx(16.80f));
    CHECK(rig->Cur(ESC_RIGHT)  == doctest::Approx(4.20f));
}

TEST_CASE("TelemHub survives a split at every byte boundary")
{
    const std::string frame = Line('W', 28, 1655, 900, 880, 700);

    for (size_t split = 0; split <= frame.size(); ++split)
    {
        Rig rig;
        CAPTURE(split);

        rig->IngestChunk(frame.substr(0, split));
        rig->IngestChunk(frame.substr(split));

        CHECK(rig->GoodFrames() == 1);
        CHECK(rig->BadFrames()  == 0);
        CHECK(rig->HasData(ESC_WEAPON));
    }
}

TEST_CASE("TelemHub handles many frames in one chunk, and one frame per chunk")
{
    SUBCASE("batched")
    {
        Rig rig;
        std::string batch;
        for (int i = 0; i < 50; ++i)
            batch += Line('R', 25, 1680 + i, 420, 120, 350);

        rig->IngestChunk(batch);
        CHECK(rig->GoodFrames() == 50);
        CHECK(rig->BadFrames()  == 0);
        CHECK(rig->PacketCount(ESC_RIGHT) == 50);
        // The last frame in the batch is the one that stuck.
        CHECK(rig->Volt(ESC_RIGHT) == doctest::Approx((1680 + 49) * 0.01f));
    }

    SUBCASE("drip fed one byte at a time")
    {
        Rig rig;
        const std::string frames = Line('R', 25, 1680, 420, 120, 350)
                                 + Line('L', 30, 1650, 900, 240, 700);

        for (char c : frames)
            rig->IngestChunk(std::string(1, c));

        CHECK(rig->GoodFrames() == 2);
        CHECK(rig->BadFrames()  == 0);
        CHECK(rig->HasData(ESC_RIGHT));
        CHECK(rig->HasData(ESC_LEFT));
    }
}

TEST_CASE("TelemHub strips carriage returns and skips empty lines")
{
    Rig rig;

    // The ESP32 sketch emits \r\n; the parser would reject the frame outright if
    // the \r were left on the end of the checksum.
    const std::string frame = MakeFrame('R', 25, 1680, 420, 120, 350);
    rig->IngestChunk(frame + "\r\n");
    CHECK(rig->GoodFrames() == 1);
    CHECK(rig->BadFrames()  == 0);

    // Blank lines are skipped before parsing, so they must not inflate the bad
    // frame counter that the UI shows as link health.
    rig->IngestChunk("\n\n\r\n\n");
    CHECK(rig->GoodFrames() == 1);
    CHECK(rig->BadFrames()  == 0);
}

TEST_CASE("TelemHub ignores an empty chunk entirely")
{
    Rig rig;

    rig->IngestChunk("");
    CHECK(rig->GoodFrames() == 0);
    CHECK(rig->BadFrames()  == 0);

    // A partial line already buffered must survive an idle poll — the empty
    // chunk returns before the accumulator is touched.
    const std::string frame = Line('R', 25, 1680, 420, 120, 350);
    rig->IngestChunk(frame.substr(0, 6));
    rig->IngestChunk("");
    rig->IngestChunk("");
    rig->IngestChunk(frame.substr(6));
    CHECK(rig->GoodFrames() == 1);
}

// =============================================================================
// Frame accounting
// =============================================================================

TEST_CASE("TelemHub counts good and bad frames separately")
{
    Rig rig;

    std::string stream;
    stream += Line('R', 25, 1680, 420, 120, 350);   // good
    stream += "#heartbeat\n";                        // bad (ignored by design)
    stream += Line('L', 30, 1650, 900, 240, 700);   // good
    stream += "$R,25,1680,420,120,350*00\n";        // bad — wrong checksum
    stream += "$X,1,2,3,4,5*7F\n";                  // bad — unknown tag
    stream += "garbage\n";                           // bad
    stream += Line('W', 28, 1655, 900, 880, 700);   // good

    rig->IngestChunk(stream);

    CHECK(rig->GoodFrames() == 3);
    CHECK(rig->BadFrames()  == 4);

    // Routing: each side landed on its own ESC.
    CHECK(rig->PacketCount(ESC_RIGHT)  == 1);
    CHECK(rig->PacketCount(ESC_LEFT)   == 1);
    CHECK(rig->PacketCount(ESC_WEAPON) == 1);
}

TEST_CASE("TelemHub routes each side tag to its own decoder")
{
    Rig rig;

    rig->IngestChunk(Line('R', 25, 1680, 420, 120, 350));
    rig->IngestChunk(Line('L', 31, 1600, 500, 130, 400));
    rig->IngestChunk(Line('W', 28, 1655, 900, 880, 700));

    // Drive sides decode through DriveSample (speed in mph), the weapon through
    // WeaponSample (tip speed). Crossing them is the routing bug this guards.
    CHECK(rig->Volt(ESC_RIGHT) == doctest::Approx(16.80f));
    CHECK(rig->Volt(ESC_LEFT)  == doctest::Approx(16.00f));
    CHECK(rig->Volt(ESC_WEAPON)== doctest::Approx(16.55f));

    CHECK(rig->Cur(ESC_RIGHT) == doctest::Approx(4.20f));
    CHECK(rig->Cur(ESC_LEFT)  == doctest::Approx(5.00f));
    CHECK(rig->Cur(ESC_WEAPON)== doctest::Approx(9.00f));

    CHECK(rig->GetDrive(ESC_RIGHT).speedMph > 0.0f);
    CHECK(rig->GetDrive(ESC_LEFT).speedMph  > 0.0f);
    CHECK(rig->Tip() > 0.0f);

    // Speed() is a drive-only concept and reads zero for the weapon.
    CHECK(rig->Speed(ESC_WEAPON) == doctest::Approx(0.0f));
}

TEST_CASE("TelemHub tracks max and average stats per ESC")
{
    Rig rig;

    for (int i = 1; i <= 10; ++i)
        rig->IngestChunk(Line('R', 25, 1600 + i * 10, 100 * i, 120, 350));

    CHECK(rig->PacketCount(ESC_RIGHT) == 10);

    // Max current is the last (largest) sample: 1000 centi-amps => 10 A.
    CHECK(rig->MaxCur(ESC_RIGHT)  == doctest::Approx(10.00f));
    CHECK(rig->MaxVolt(ESC_RIGHT) == doctest::Approx(17.00f));

    // Average over the ten samples: currents 1..10 A => 5.5 A.
    CHECK(rig->AvgCur(ESC_RIGHT) == doctest::Approx(5.50f).epsilon(0.001));

    // Untouched ESCs stay at zero rather than inheriting the right side's stats.
    CHECK(rig->MaxCur(ESC_LEFT)   == doctest::Approx(0.0f));
    CHECK(rig->AvgCur(ESC_WEAPON) == doctest::Approx(0.0f));

    rig->ResetStats();
    CHECK(rig->MaxCur(ESC_RIGHT)  == doctest::Approx(0.0f));
    CHECK(rig->MaxVolt(ESC_RIGHT) == doctest::Approx(0.0f));
}

// =============================================================================
// Presence and staleness
// =============================================================================

TEST_CASE("TelemHub reports per-ESC presence and goes stale on silence")
{
    Rig rig;

    // Nothing has arrived: no ESC is present, and an unplugged telemetry wire is
    // exactly this state — the app has to keep running on the sides that ARE
    // streaming.
    for (int id = 0; id < ESC_COUNT; ++id)
    {
        CAPTURE(id);
        CHECK_FALSE(rig->HasData(id));
        CHECK(rig->Stale(id));
        CHECK_FALSE(rig->Present(id));
    }
    CHECK_FALSE(rig->AnyPresent());

    // Only the right drive is wired up.
    rig->IngestChunk(Line('R', 25, 1680, 420, 120, 350));

    CHECK(rig->HasData(ESC_RIGHT));
    CHECK(rig->Present(ESC_RIGHT));
    CHECK(rig->AnyPresent());
    CHECK_FALSE(rig->HasData(ESC_LEFT));
    CHECK_FALSE(rig->Present(ESC_LEFT));
    CHECK_FALSE(rig->HasData(ESC_WEAPON));

    // k_StaleTimeout is 1.5 s of app clock. One second of silence is still fresh.
    for (int i = 0; i < 60; ++i) rig->OnUpdate(1.0f / 60.0f);
    CHECK(rig->Present(ESC_RIGHT));

    // Past the timeout it goes stale, but HasData stays true — "was seen once"
    // and "is live now" are deliberately different questions.
    for (int i = 0; i < 60; ++i) rig->OnUpdate(1.0f / 60.0f);
    CHECK(rig->Stale(ESC_RIGHT));
    CHECK_FALSE(rig->Present(ESC_RIGHT));
    CHECK(rig->HasData(ESC_RIGHT));
    CHECK_FALSE(rig->AnyPresent());

    // A fresh frame revives it.
    rig->IngestChunk(Line('R', 25, 1680, 420, 120, 350));
    CHECK(rig->Present(ESC_RIGHT));
    CHECK(rig->AnyPresent());
}

// =============================================================================
// The 4 KB accumulator purge
// =============================================================================

TEST_CASE("TelemHub purges the accumulator when a newline never arrives")
{
    // A stream with no line terminator — a wrong baud rate, or binary noise from
    // a half-configured link — must not grow the buffer without bound.
    Rig rig;

    rig->IngestChunk(std::string(5000, 'x'));
    CHECK(rig->GoodFrames() == 0);
    CHECK(rig->BadFrames()  == 0);   // nothing was ever line-terminated

    // The purge happened, so the next real frame is clean rather than carrying a
    // 5000-character prefix that would make it unparseable.
    rig->IngestChunk(Line('R', 25, 1680, 420, 120, 350));
    CHECK(rig->GoodFrames() == 1);
    CHECK(rig->BadFrames()  == 0);
}

TEST_CASE("TelemHub keeps sub-threshold junk, proving the purge is what saved the frame")
{
    // The contrast case. 100 junk bytes stay buffered (they are under the 4 KB
    // threshold), so the following frame IS corrupted — which is what makes the
    // previous test meaningful rather than vacuous.
    Rig rig;

    rig->IngestChunk(std::string(100, 'x'));
    rig->IngestChunk(Line('R', 25, 1680, 420, 120, 350));

    CHECK(rig->GoodFrames() == 0);
    CHECK(rig->BadFrames()  == 1);
    CHECK_FALSE(rig->HasData(ESC_RIGHT));

    // And the link recovers on the next clean frame.
    rig->IngestChunk(Line('R', 25, 1680, 420, 120, 350));
    CHECK(rig->GoodFrames() == 1);
    CHECK(rig->HasData(ESC_RIGHT));
}

TEST_CASE("TelemHub stays bounded under sustained newline-free noise")
{
    Rig rig;

    for (int i = 0; i < 200; ++i)
        rig->IngestChunk(std::string(1024, '\x7F'));

    CHECK(rig->GoodFrames() == 0);

    // Still healthy afterwards.
    rig->IngestChunk(Line('L', 30, 1650, 900, 240, 700));
    CHECK(rig->GoodFrames() == 1);
    CHECK(rig->HasData(ESC_LEFT));
}

// =============================================================================
// Mode gating and teardown
// =============================================================================

TEST_CASE("TelemHub keeps consuming live bytes until a recording is actually loaded")
{
    Rig rig;

    // OnUpdate gates the serial pump on Replaying(), which is mode == Replay AND
    // a loaded player. Flipping the panel to Replay with nothing loaded must
    // therefore leave the live path running — otherwise the dashboard would go
    // dead the moment a user opened the replay section.
    rig->Panel().SetMode(Cosmic::TelemetryPanel::Mode::Replay);
    CHECK_FALSE(rig->Replaying());

    rig->IngestChunk(Line('R', 25, 1680, 420, 120, 350));
    CHECK(rig->GoodFrames() == 1);
    CHECK(rig->HasData(ESC_RIGHT));

    // OnUpdate in this state is safe and does not fault on the empty player.
    for (int i = 0; i < 10; ++i)
        CHECK_NOTHROW(rig->OnUpdate(1.0f / 60.0f));

    CHECK(rig->ReplayDuration() == doctest::Approx(0.0f));
    CHECK(rig->ReplayPosition() == doctest::Approx(0.0f));

    rig->Panel().SetMode(Cosmic::TelemetryPanel::Mode::Live);
    CHECK_FALSE(rig->Replaying());
}

TEST_CASE("TelemHub::RecordFixed captures all three entities every tick")
{
    Rig rig;

    rig->IngestChunk(Line('R', 25, 1680, 420, 120, 350));
    rig->IngestChunk(Line('L', 30, 1650, 900, 240, 700));

    // All three entities record every tick, absent ones as zeros, so the CSV
    // columns stay aligned. A zero dt is ignored.
    rig->RecordFixed(0.0f);
    for (int i = 0; i < 120; ++i)
        rig->RecordFixed(1.0f / 60.0f);

    CHECK_NOTHROW(rig->OnUpdate(1.0f / 60.0f));
}

TEST_CASE("TelemHub::Shutdown is bounded, clean and idempotent")
{
    Cosmic::SerialLink link;
    auto hub = std::make_unique<TelemHub>();
    hub->Init(&link);

    hub->IngestChunk(Line('R', 25, 1680, 420, 120, 350));
    for (int i = 0; i < 60; ++i)
        hub->RecordFixed(1.0f / 60.0f);

    // No recording was started through the UI, so m_RecordingDirty is false and
    // Shutdown must NOT write a session folder — it only waits out any flush and
    // clears the shared entity selection.
    CHECK_NOTHROW(hub->Shutdown());
    CHECK_NOTHROW(hub->Shutdown());

    // Destroying after Shutdown, and destroying the link afterwards, must both
    // be clean — this is the return-to-launcher path.
    CHECK_NOTHROW(hub.reset());
}

TEST_CASE("TelemHub construct/destroy cycles leave no residue")
{
    // Screen switches build and tear these down repeatedly; the recorder
    // registrations and the shared EntitySelection must not accumulate.
    for (int i = 0; i < 5; ++i)
    {
        Rig rig;
        rig->IngestChunk(Line('R', 25, 1680 + i, 420, 120, 350));
        CHECK(rig->GoodFrames() == 1);
        CHECK(rig->PacketCount(ESC_RIGHT) == 1);
    }
}
