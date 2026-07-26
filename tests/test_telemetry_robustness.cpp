// test_telemetry_robustness.cpp — W9 (plan doc 28 §9.6).
//
// The hostile-input net around the telemetry stack SF_Telem depends on. Two
// confirmed crash bugs lived in DataPlayer::LoadBinaryFile until W9:
//
//   1. entityCount / channel_count / sample_count were used to size vectors with
//      no validation at all. A corrupt 0xFFFFFFFF asked for hundreds of GB, and
//      because there is no try/catch anywhere on that path the resulting
//      bad_alloc was an uncaught exception — i.e. std::terminate, not an error
//      return. "Absurd counts" below is the direct regression test.
//   2. The end-of-read check was `!good() && !eof()`. A short read sets failbit
//      AND eofbit, so the predicate was false exactly when the file was
//      truncated, and Load returned SUCCESS with the missing frames silently
//      zero-filled. "Truncation sweep" below is the direct regression test.
//
// test_telemetry_roundtrip.cpp is the other half of the net: it proves VALID
// files still load identically. Nothing here may weaken that.
//
// Headless: no GL, no window. Everything runs against scratch files under the
// system temp directory.

#include <doctest.h>

#include "telemetry/DataRecorder.h"
#include "telemetry/DataPlayer.h"
#include "telemetry/TelemetryChannel.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace Cosmic;
namespace fs = std::filesystem;

namespace
{
    // Fresh scratch directory per run; best-effort cleanup at scope exit.
    // (Mirrors test_telemetry_roundtrip.cpp so both suites behave the same.)
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

    // ---------------------------------------------------------------------
    // Raw byte helpers — these tests craft malformed files deliberately, so
    // they write bytes rather than going through DataRecorder.
    // ---------------------------------------------------------------------

    void WriteBytes(const fs::path& path, const std::vector<char>& bytes)
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        REQUIRE(f.is_open());
        if (!bytes.empty())
            f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    std::vector<char> ReadBytes(const fs::path& path)
    {
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        REQUIRE(f.is_open());
        const std::streamoff size = f.tellg();
        f.seekg(0, std::ios::beg);
        std::vector<char> bytes(static_cast<size_t>(size));
        if (size > 0)
            f.read(bytes.data(), size);
        return bytes;
    }

    template <typename T>
    void Append(std::vector<char>& out, const T& value)
    {
        const char* p = reinterpret_cast<const char*>(&value);
        out.insert(out.end(), p, p + sizeof(T));
    }

    void AppendFixed(std::vector<char>& out, const std::string& text, size_t width)
    {
        std::vector<char> field(width, '\0');
        const size_t n = std::min(text.size(), width - 1);
        std::memcpy(field.data(), text.data(), n);
        out.insert(out.end(), field.begin(), field.end());
    }

    // A v1 header with caller-chosen counts, so each count can be made absurd in
    // isolation. Layout is DataRecorder.h's documented format:
    //   "CSMC" | version | entity_count | sample_rate
    //   per entity: name[64] tag[64] channel_count sample_count names[32*n]
    //   per entity: sample_count * (channel_count + 1) floats
    std::vector<char> MakeV1Header(uint32_t entityCount, uint32_t version = 1u)
    {
        std::vector<char> bytes;
        bytes.insert(bytes.end(), { 'C', 'S', 'M', 'C' });
        Append(bytes, version);
        Append(bytes, entityCount);
        Append(bytes, 60.0f);
        return bytes;
    }

    void AppendEntityDescriptor(std::vector<char>& bytes, const std::string& name,
                                uint32_t chCount, uint32_t sampleCount,
                                uint32_t namesToWrite)
    {
        AppendFixed(bytes, name, 64);
        AppendFixed(bytes, "test", 64);
        Append(bytes, chCount);
        Append(bytes, sampleCount);
        for (uint32_t c = 0; c < namesToWrite; ++c)
            AppendFixed(bytes, "ch" + std::to_string(c), 32);
    }

    // Produce a genuine recording through the real writer, then hand back the
    // bytes of scene.bin. The truncation sweep works on this so it is testing the
    // actual on-disk format, not a hand-rolled approximation of it.
    std::vector<char> MakeRealRecordingBytes(const fs::path& dir, int frames = 40)
    {
        DataRecorder recorder;
        const uint32_t idA = recorder.Register("EntityA", "test", { "a", "b", "c" });
        const uint32_t idB = recorder.Register("EntityB", "test", { "solo" });
        recorder.ReserveCapacity(static_cast<size_t>(frames) + 8);

        for (int i = 0; i < frames; ++i)
        {
            recorder.Tick(1.0f / 60.0f);
            recorder.Record(idA, std::vector<float>{ (float)i, (float)i * 2.0f, (float)i * 3.0f });
            recorder.Record(idB, std::vector<float>{ 7.0f });
        }

        recorder.Flush(dir.string(), "real", 60.0f);
        recorder.WaitForFlush();

        const fs::path bin = dir / "real" / "scene.bin";
        REQUIRE(fs::exists(bin));
        return ReadBytes(bin);
    }
}

