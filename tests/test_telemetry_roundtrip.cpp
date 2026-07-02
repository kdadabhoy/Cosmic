// Telemetry round-trip: DataRecorder -> scene.bin -> DataPlayer.
// Exercises the full binary path headlessly (no GL, no window). Also covers the
// DataPlayer directory fallback (per-entity .bin files without scene.bin).

#include <string>
#include <vector>
#include <filesystem>

#include <doctest.h>

#include "telemetry/DataRecorder.h"
#include "telemetry/DataPlayer.h"
#include "telemetry/TelemetryChannel.h"

using namespace Cosmic;
namespace fs = std::filesystem;

namespace
{
    // Fresh scratch directory per run; best-effort cleanup at scope exit.
    struct ScratchDir
    {
        fs::path Path;
        explicit ScratchDir(const char* name)
        {
            Path = fs::temp_directory_path() / "cosmic_tests" / name;
            std::error_code ec;
            fs::remove_all(Path, ec);
            fs::create_directories(Path, ec);
        }
        ~ScratchDir()
        {
            std::error_code ec;
            fs::remove_all(Path, ec);
        }
    };
}

TEST_CASE("DataRecorder -> DataPlayer round-trip via scene.bin")
{
    ScratchDir scratch("roundtrip");

    DataRecorder recorder;
    const uint32_t idA = recorder.Register("EntityA", "test", { "ramp", "double_ramp" });
    const uint32_t idB = recorder.Register("EntityB", "test", { "constant" });
    recorder.ReserveCapacity(128);

    const float dt = 1.0f / 60.0f;
    const int frames = 100;
    for (int i = 0; i < frames; ++i)
    {
        recorder.Tick(dt);
        recorder.Record(idA, { (float)i, 2.0f * (float)i });
        recorder.Record(idB, { 7.0f });
    }

    CHECK(recorder.GetTotalFrameCount() == (size_t)frames);

    recorder.Flush(scratch.Path.string(), "session", 60.0f);
    recorder.WaitForFlush();

    const fs::path sessionDir = scratch.Path / "session";
    REQUIRE(fs::exists(sessionDir / "scene.bin"));

    DataPlayer player;
    REQUIRE(player.Load(sessionDir.string()));
    CHECK(player.IsLoaded());

    // Duration equals the last recorded timestamp: frames * dt.
    CHECK(player.GetDuration() == doctest::Approx(frames * dt).epsilon(0.02));

    // The ramp channel interpolates to value = 60*t - 1 at time t (frame k lands
    // at t = k/60 carrying value k-1). Sample mid-recording and check both entities.
    TelemetryFrame frame;
    REQUIRE(player.SampleAt("EntityA", 1.0f, frame));
    REQUIRE(frame.values.size() == 2);
    CHECK(frame.values[0] == doctest::Approx(59.0f).epsilon(0.02));
    CHECK(frame.values[1] == doctest::Approx(118.0f).epsilon(0.02));

    REQUIRE(player.SampleAt("EntityB", 0.5f, frame));
    REQUIRE(frame.values.size() == 1);
    CHECK(frame.values[0] == doctest::Approx(7.0f));
}

TEST_CASE("DataPlayer falls back to per-entity .bin files when scene.bin is absent")
{
    ScratchDir scratch("fallback");

    // Produce a valid scene.bin, then present it under a NON-scene.bin name in a
    // directory of its own — the fallback path must pick it up.
    DataRecorder recorder;
    const uint32_t id = recorder.Register("SoloEntity", "test", { "value" });
    recorder.ReserveCapacity(32);
    for (int i = 0; i < 30; ++i)
    {
        recorder.Tick(1.0f / 60.0f);
        recorder.Record(id, { (float)i });
    }
    recorder.Flush(scratch.Path.string(), "session", 60.0f);
    recorder.WaitForFlush();

    const fs::path legacyDir = scratch.Path / "legacy";
    fs::create_directories(legacyDir);
    fs::copy_file(scratch.Path / "session" / "scene.bin", legacyDir / "solo_entity.bin");

    DataPlayer player;
    REQUIRE(player.Load(legacyDir.string()));

    TelemetryFrame frame;
    CHECK(player.SampleAt("SoloEntity", 0.25f, frame));
}

TEST_CASE("DataPlayer::Load fails cleanly on an empty directory")
{
    ScratchDir scratch("empty");

    DataPlayer player;
    CHECK_FALSE(player.Load(scratch.Path.string()));
    CHECK_FALSE(player.IsLoaded());
}