// =============================================================================
// DataPlayer — malformed headers
// =============================================================================

TEST_CASE("DataPlayer rejects an empty file")
{
    ScratchDir scratch("robust_empty");
    const fs::path path = scratch.Path / "empty.bin";
    WriteBytes(path, {});

    DataPlayer player;
    CHECK_FALSE(player.Load(path.string()));
    CHECK_FALSE(player.IsLoaded());
}

TEST_CASE("DataPlayer rejects a file with bad magic")
{
    ScratchDir scratch("robust_magic");

    SUBCASE("wrong four bytes")
    {
        const fs::path path = scratch.Path / "bad.bin";
        std::vector<char> bytes = { 'N', 'O', 'P', 'E' };
        Append(bytes, uint32_t(1));
        WriteBytes(path, bytes);

        DataPlayer player;
        CHECK_FALSE(player.Load(path.string()));
    }

    SUBCASE("shorter than the magic itself")
    {
        const fs::path path = scratch.Path / "stub.bin";
        WriteBytes(path, { 'C', 'S' });

        DataPlayer player;
        CHECK_FALSE(player.Load(path.string()));
    }
}

TEST_CASE("DataPlayer rejects unknown versions")
{
    ScratchDir scratch("robust_version");

    // Only a v1 path exists. v0, v2 and a garbage version must all be refused —
    // never silently treated as v1.
    for (uint32_t version : { 0u, 2u, 3u, 0xFFFFFFFFu })
    {
        std::vector<char> bytes = MakeV1Header(1u, version);
        AppendEntityDescriptor(bytes, "E", 1u, 1u, 1u);
        Append(bytes, 0.0f);
        Append(bytes, 1.0f);

        const fs::path path = scratch.Path / ("v" + std::to_string(version) + ".bin");
        WriteBytes(path, bytes);

        DataPlayer player;
        CAPTURE(version);
        CHECK_FALSE(player.Load(path.string()));
    }
}

// =============================================================================
// DataPlayer — bug 1: unvalidated counts
// =============================================================================

TEST_CASE("DataPlayer rejects absurd counts instead of attempting the allocation")
{
    ScratchDir scratch("robust_counts");

    // Every one of these used to reach a std::vector constructor with the raw
    // value. 0xFFFFFFFF entities is ~400 GB of PlayerEntityData; there is no
    // try/catch on the path, so the bad_alloc terminated the process. The load
    // must now fail fast — and, critically, WITHOUT allocating, which the wall
    // clock below is what actually proves.
    const auto start = std::chrono::steady_clock::now();

    SUBCASE("absurd entity count")
    {
        std::vector<char> bytes = MakeV1Header(0xFFFFFFFFu);
        const fs::path path = scratch.Path / "entities.bin";
        WriteBytes(path, bytes);

        DataPlayer player;
        CHECK_NOTHROW(CHECK_FALSE(player.Load(path.string())));
    }

    SUBCASE("plausible entity count that still exceeds the file")
    {
        // 4096 is exactly the hard cap, so this one has to be caught by the
        // remaining-bytes check rather than the cap.
        std::vector<char> bytes = MakeV1Header(4096u);
        const fs::path path = scratch.Path / "entities_capped.bin";
        WriteBytes(path, bytes);

        DataPlayer player;
        CHECK_NOTHROW(CHECK_FALSE(player.Load(path.string())));
    }

    SUBCASE("absurd channel count")
    {
        std::vector<char> bytes = MakeV1Header(1u);
        AppendEntityDescriptor(bytes, "E", 0xFFFFFFFFu, 1u, 0u);
        const fs::path path = scratch.Path / "channels.bin";
        WriteBytes(path, bytes);

        DataPlayer player;
        CHECK_NOTHROW(CHECK_FALSE(player.Load(path.string())));
    }

    SUBCASE("absurd sample count")
    {
        std::vector<char> bytes = MakeV1Header(1u);
        AppendEntityDescriptor(bytes, "E", 2u, 0xFFFFFFFFu, 2u);
        const fs::path path = scratch.Path / "samples.bin";
        WriteBytes(path, bytes);

        DataPlayer player;
        CHECK_NOTHROW(CHECK_FALSE(player.Load(path.string())));
    }

    SUBCASE("many entities whose sample counts only overflow in aggregate")
    {
        // No single count is absurd here — 64 entities x 2 channels x 200k
        // samples. Only the sum exceeds what the file could hold.
        std::vector<char> bytes = MakeV1Header(64u);
        for (uint32_t e = 0; e < 64u; ++e)
            AppendEntityDescriptor(bytes, "E" + std::to_string(e), 2u, 200000u, 2u);

        const fs::path path = scratch.Path / "aggregate.bin";
        WriteBytes(path, bytes);

        DataPlayer player;
        CHECK_NOTHROW(CHECK_FALSE(player.Load(path.string())));
    }

    const auto elapsed = std::chrono::steady_clock::now() - start;
    // A rejection is a header read and two comparisons. If this ever takes
    // seconds, something is allocating again.
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 2000);
}

// =============================================================================
// DataPlayer — bug 2: truncation reported as success
// =============================================================================

TEST_CASE("DataPlayer rejects every truncation of a real recording")
{
    ScratchDir scratch("robust_truncate");
    const std::vector<char> full = MakeRealRecordingBytes(scratch.Path);
    REQUIRE(full.size() > 64);

    // Sanity: the untouched file still loads. If this fails the fix broke valid
    // files and the whole sweep below is meaningless.
    {
        const fs::path path = scratch.Path / "full.bin";
        WriteBytes(path, full);
        DataPlayer player;
        REQUIRE(player.Load(path.string()));
        CHECK(player.GetEntityNames().size() == 2);
    }

    // Every prefix of a valid file is missing data it declares in its own
    // header, so every one of them must fail. Before W9 the tail of this range
    // returned TRUE with zero-filled frames.
    const fs::path path = scratch.Path / "cut.bin";
    size_t checked = 0;
    for (size_t cut = 0; cut < full.size(); cut += 7)   // stride keeps the sweep quick
    {
        WriteBytes(path, std::vector<char>(full.begin(), full.begin() + cut));

        DataPlayer player;
        CAPTURE(cut);
        CHECK_FALSE(player.Load(path.string()));
        CHECK_FALSE(player.IsLoaded());
        ++checked;
    }
    CHECK(checked > 20);

    // The boundaries the stride can step over: one byte short is the case the
    // old predicate got wrong, and it is the whole point of the fix.
    for (size_t cut : { full.size() - 1, full.size() - 2, full.size() - 4 })
    {
        WriteBytes(path, std::vector<char>(full.begin(), full.begin() + cut));

        DataPlayer player;
        CAPTURE(cut);
        CHECK_FALSE(player.Load(path.string()));
    }
}

TEST_CASE("DataPlayer accepts a file with trailing garbage but not a short one")
{
    // Asymmetry worth pinning: extra bytes after the declared data are harmless
    // (the loader never reads them), missing bytes are not.
    ScratchDir scratch("robust_trailing");
    std::vector<char> full = MakeRealRecordingBytes(scratch.Path, 20);

    full.insert(full.end(), 128, '\x7F');
    const fs::path path = scratch.Path / "trailing.bin";
    WriteBytes(path, full);

    DataPlayer player;
    CHECK(player.Load(path.string()));
    CHECK(player.GetEntityNames().size() == 2);
}

// =============================================================================
// DataPlayer — seeded fuzz
// =============================================================================

TEST_CASE("DataPlayer survives seeded random-byte fuzz")
{
    ScratchDir scratch("robust_fuzz");
    const fs::path path = scratch.Path / "fuzz.bin";

    // Fixed seed: a failure here reproduces exactly on a rerun.
    std::mt19937 rng(0xC05B1Cu);
    std::uniform_int_distribution<int>    byteDist(0, 255);
    std::uniform_int_distribution<size_t> lenDist(0, 2048);

    const auto start = std::chrono::steady_clock::now();

    for (int iter = 0; iter < 200; ++iter)
    {
        std::vector<char> bytes;

        // Two thirds carry a valid magic + version so the fuzz actually reaches
        // the count-handling code; pure noise almost always dies at the magic
        // check and would exercise nothing.
        const bool seeded = (iter % 3) != 0;
        if (seeded)
        {
            bytes.insert(bytes.end(), { 'C', 'S', 'M', 'C' });
            Append(bytes, uint32_t(1));
        }

        const size_t extra = lenDist(rng);
        for (size_t i = 0; i < extra; ++i)
            bytes.push_back(static_cast<char>(byteDist(rng)));

        WriteBytes(path, bytes);

        // The contract under fuzz is narrow and absolute: never throw, never
        // hang, never crash. The return value is deliberately NOT asserted — a
        // random buffer is allowed to be accidentally well-formed.
        DataPlayer player;
        CAPTURE(iter);
        CHECK_NOTHROW(player.Load(path.string()));
    }

    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() < 30);
}

// =============================================================================
// DataRecorder — lifecycle and concurrency
// =============================================================================

TEST_CASE("DataRecorder autosave overwrites one rolling folder")
{
    ScratchDir scratch("robust_autosave");

    DataRecorder recorder;
    const uint32_t id = recorder.Register("Rolling", "test", { "v" });
    recorder.ReserveCapacity(256);

    // SetAutosave clamps the interval up to 0.1s, so 0.05s ticks fire it every
    // other tick. The session name is fixed by design — the point of autosave is
    // ONE rolling snapshot, not a new timestamped folder per interval.
    recorder.SetAutosave(scratch.Path.string(), "_autosave", 0.1f, 60.0f);

    for (int i = 0; i < 20; ++i)
    {
        recorder.Record(id, std::vector<float>{ (float)i });
        recorder.Tick(0.05f);
    }
    recorder.WaitForFlush();

    const fs::path rolling = scratch.Path / "_autosave";
    REQUIRE(fs::exists(rolling / "scene.bin"));

    size_t dirCount = 0;
    for (const auto& entry : fs::directory_iterator(scratch.Path))
        if (entry.is_directory()) ++dirCount;
    CHECK(dirCount == 1);

    const auto firstSize = fs::file_size(rolling / "scene.bin");

    // A second round must land in the SAME folder, with more data in it.
    for (int i = 0; i < 40; ++i)
    {
        recorder.Record(id, std::vector<float>{ (float)i });
        recorder.Tick(0.05f);
    }
    recorder.WaitForFlush();

    dirCount = 0;
    for (const auto& entry : fs::directory_iterator(scratch.Path))
        if (entry.is_directory()) ++dirCount;
    CHECK(dirCount == 1);
    CHECK(fs::file_size(rolling / "scene.bin") > firstSize);

    recorder.DisableAutosave();

    // And the rolling snapshot is a loadable recording, not a torn file.
    DataPlayer player;
    CHECK(player.Load((rolling / "scene.bin").string()));
}

TEST_CASE("DataRecorder::Flush is safe while four threads are recording")
{
    ScratchDir scratch("robust_concurrent");

    constexpr int kThreads         = 4;
    constexpr int kFramesPerThread = 10000;

    DataRecorder recorder;
    std::vector<uint32_t> ids;
    for (int t = 0; t < kThreads; ++t)
        ids.push_back(recorder.Register("Worker" + std::to_string(t), "test", { "x", "y" }));
    recorder.ReserveCapacity(kFramesPerThread + 64);

    std::atomic<bool> go{ false };
    std::vector<std::thread> workers;
    for (int t = 0; t < kThreads; ++t)
    {
        workers.emplace_back([&recorder, &go, id = ids[t], t]()
        {
            while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            for (int i = 0; i < kFramesPerThread; ++i)
                recorder.Record(id, std::vector<float>{ (float)i, (float)t });
        });
    }

    go.store(true, std::memory_order_release);

    // Flush mid-flight — it snapshots under the per-entity locks while the
    // workers keep pushing. The snapshot is allowed to be short; it is NOT
    // allowed to be torn, and nothing may deadlock.
    recorder.Flush(scratch.Path.string(), "concurrent", 60.0f);

    for (auto& w : workers) w.join();
    recorder.WaitForFlush();

    CHECK(recorder.GetTotalFrameCount() == (size_t)kFramesPerThread);

    const fs::path bin = scratch.Path / "concurrent" / "scene.bin";
    REQUIRE(fs::exists(bin));

    DataPlayer player;
    REQUIRE(player.Load(bin.string()));
    CHECK(player.GetEntityNames().size() == kThreads);

    // Every entity in the snapshot must carry the full channel set — a torn
    // write would show up here as a short or missing channel list.
    for (int t = 0; t < kThreads; ++t)
    {
        const EntityTelemetryInfo* info = player.GetInfo("Worker" + std::to_string(t));
        REQUIRE(info != nullptr);
        CHECK(info->channels.size() == 2);
    }
}

TEST_CASE("DataRecorder::Clear drops frames but keeps registrations")
{
    ScratchDir scratch("robust_clear");

    DataRecorder recorder;
    const uint32_t idA = recorder.Register("KeepA", "test", { "one", "two" });
    const uint32_t idB = recorder.Register("KeepB", "test", { "solo" });
    recorder.ReserveCapacity(64);

    for (int i = 0; i < 30; ++i)
    {
        recorder.Tick(1.0f / 60.0f);
        recorder.Record(idA, std::vector<float>{ (float)i, (float)i });
        recorder.Record(idB, std::vector<float>{ 1.0f });
    }
    REQUIRE(recorder.GetTotalFrameCount() == 30);
    REQUIRE(recorder.GetRecordedDuration() > 0.0f);

    recorder.Clear();

    CHECK(recorder.GetTotalFrameCount() == 0);
    CHECK(recorder.GetRecordedDuration() == doctest::Approx(0.0f));

    // Registrations survive — this is what makes Clear usable between runs.
    CHECK(recorder.GetEntityNames().size() == 2);
    REQUIRE(recorder.GetInfo("KeepA") != nullptr);
    CHECK(recorder.GetInfo("KeepA")->channels.size() == 2);
    CHECK(recorder.GetInfo("KeepB") != nullptr);

    // And the same IDs keep working afterwards.
    for (int i = 0; i < 10; ++i)
    {
        recorder.Tick(1.0f / 60.0f);
        recorder.Record(idA, std::vector<float>{ 5.0f, 6.0f });
        recorder.Record(idB, std::vector<float>{ 7.0f });
    }
    CHECK(recorder.GetTotalFrameCount() == 10);

    // Re-registering an existing name returns the SAME id rather than duplicating.
    CHECK(recorder.Register("KeepA", "test", { "one", "two" }) == idA);
    CHECK(recorder.GetEntityNames().size() == 2);
}

TEST_CASE("DataRecorder tolerates a second Flush while the first is in flight")
{
    ScratchDir scratch("robust_doubleflush");

    DataRecorder recorder;
    const uint32_t id = recorder.Register("Twice", "test", { "v" });
    recorder.ReserveCapacity(4096);
    for (int i = 0; i < 4000; ++i)
    {
        recorder.Tick(1.0f / 60.0f);
        recorder.Record(id, std::vector<float>{ (float)i });
    }

    // The second call is expected to be ignored (it warns and returns) rather
    // than racing the first onto the same files or spawning a second thread.
    recorder.Flush(scratch.Path.string(), "first", 60.0f);
    recorder.Flush(scratch.Path.string(), "second", 60.0f);
    recorder.WaitForFlush();

    CHECK(fs::exists(scratch.Path / "first" / "scene.bin"));

    // Once the first has finished, a fresh Flush works normally.
    recorder.Flush(scratch.Path.string(), "third", 60.0f);
    recorder.WaitForFlush();
    REQUIRE(fs::exists(scratch.Path / "third" / "scene.bin"));

    DataPlayer player;
    REQUIRE(player.Load((scratch.Path / "third" / "scene.bin").string()));
    CHECK(player.GetDuration() == doctest::Approx(4000.0f / 60.0f).epsilon(0.02));

    // Flushing the same session name twice overwrites cleanly and stays loadable.
    recorder.Flush(scratch.Path.string(), "third", 60.0f);
    recorder.WaitForFlush();
    DataPlayer again;
    CHECK(again.Load((scratch.Path / "third" / "scene.bin").string()));
}

TEST_CASE("DataRecorder destructor waits for an in-flight flush")
{
    ScratchDir scratch("robust_dtor");

    {
        DataRecorder recorder;
        const uint32_t id = recorder.Register("Dying", "test", { "a", "b", "c", "d" });
        recorder.ReserveCapacity(20000);
        for (int i = 0; i < 20000; ++i)
        {
            recorder.Tick(1.0f / 60.0f);
            recorder.Record(id, std::vector<float>{ (float)i, 1.0f, 2.0f, 3.0f });
        }

        // Deliberately NO WaitForFlush — the destructor has to join the writer.
        // Getting this wrong is a torn file at best and a use-after-free of the
        // recorder's own members from the writer thread at worst.
        recorder.Flush(scratch.Path.string(), "dtor", 60.0f);
    }

    // Scope exited: the file must be complete and loadable.
    const fs::path bin = scratch.Path / "dtor" / "scene.bin";
    REQUIRE(fs::exists(bin));

    DataPlayer player;
    REQUIRE(player.Load(bin.string()));
    CHECK(player.GetEntityNames().size() == 1);

    TelemetryFrame frame;
    REQUIRE(player.SampleAt("Dying", player.GetDuration() * 0.5f, frame));
    CHECK(frame.values.size() == 4);
}
