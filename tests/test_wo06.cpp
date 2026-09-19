#include "../Projects/SF_Telem/src/SF_Telem.h"
#include "FakeSerialTransport.h"
#include "telemetry/DataPlayer.h"
#include "telemetry/DataRecorder.h"
#include "utils/AtomicOutput.h"
#include "utils/DataExport.h"
#include <Windows.h>
#include <barrier>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <doctest.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <locale>
#include <psapi.h>
#include <random>
#include <set>
#include <string>
#include <thread>
using namespace Cosmic;
namespace fs = std::filesystem;
namespace
{
fs::path Scratch(const char *name)
{
    // KI-57 (AP-P1): start each named scratch EMPTY, once per process. D01
    // deliberately leaves a bad-version scene.bin in %TEMP%\wo06\fallback, so
    // without this the next process to run D01 loaded that leftover and
    // REQUIRE(p.Load(...)) failed — the suite passed only on a machine's first
    // run, and CI's Debug pass poisoned its own Release pass.
    //
    // Once per NAME, not per call: several cases call Scratch("x") again to read
    // back what they just wrote there, so clearing on every call would delete the
    // data under the test. Main-thread only, like every call site.
    static std::set<std::string> cleared;
    auto p = fs::temp_directory_path() / "wo06" / name;
    if (cleared.insert(name).second)
    {
        std::error_code ec;
        fs::remove_all(p, ec);
    }
    fs::create_directories(p);
    return p;
}
void Text(const fs::path &p, const std::string &s)
{
    std::ofstream f(p, std::ios::binary);
    f << s;
}
std::string Bytes(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
template <class T> void Put(std::ofstream &f, T v)
{
    f.write(reinterpret_cast<const char *>(&v), sizeof(v));
}
// Independent v1 encoder; no recorder or player calls.
void Specimen(const fs::path &p, std::vector<float> times = {0, 2, 4})
{
    std::ofstream f(p, std::ios::binary);
    f.write("CSMC", 4);
    Put(f, 1u);
    Put(f, 1u);
    Put(f, 60.0f);
    char name[64] = "A", tag[64] = "specimen", ch[32] = "value";
    f.write(name, 64);
    f.write(tag, 64);
    Put(f, 1u);
    Put(f, static_cast<unsigned>(times.size()));
    f.write(ch, 32);
    for (size_t i = 0; i < times.size(); ++i)
    {
        Put(f, times[i]);
        Put(f, static_cast<float>(i * 8));
    }
}
namespace
{
struct RawEntity
{
    std::string name, tag;
    std::vector<std::string> channels;
    std::vector<std::vector<float>> rows;
};
std::vector<RawEntity> Decode(const fs::path &path)
{
    auto b = Bytes(path);
    size_t at = 0;
    auto u32 = [&]() {
        REQUIRE(at + 4 <= b.size());
        unsigned v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<unsigned>(static_cast<unsigned char>(b[at++])) << (8 * i);
        return v;
    };
    auto str = [&](size_t n) {
        REQUIRE(at + n <= b.size());
        auto s = b.substr(at, n);
        at += n;
        return s.substr(0, s.find('\0'));
    };
    REQUIRE(str(4) == "CSMC");
    REQUIRE(u32() == 1);
    auto count = u32();
    CHECK(std::bit_cast<float>(u32()) == 60);
    std::vector<RawEntity> result(count);
    std::vector<unsigned> counts(count);
    for (unsigned e = 0; e < count; ++e)
    {
        auto &r = result[e];
        r.name = str(64);
        r.tag = str(64);
        auto channels = u32();
        counts[e] = u32();
        for (unsigned c = 0; c < channels; ++c)
            r.channels.push_back(str(32));
    }
    for (unsigned e = 0; e < count; ++e)
        for (unsigned i = 0; i < counts[e]; ++i)
        {
            std::vector<float> row;
            for (size_t c = 0; c <= result[e].channels.size(); ++c)
                row.push_back(std::bit_cast<float>(u32()));
            result[e].rows.push_back(std::move(row));
        }
    CHECK(at == b.size());
    return result;
}
std::string Utf8(const fs::path &p)
{
    auto s = p.u8string();
    return {reinterpret_cast<const char *>(s.data()), s.size()};
}
struct Gate
{
    std::promise<void> entered, release;
    std::shared_future<void> ready = release.get_future().share();
    std::atomic<unsigned> calls{0};
    void Wait()
    {
        if (++calls == 1)
            entered.set_value();
        ready.wait();
    }
    ~Gate()
    {
        if (calls)
        {
            try
            {
                release.set_value();
            }
            catch (...)
            {
            }
        }
    }
};
} // namespace
TEST_CASE("WO-06 D01: pinned independent v1 and recorder exact storage")
{
    auto pinned = fs::path(COSMIC_WO06_FIXTURES) / "independent-v1.bin";
    auto expected = Decode(pinned);
    DataPlayer p;
    REQUIRE(p.Load(pinned.string()));
    CHECK(p.GetEntityNames().size() == 4);
    for (const auto &e : expected)
    {
        auto *info = p.GetInfo(e.name);
        REQUIRE(info);
        CHECK(info->tag == e.tag);
        CHECK(info->channels == e.channels);
        for (const auto &row : e.rows)
        {
            TelemetryFrame f;
            REQUIRE(p.SampleAt(e.name, row[0], f));
            REQUIRE(f.values.size() + 1 == row.size());
            for (size_t c = 0; c < f.values.size(); ++c)
                CHECK(std::bit_cast<unsigned>(f.values[c]) == std::bit_cast<unsigned>(row[c + 1]));
        }
    }
    TelemetryFrame f;
    CHECK_FALSE(p.SampleAt("Empty", 2, f));
    DataRecorder r;
    for (const auto &e : expected)
        r.Register(e.name, e.tag, e.channels);
    r.Record(2, {8});
    r.Tick(1);
    r.Record(3, {100});
    r.Tick(.25f);
    r.Record(1, {-0.0f});
    r.Tick(.75f);
    r.Record(2, {16});
    r.Tick(1);
    r.Record(3, {300});
    r.Tick(1);
    r.Record(2, {32});
    auto dir = Scratch("exact");
    r.Flush(dir.string(), "save");
    r.WaitForFlush();
    CHECK(r.GetFlushState() == DataRecorder::FlushState::Succeeded);
    auto actual = Decode(dir / "save/scene.bin");
    REQUIRE(actual.size() == expected.size());
    for (size_t e = 0; e < expected.size(); ++e)
    {
        CHECK(actual[e].name == expected[e].name);
        CHECK(actual[e].channels == expected[e].channels);
        REQUIRE(actual[e].rows.size() == expected[e].rows.size());
        for (size_t s = 0; s < actual[e].rows.size(); ++s)
            for (size_t c = 0; c < actual[e].rows[s].size(); ++c)
                CHECK(std::bit_cast<unsigned>(actual[e].rows[s][c]) ==
                      std::bit_cast<unsigned>(expected[e].rows[s][c]));
    }
    auto fallback = Scratch("fallback");
    fs::copy_file(fs::path(COSMIC_WO06_FIXTURES) / "fallback-A.bin", fallback / "A.bin",
                  fs::copy_options::overwrite_existing);
    REQUIRE(p.Load(fallback.string()));
    CHECK(p.GetEntityNames() == std::vector<std::string>{"A"});
    fs::copy_file(pinned, fallback / "scene.bin", fs::copy_options::overwrite_existing);
    REQUIRE(p.Load(fallback.string()));
    CHECK(p.GetEntityNames().size() == 4);
    fs::copy_file(fs::path(COSMIC_WO06_FIXTURES) / "bad-version.bin", fallback / "scene.bin",
                  fs::copy_options::overwrite_existing);
    CHECK_FALSE(p.Load(fallback.string()));
    CHECK(p.GetEntityNames().empty());
}
TEST_CASE("WO-06 D02: independent interpolation endpoints pause and all speeds")
{
    DataPlayer p;
    REQUIRE(p.Load(std::string(COSMIC_WO06_FIXTURES) + "/independent-v1.bin"));
    TelemetryFrame f;
    for (auto pair :
         {std::pair{-2.f, 8.f}, {0.f, 8.f}, {1.f, 12.f}, {2.f, 16.f}, {3.f, 24.f}, {4.f, 32.f}, {8.f, 32.f}})
    {
        REQUIRE(p.SampleAt("A", pair.first, f));
        CHECK(f.values[0] == pair.second);
        CHECK(f.timestamp == std::clamp(pair.first, 0.f, 4.f));
    }
    for (auto pair : {std::pair{0.f, 100.f}, {1.f, 100.f}, {2.f, 200.f}, {3.f, 300.f}, {4.f, 300.f}})
    {
        REQUIRE(p.SampleAt("B", pair.first, f));
        CHECK(f.values[0] == pair.second);
    }
    for (float speed : {-2.f, -1.f, 0.f, .25f, 1.f, 4.f})
    {
        CAPTURE(speed);
        p.SetPosition(2);
        p.SetSpeed(speed);
        p.Play();
        p.Tick(.25f);
        CHECK(p.GetPosition() == 2 + .25f * speed);
        p.Pause();
        auto pos = p.GetPosition();
        p.Tick(1);
        CHECK(p.GetPosition() == pos);
        p.SetPosition(speed < 0 ? .5f : 3.5f);
        p.Play();
        p.Tick(10);
        if (speed != 0)
        {
            CHECK_FALSE(p.IsPlaying());
            CHECK(p.GetPosition() == (speed < 0 ? 0.f : 4.f));
        }
        else
            CHECK(p.IsPlaying());
    }
    p.SetPosition(-10);
    CHECK(p.GetPosition() == 0);
    p.SetPosition(100);
    CHECK(p.GetPosition() == 4);
    p.SetSpeed(1);
    p.SetSpeed(std::numeric_limits<float>::infinity());
    CHECK(p.GetSpeed() == 1);
    p.SetPosition(2);
    p.Play();
    p.Tick(std::numeric_limits<float>::quiet_NaN());
    CHECK(p.GetPosition() == 2);
    p.Tick(-1);
    CHECK(p.GetPosition() == 2);
}
TEST_CASE("WO-06 D03: bounded independent F-CORRUPT seed6 2000 cases")
{
    auto dir = Scratch("fuzz");
    auto good = Bytes(fs::path(COSMIC_WO06_FIXTURES) / "independent-v1.bin");
    std::mt19937 rng(6);
    DataPlayer p;
    char *volume = nullptr;
    size_t length = 0;
    _dupenv_s(&volume, &length, "COSMIC_WO06_FUZZ_CASES");
    unsigned cases = volume ? static_cast<unsigned>(std::strtoul(volume, nullptr, 10)) : 2000;
    free(volume);
    REQUIRE((cases == 2000 || cases == 50000));
    for (unsigned i = 0; i < cases; ++i)
    {
        auto bad = good;
        auto bits = [&](size_t offset, unsigned value) {
            for (int n = 0; n < 4; ++n)
                bad[offset + n] = static_cast<char>(value >> (8 * n));
        };
        switch (i % 12)
        {
        case 0:
            bad.resize(rng() % good.size());
            break;
        case 1:
            bad[0] = 'X';
            break;
        case 2:
            bad[4] = static_cast<char>(2 + rng() % 250);
            break;
        case 3:
            for (int n = 8; n < 12; ++n)
                bad[n] = static_cast<char>(255);
            break;
        case 4:
            for (int n = 16; n < 80; ++n)
                bad[n] = 'x';
            break;
        case 5:
            for (int n = 144; n < 148; ++n)
                bad[n] = static_cast<char>(255);
            break;
        case 6:
            for (int n = 80; n < 144; ++n)
                bad[n] = 't';
            break;
        case 7:
            for (int n = 152; n < 184; ++n)
                bad[n] = 'c';
            break;
        case 8:
            bits(148, 0xffffffffu);
            break;
        case 9:
            bits(12, 0x7fc00000u);
            break;
        case 10:
            bits(688, 0x7fc00000u);
            break;
        case 11:
            bits(704, std::bit_cast<unsigned>(-1.f));
            break;
        }
        Text(dir / "bad.bin", bad);
        CAPTURE(i);
        CHECK_FALSE(p.Load((dir / "bad.bin").string()));
        CHECK_FALSE(p.IsLoaded());
        CHECK_FALSE(p.IsPlaying());
        CHECK(p.GetEntityNames().empty());
        CHECK(p.GetPosition() == 0);
        CHECK(p.GetDuration() == 0);
    }
    Text(dir / "trailing.bin", good + "legacy trailing bytes");
    REQUIRE(p.Load((dir / "trailing.bin").string()));
    CHECK(p.GetEntityNames().size() == 4);
    // A failed multi-file load clears successful earlier files too.
    auto multi = dir / "multi";
    fs::create_directory(multi);
    Text(multi / "a.bin", good);
    Text(multi / "b.bin", "bad");
    CHECK_FALSE(p.Load(multi.string()));
    CHECK(p.GetEntityNames().empty());
    std::printf("WO06 F-CORRUPT seed=6 cases=%u max_fixture_bytes=%zu\n", cases, good.size());
}
TEST_CASE("WO-06 D04: four writers 10000 each concurrent prefixes double flush clear")
{
    auto dir = Scratch("writers");
    DataRecorder r;
    for (int e = 0; e < 4; ++e)
        r.Register("E" + std::to_string(e), "writer", {"sequence", "negative"});
    r.ReserveCapacity(10000);
    std::barrier middle(5);
    std::vector<std::thread> writers;
    for (unsigned e = 0; e < 4; ++e)
        writers.emplace_back([&, e] {
            for (int i = 0; i < 5000; ++i)
                r.Record(e, {static_cast<float>(i), static_cast<float>(-i)});
            middle.arrive_and_wait();
            for (int i = 5000; i < 10000; ++i)
                r.Record(e, {static_cast<float>(i), static_cast<float>(-i)});
        });
    Gate gate;
    r.SetFlushWriteBarrier([&] { gate.Wait(); });
    middle.arrive_and_wait();
    r.Flush(dir.string(), "prefix");
    gate.entered.get_future().wait();
    r.Flush(dir.string(), "ignored");
    CHECK_FALSE(fs::exists(dir / "ignored/scene.bin"));
    for (auto &w : writers)
        w.join();
    gate.release.set_value();
    r.WaitForFlush();
    CHECK(r.GetFlushState() == DataRecorder::FlushState::Succeeded);
    auto prefix = Decode(dir / "prefix/scene.bin");
    for (const auto &e : prefix)
    {
        CHECK(e.rows.size() >= 5000);
        CHECK(e.rows.size() <= 10000);
        for (size_t i = 0; i < e.rows.size(); ++i)
        {
            CHECK(e.rows[i][1] == static_cast<float>(i));
            CHECK(e.rows[i][2] == -static_cast<float>(i));
        }
    }
    r.SetFlushWriteBarrier({});
    r.Flush(dir.string(), "final");
    r.WaitForFlush();
    auto final = Decode(dir / "final/scene.bin");
    for (const auto &e : final)
    {
        CHECK(e.rows.size() == 10000);
        for (size_t i = 0; i < e.rows.size(); ++i)
            CHECK(e.rows[i][1] == static_cast<float>(i));
    }
    auto capacity = r.GetStorageBytes().second;
    r.Clear();
    CHECK(r.GetTotalFrameCount() == 0);
    CHECK(r.GetStorageBytes().second == capacity);
    CHECK(r.Register("E0", "different", {"other"}) == 0);
    CHECK(r.GetInfo("E0")->channels == std::vector<std::string>{"sequence", "negative"});
    CHECK(r.Register("new", "tag", {"x"}) == 4);
}
TEST_CASE("WO-06 D04: destruction drains barrier-held flush")
{
    Gate gate;
    auto r = std::make_unique<DataRecorder>();
    r->Register("A", "tag", {"x"});
    r->Record(0, {1});
    r->SetFlushWriteBarrier([&] { gate.Wait(); });
    r->Flush(Scratch("destruction").string(), "save");
    gate.entered.get_future().wait();
    std::promise<void> destroying;
    auto done = std::async(std::launch::async, [&] {
        destroying.set_value();
        r.reset();
    });
    destroying.get_future().wait();
    CHECK(done.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout);
    gate.release.set_value();
    done.get();
    CHECK(gate.calls == 1);
    DataPlayer p;
    CHECK(p.Load((Scratch("destruction") / "save").string()));
}
TEST_CASE("WO-06 D04: shared entity writers retain ordered append times and all calls")
{
    DataRecorder r;
    r.Register("A", "tag", {"call"});
    r.ReserveCapacity(40000);
    std::barrier start(5);
    std::vector<std::thread> writers;
    for (unsigned writer = 0; writer < 4; ++writer)
        writers.emplace_back([&, writer] {
            start.arrive_and_wait();
            for (unsigned i = 0; i < 10000; ++i)
                r.Record(0, {static_cast<float>(writer * 10000 + i)});
        });
    start.arrive_and_wait();
    for (int i = 0; i < 40000; ++i)
    {
        r.Tick(1);
        r.GetTotalFrameCount();
    }
    for (auto &w : writers)
        w.join();
    r.Flush(Scratch("shared-writers").string(), "save");
    r.WaitForFlush();
    auto rows = Decode(Scratch("shared-writers") / "save/scene.bin")[0].rows;
    REQUIRE(rows.size() == 40000);
    unsigned reversals = 0;
    std::vector<unsigned> calls;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (i && rows[i][0] < rows[i - 1][0])
            ++reversals;
        calls.push_back(static_cast<unsigned>(rows[i][1]));
    }
    std::sort(calls.begin(), calls.end());
    for (unsigned i = 0; i < 40000; ++i)
        CHECK(calls[i] == i);
    std::printf("WO06 shared_entity intended=40000 actual=40000 timestamp_reversals=%u\n", reversals);
    CHECK(reversals == 0);
    DataPlayer p;
    CHECK(p.Load((Scratch("shared-writers") / "save").string()));
}
TEST_CASE("WO-06 D06: entire restricted numeric grammar matrix independent constants")
{
    struct Case
    {
        const char *label;
        std::string text;
        bool pass;
        std::vector<std::vector<double>> expected;
    };
    const std::vector<Case> cases = {
        {"header LF", "a,b\n1,2\n3,4\n", true, {{1, 3}, {2, 4}}},
        {"headerless CRLF", "1,2\r\n3,4\r\n", true, {{1, 3}, {2, 4}}},
        {"blank lines", "\n \t\r\na,b\n\n1,2\n\n", true, {{1}, {2}}},
        {"whitespace exponent", "a,b\n \t+1.25e2\t , -.5E+1 \t\r\n", true, {{125}, {-5}}},
        {"signed zero", "a,b\n+0,-0\n", true, {{0}, {0}}},
        {"finite extremes",
         "a,b\n1.7976931348623157e308,4.9406564584124654e-324\n",
         true,
         {{std::numeric_limits<double>::max()}, {std::numeric_limits<double>::denorm_min()}}},
        {"sensitive",
         "x\n1.0000000000000002\n0.10000000000000001\n2.2250738585072014e-308\n",
         true,
         {{std::nextafter(1., 2.), .1, std::numeric_limits<double>::min()}}},
        {"BOM", std::string("\xEF\xBB\xBF") + "a,b\n1,2\n", true, {{1}, {2}}},
        {"too few", "a,b\n1\n", false, {}},
        {"too many", "a,b\n1,2,3\n", false, {}},
        {"blank cell", "a,b\n1,\n", false, {}},
        {"whitespace cell", "a,b\n1, \t\n", false, {}},
        {"leading empty", "a,b\n,2\n", false, {}},
        {"trailing delimiter", "a,b\n1,2,\n", false, {}},
        {"junk", "a,b\n1x,2\n", false, {}},
        {"empty", "", false, {}},
        {"header only", "a,b\n", false, {}},
        {"duplicate header", "a,a\n1,2\n", false, {}},
        {"trim duplicate", "a, a\n1,2\n", false, {}},
        {"numeric mixed header", "1,b\n1,2\n", false, {}},
        {"numeric row is data", "1,2\n3,4\n", true, {{1, 3}, {2, 4}}},
        {"quote comma", "\"a,b\",c\n1,2\n", false, {}},
        {"quote newline", "\"a\nb\",c\n1,2\n", false, {}},
        {"NaN", "a\nnan\n", false, {}},
        {"Inf", "a\ninf\n", false, {}},
        {"negative Inf", "a\n-inf\n", false, {}},
        {"overflow", "a\n1e999\n", false, {}},
        {"underflow", "a\n1e-999\n", false, {}},
        {"hex", "a\n0x1p2\n", false, {}},
        {"embedded CR", "a\n1\r2\n", false, {}},
        {"blank header", "a,\n1,2\n", false, {}},
        {"bare dot", "a\n.\n", false, {}},
        {"bare sign", "a\n+\n", false, {}},
        {"incomplete exponent", "a\n1e+\n", false, {}},
        {"nonfinite first", "nan,inf\n1,2\n", false, {}},
        {"embedded NUL", std::string("a\n1\0x\n", 6), false, {}}};
    auto path = Scratch("csv-matrix") / fs::path(L"numeric-\u03A9-\u4E2D.csv");
    for (const auto &c : cases)
    {
        CAPTURE(c.label);
        Text(path, c.text);
        std::vector<std::vector<double>> cols{{999}};
        std::vector<std::string> headers{"old"};
        CHECK(DataExport::LoadCSV(Utf8(path), cols, &headers) == c.pass);
        if (!c.pass)
        {
            CHECK(cols.empty());
            CHECK(headers.empty());
            continue;
        }
        REQUIRE(cols.size() == c.expected.size());
        for (size_t col = 0; col < cols.size(); ++col)
        {
            REQUIRE(cols[col].size() == c.expected[col].size());
            for (size_t row = 0; row < cols[col].size(); ++row)
                if (c.expected[col][row] != 0)
                    CHECK(std::bit_cast<uint64_t>(cols[col][row]) ==
                          std::bit_cast<uint64_t>(c.expected[col][row]));
                else
                    CHECK(cols[col][row] == 0);
        }
    }
    std::printf("WO06 D06 grammar_cases=%zu Unicode_UTF8_path=PASS\n", cases.size());
}
TEST_CASE("WO-06 D06: writer numeric oracle locale and parameter failure matrix")
{
    auto path = Scratch("csv-output") / fs::path(L"export-\u03A9.csv");
    auto s = Utf8(path);
    const std::vector<double> expected{.1,
                                       std::nextafter(1., 2.),
                                       std::numeric_limits<double>::max(),
                                       std::numeric_limits<double>::min(),
                                       std::numeric_limits<double>::denorm_min(),
                                       -0.};
    struct Comma : std::numpunct<char>
    {
        char do_decimal_point() const override
        {
            return ',';
        }
    };
    auto old = std::locale();
    std::locale::global(std::locale(old, new Comma));
    bool written = DataExport::WriteCSV(s, {"x"}, {expected});
    std::vector<std::vector<double>> c;
    bool loaded = DataExport::LoadCSV(s, c);
    std::locale::global(old);
    REQUIRE(written);
    REQUIRE(loaded);
    REQUIRE(c.size() == 1);
    REQUIRE(c[0].size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
        if (expected[i] != 0)
            CHECK(std::bit_cast<uint64_t>(c[0][i]) == std::bit_cast<uint64_t>(expected[i]));
    auto good = Bytes(path);
    for (const auto &headers :
         {std::vector<std::string>{"x,y"}, std::vector<std::string>{"\"x\""},
          std::vector<std::string>{"x\ny"}, std::vector<std::string>{"12"}, std::vector<std::string>{""}})
    {
        CHECK_FALSE(DataExport::WriteCSV(s, headers, {{1}}));
        CHECK(Bytes(path) == good);
    }
    CHECK_FALSE(DataExport::WriteCSV(s, {"a", "b"}, {{1}}));
    CHECK_FALSE(DataExport::WriteCSV(s, {"a", "b"}, {{1}, {1, 2}}));
    CHECK_FALSE(DataExport::WriteCSV(s, {"a", "a"}, {{1}, {2}}));
    for (double v : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
    {
        CHECK_FALSE(DataExport::WriteCSV(s, {"x"}, {{v}}));
        CHECK_FALSE(DataExport::AppendRow(s, {v}));
    }
    CHECK(Bytes(path) == good);
    float ring[]{1, 2, 3};
    for (auto params : {std::vector<int>{-1, 0, 3}, {4, 0, 3}, {1, -1, 3}, {1, 3, 3}, {0, 0, 0}, {1, 0, -1}})
        CHECK_FALSE(DataExport::WriteCircularBuffer(s, {"x"}, {ring}, params[0], params[1], params[2]));
    CHECK_FALSE(DataExport::WriteCircularBuffer(s, {"x"}, {nullptr}, 1, 0, 3));
    CHECK_FALSE(DataExport::WriteCircularBuffer(s, {"x", "y"}, {ring}, 1, 0, 3));
    CHECK(Bytes(path) == good);
    REQUIRE(DataExport::WriteCircularBuffer(s, {"x"}, {ring}, 3, 2, 3));
    REQUIRE(DataExport::LoadCSV(s, c));
    CHECK(c == std::vector<std::vector<double>>{{3, 1, 2}});
    REQUIRE(DataExport::AppendRow(s, {4}));
    REQUIRE(DataExport::LoadCSV(s, c));
    CHECK(c[0] == std::vector<double>{3, 1, 2, 4});
    auto blocker = Scratch("csv-output") / "blocker";
    Text(blocker, "file");
    CHECK_FALSE(DataExport::WriteCSV((blocker / "child/a.csv").string(), {"x"}, {{1}}));
}
TEST_CASE("WO-06 D06: actual stream status and OS publication failures preserve output")
{
    auto path = Scratch("csv-fault") / "a.csv";
    Text(path, "last good");
    for (const char *stage : {"open", "write", "flush", "close", "partial-write", "publish"})
    {
        CAPTURE(stage);
        SetWriteFaultForTesting(
            [&](const std::string &, const char *actual) { return std::string(actual) == stage; });
        CHECK_FALSE(DataExport::WriteCSV(path.string(), {"x"}, {{1, 2}}));
        CHECK_FALSE(DataExport::AppendRow(path.string(), {3}));
        float buffer[]{1};
        CHECK_FALSE(DataExport::WriteCircularBuffer(path.string(), {"x"}, {buffer}, 1, 0, 1));
        SetWriteFaultForTesting({});
        CHECK(Bytes(path) == "last good");
    }
    {
        AtomicOutput out(path.string());
        REQUIRE(out.stream.is_open());
        out.stream << "partial";
        out.stream.setstate(std::ios::badbit);
        CHECK_FALSE(out.Finish());
        CHECK_FALSE(out.Publish());
    }
    CHECK(Bytes(path) == "last good");
    REQUIRE(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY));
    CHECK_FALSE(DataExport::WriteCSV(path.string(), {"x"}, {{1}}));
    REQUIRE(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL));
    CHECK(Bytes(path) == "last good");
    auto directory = Scratch("csv-fault") / "directory.csv";
    fs::create_directory(directory);
    CHECK_FALSE(DataExport::WriteCSV(directory.string(), {"x"}, {{1}}));
}
TEST_CASE("WO-06 D05: injected disk failures interrupted autosave preserve publication")
{
    auto dir = Scratch("record-fault");
    DataRecorder r;
    r.Register("A", "tag", {"x"});
    r.Record(0, {1});
    r.Flush(dir.string(), "save");
    r.WaitForFlush();
    auto old = Bytes(dir / "save/scene.bin");
    r.Tick(1);
    r.Record(0, {2});
    for (const char *stage : {"open", "write", "flush", "close", "partial-write", "publish"})
        for (bool csv : {false, true})
        {
            CAPTURE(stage);
            CAPTURE(csv);
            SetWriteFaultForTesting([&](const std::string &path, const char *actual) {
                return (path.ends_with(".csv") == csv) && std::string(actual) == stage;
            });
            r.Flush(dir.string(), "save");
            r.WaitForFlush();
            SetWriteFaultForTesting({});
            CHECK(r.GetFlushState() == DataRecorder::FlushState::Failed);
            CHECK_FALSE(r.IsFlushing());
            CHECK(Bytes(dir / "save/scene.bin") == old);
            DataPlayer p;
            CHECK(p.Load((dir / "save").string()));
        }
    r.SetFlushWriteBarrier([] { throw std::runtime_error("interrupted autosave"); });
    r.Flush(dir.string(), "save");
    r.WaitForFlush();
    CHECK(r.GetFlushState() == DataRecorder::FlushState::Failed);
    CHECK(Bytes(dir / "save/scene.bin") == old);
    r.SetFlushWriteBarrier({});
    REQUIRE(SetFileAttributesW((dir / "save/scene.bin").c_str(), FILE_ATTRIBUTE_READONLY));
    r.Flush(dir.string(), "save");
    r.WaitForFlush();
    CHECK(r.GetFlushState() == DataRecorder::FlushState::Failed);
    REQUIRE(SetFileAttributesW((dir / "save/scene.bin").c_str(), FILE_ATTRIBUTE_NORMAL));
    CHECK(Bytes(dir / "save/scene.bin") == old);
    r.Flush((dir / "save/scene.bin").string(), "invalid");
    r.WaitForFlush();
    CHECK(r.GetFlushState() == DataRecorder::FlushState::Failed);
    r.Flush(dir.string(), "save");
    r.WaitForFlush();
    CHECK(r.GetFlushState() == DataRecorder::FlushState::Succeeded);
    CHECK(Decode(dir / "save/scene.bin")[0].rows.size() == 2);
}
TEST_CASE("WO-06 D05: measured loss from publication and sample accounting")
{
    auto dir = Scratch("loss");
    DataRecorder r;
    r.Register("A", "tag", {"sequence"});
    r.SetAutosave(dir.string(), "rolling", 5);
    for (int i = 0; i < 5; ++i)
    {
        r.Record(0, {static_cast<float>(i)});
        r.Tick(1);
    }
    r.WaitForFlush();
    REQUIRE(r.GetFlushState() == DataRecorder::FlushState::Succeeded);
    auto first = Decode(dir / "rolling/scene.bin");
    REQUIRE(first[0].rows.size() == 5);
    Gate gate;
    r.SetFlushWriteBarrier([&] { gate.Wait(); });
    for (int i = 5; i < 10; ++i)
    {
        r.Record(0, {static_cast<float>(i)});
        r.Tick(1);
    }
    gate.entered.get_future().wait();
    for (int i = 10; i < 17; ++i)
    {
        r.Record(0, {static_cast<float>(i)});
        r.Tick(1);
    }
    CHECK(Decode(dir / "rolling/scene.bin")[0].rows.size() == 5);
    CHECK(r.GetTotalFrameCount() == 17);
    // At t=17 last publication accounts for five samples, so 12 samples / seconds
    // are recoverably absent, despite the configured five-second autosave interval.
    std::printf("WO06 loss held_write current_samples=17 published_samples=5 unpublished_samples=12 "
                "oldest_unpublished_t=5 current_t=17 window_seconds=12\n");
    gate.release.set_value();
    r.WaitForFlush();
    auto second = Decode(dir / "rolling/scene.bin");
    CHECK(second[0].rows.size() == 10);
    CHECK(r.GetTotalFrameCount() - second[0].rows.size() == 7);
    r.SetFlushWriteBarrier({});
    r.DisableAutosave();
    auto start = std::chrono::steady_clock::now();
    r.Flush(dir.string(), "rolling");
    r.WaitForFlush();
    CHECK(Decode(dir / "rolling/scene.bin")[0].rows.size() == 17);
    std::printf("WO06 loss final unpublished_samples=0 final_publication_ms=%.3f\n",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}
TEST_CASE("WO-06 D05: two hour production fixture final export memory and sample accounting")
{
    Workspace::SF_Telem root(std::make_unique<FakeSerialTransport>());
    root.InitializeServices();
    root.SetScreen(Workspace::SF_Telem::SCREEN_MAIN);
    auto &hub = root.Hub();
    hub.SetSessionName("wo06-two-hour");
    hub.StartRecording();
    hub.Recorder().DisableAutosave();
    // Production parser and decoded state; independent primary-channel constants.
    for (char tag : {'R', 'L', 'W'})
    {
        char payload[64], line[96];
        std::snprintf(payload, sizeof(payload), "%c,25,1680,420,120,350", tag);
        unsigned sum = 0;
        for (const char *c = payload; *c; ++c)
            sum ^= static_cast<unsigned char>(*c);
        std::snprintf(line, sizeof(line), "$%s*%02X\n", payload, sum);
        hub.IngestChunk(line);
    }
    const auto start = std::chrono::steady_clock::now();
    std::atomic<size_t> peakPrivate{0};
    auto memory = [&] {
        PROCESS_MEMORY_COUNTERS_EX m{};
        m.cb = sizeof(m);
        if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&m),
                                    sizeof(m)))
        {
            auto before = peakPrivate.load();
            while (before < m.PrivateUsage && !peakPrivate.compare_exchange_weak(before, m.PrivateUsage))
            {
            }
        }
    };
    SetWriteFaultForTesting([&](const std::string &, const char *) {
        memory();
        return false;
    });
    std::vector<std::pair<size_t, size_t>> storage;
    auto exportStart = start;
    for (int i = 0; i < 432000; ++i)
    {
        if (i == 431999)
            exportStart = std::chrono::steady_clock::now();
        root.OnFixedUpdate(1.f / 60.f);
        if (i % 18000 == 0)
            memory();
    }
    const auto captureEnd = std::chrono::steady_clock::now();
    storage.push_back(hub.Recorder().GetStorageBytes());
    memory();
    CHECK_FALSE(hub.Recording());
    CHECK(hub.Recorder().GetTotalFrameCount() == 432000);
    root.OnFixedUpdate(1.f / 60.f);
    CHECK(hub.Recorder().GetTotalFrameCount() == 432000);
    // Stop-and-finalize started the real export on the final fixed tick.
    hub.Recorder().WaitForFlush();
    SetWriteFaultForTesting({});
    memory();
    const auto exported = std::chrono::steady_clock::now();
    CHECK(hub.Recorder().GetFlushState() == DataRecorder::FlushState::Succeeded);
    CHECK(std::chrono::duration<double>(exported - exportStart).count() <= 30);
    CHECK(peakPrivate.load() < 2ull * 1024 * 1024 * 1024);
    auto data = Decode("recordings/SF_Telem/wo06-two-hour/scene.bin");
    REQUIRE(data.size() == 3);
    for (const auto &e : data)
    {
        REQUIRE(e.rows.size() == 432000);
        CHECK(e.channels.size() == (e.name == "ESC_Weapon" ? 9 : 8));
        float last = -1;
        for (const auto &row : e.rows)
        {
            CHECK(row[0] >= last);
            last = row[0];
            CHECK(row[1] == 25);
            CHECK(row[2] == doctest::Approx(16.8));
            CHECK(row[3] == doctest::Approx(4.2));
            CHECK(row[4] == 120);
            CHECK(row[5] == 35000);
        }
    }
    const size_t logical = storage[0].first, reserved = storage[0].second;
    // Recorder history + float snapshot + largest entity's widened CSV columns.
    const size_t snapshotBytes = logical, csvDoubleBytes = 432000ull * 10 * 8;
    std::printf("WO06 two_hour samples_per_entity=432000 entities=3 capture_wall_seconds=%.3f "
                "final_export_total_seconds=%.3f history_logical_bytes=%zu history_capacity_bytes=%zu "
                "snapshot_logical_bytes=%zu largest_csv_double_bytes=%zu observed_private_bytes=%zu "
                "nominal_duration=7200 stored_last_timestamp=%.9g\n",
                std::chrono::duration<double>(captureEnd - start).count(),
                std::chrono::duration<double>(exported - exportStart).count(), logical, reserved,
                snapshotBytes, csvDoubleBytes, peakPrivate.load(), data[0].rows.back()[0]);
    root.OnDetach();
    CHECK_FALSE(hub.RecordingDirty());
}
TEST_CASE("WO-06 D05: production replay live isolation and truthful failed export")
{
    auto transport = std::make_unique<FakeSerialTransport>();
    auto *fake = transport.get();
    auto counters = fake->Counts();
    fake->SetAvailablePorts({"COM_FAKE"});
    Workspace::SF_Telem root(std::move(transport));
    root.InitializeServices();
    root.SetScreen(Workspace::SF_Telem::SCREEN_MAIN);
    auto &hub = root.Hub();
    hub.SetSessionName("wo06-state");
    hub.StartRecording();
    root.OnFixedUpdate(1);
    hub.StopRecording();
    hub.Recorder().WaitForFlush();
    root.OnUpdate(0);
    CHECK_FALSE(hub.RecordingDirty());
    REQUIRE(hub.Player().Load("recordings/SF_Telem/wo06-state"));
    root.SetScreen(Workspace::SF_Telem::SCREEN_REPLAY);
    auto count = hub.Recorder().GetTotalFrameCount();
    root.OnFixedUpdate(1);
    CHECK(hub.Recorder().GetTotalFrameCount() == count);
    CHECK(hub.Replaying());
    auto until = [&](auto predicate) {
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!predicate() && std::chrono::steady_clock::now() < end)
            std::this_thread::yield();
        return predicate();
    };
    root.Link().Connect();
    REQUIRE(until([&] { return root.Link().GetState() == SerialPort::State::Open; }));
    REQUIRE(until([&] { return counters->reads == 1; }));
    auto calls = counters->readCalls.load();
    const std::string payload = "R,25,1680,420,120,350";
    unsigned checksum = 0;
    for (char c : payload)
        checksum ^= static_cast<unsigned char>(c);
    char frame[96];
    std::snprintf(frame, sizeof(frame), "$%s*%02X\n", payload.c_str(), checksum);
    fake->PushBytes(frame);
    REQUIRE(until([&] { return counters->readCalls > calls; }));
    root.OnUpdate(0);
    CHECK(hub.GoodFrames() == 0);
    CHECK(root.Link().ReceivedBytes() == std::strlen(frame));
    CHECK(hub.Volt(Workspace::ESC_RIGHT) == 0);
    hub.StartRecording();
    CHECK_FALSE(hub.Player().IsLoaded());
    CHECK_FALSE(hub.Replaying());
    CHECK(hub.Recording());
    CHECK(hub.Recorder().GetTotalFrameCount() == 0);
    root.SetScreen(Workspace::SF_Telem::SCREEN_MAIN);
    root.OnUpdate(0);
    CHECK(hub.GoodFrames() == 1);
    CHECK(hub.Volt(Workspace::ESC_RIGHT) == doctest::Approx(16.8));
    root.OnFixedUpdate(1);
    SetWriteFaultForTesting(
        [](const std::string &, const char *stage) { return std::string(stage) == "write"; });
    hub.StopRecording();
    hub.Recorder().WaitForFlush();
    SetWriteFaultForTesting({});
    root.OnUpdate(0);
    CHECK(hub.RecordingDirty());
    CHECK(hub.RecordingStatus().find("FAILED") != std::string::npos);
    hub.ExportRecording();
    hub.Recorder().WaitForFlush();
    root.OnUpdate(0);
    CHECK_FALSE(hub.RecordingDirty());
    root.OnDetach();
}
} // namespace
TEST_CASE("WO-06 D03: reject decreasing and nonfinite timestamps")
{
    auto dir = Scratch("invalid-time");
    DataPlayer p;
    for (auto times :
         {std::vector<float>{0, 4, 2}, std::vector<float>{0, std::numeric_limits<float>::quiet_NaN(), 4},
          std::vector<float>{-1, 2, 4}})
    {
        Specimen(dir / "bad.bin", times);
        CHECK_FALSE(p.Load((dir / "bad.bin").string()));
        CHECK_FALSE(p.IsLoaded());
        CHECK(p.GetEntityNames().empty());
    }
}
TEST_CASE("WO-06 D02: invalid seek leaves state and output unchanged")
{
    auto dir = Scratch("seek");
    Specimen(dir / "scene.bin");
    DataPlayer p;
    REQUIRE(p.Load(dir.string()));
    p.SetPosition(2);
    p.SetPosition(std::numeric_limits<float>::quiet_NaN());
    CHECK(p.GetPosition() == 2);
    TelemetryFrame out{7, {99}};
    CHECK_FALSE(p.SampleAt("A", std::numeric_limits<float>::quiet_NaN(), out));
    CHECK(out.timestamp == 7);
    CHECK(out.values == std::vector<float>{99});
}
TEST_CASE("WO-06 D02: reverse and zero transport at held endpoints")
{
    DataPlayer p;
    REQUIRE(p.Load(std::string(COSMIC_WO06_FIXTURES) + "/independent-v1.bin"));
    p.SetPosition(4);
    p.SetSpeed(-1);
    p.Play();
    p.Tick(0);
    CHECK(p.IsPlaying());
    CHECK(p.GetPosition() == 4);
    for (float endpoint : {0.f, 4.f})
    {
        p.SetPosition(endpoint);
        p.SetSpeed(0);
        p.Play();
        p.Tick(1);
        CHECK(p.IsPlaying());
        CHECK(p.GetPosition() == endpoint);
    }
    auto single = Scratch("endpoint-single") / "one.bin";
    Specimen(single, {0});
    REQUIRE(p.Load(single.string()));
    CHECK(p.GetDuration() == 0);
    for (float speed : {-1.f, 0.f, 1.f})
    {
        p.SetSpeed(speed);
        p.Play();
        p.Tick(0);
        CHECK(p.IsPlaying() == (speed == 0));
        CHECK(p.GetPosition() == 0);
    }
}
TEST_CASE("WO-06 D06: blank and nonfinite cells fail with empty outputs")
{
    auto path = Scratch("csv-bad") / "a.csv";
    for (const auto &s :
         {"a,b\n1,   \n", "a,b\n1,nan\n", "a,b\n1,inf\n", "a,b\n1,1e999\n", "a,b\n1,1e-999\n"})
    {
        Text(path, s);
        std::vector<std::vector<double>> c;
        std::vector<std::string> h;
        CHECK_FALSE(DataExport::LoadCSV(path.string(), c, &h));
        CHECK(c.empty());
        CHECK(h.empty());
    }
}
TEST_CASE("WO-06 D05: shutdown finalizes records newer than pending autosave")
{
    auto transport = std::make_unique<FakeSerialTransport>();
    Workspace::SF_Telem root(std::move(transport));
    root.InitializeServices();
    root.SetScreen(Workspace::SF_Telem::SCREEN_MAIN);
    auto &hub = root.Hub();
    hub.SetSessionName("wo06-pending");
    hub.StartRecording();
    root.OnFixedUpdate(1);
    std::promise<void> entered, release;
    auto ready = release.get_future().share();
    std::atomic<int> calls{0};
    hub.Recorder().SetFlushWriteBarrier([&] {
        if (++calls == 1)
            entered.set_value();
        ready.wait();
    });
    hub.Recorder().Flush(Scratch("pending").string(), "old");
    entered.get_future().wait();
    root.OnFixedUpdate(1);
    release.set_value();
    root.OnDetach();
    DataPlayer p;
    REQUIRE(p.Load("recordings/SF_Telem/wo06-pending"));
    CHECK(p.GetDuration() == 1);
}
TEST_CASE("WO-06 D05: recording ceiling stops and finalizes")
{
    for (bool autoExport : {true, false})
    {
        Workspace::SF_Telem root(std::make_unique<FakeSerialTransport>());
        root.InitializeServices();
        root.SetScreen(Workspace::SF_Telem::SCREEN_MAIN);
        auto &h = root.Hub();
        const auto session = autoExport ? "wo06-ceiling-auto" : "wo06-ceiling-mandatory";
        h.SetSessionName(session);
        h.SetAutoExportOnStop(autoExport);
        h.StartRecording();
        root.OnFixedUpdate(7200);
        CHECK_FALSE(h.Recording());
        auto count = h.Recorder().GetTotalFrameCount();
        root.OnFixedUpdate(1);
        CHECK(h.Recorder().GetTotalFrameCount() == count);
        h.Recorder().WaitForFlush();
        h.ServiceRecording();
        h.Recorder().WaitForFlush();
        h.ServiceRecording();
        CHECK(fs::exists(fs::path("recordings/SF_Telem") / session / "scene.bin"));
        CHECK_FALSE(h.RecordingDirty());
        root.OnDetach();
    }
}
TEST_CASE("WO-06 D05: stop queues final export without blocking on pending autosave")
{
    Gate gate;
    std::promise<void> stopping, stopped;
    auto owner = std::async(std::launch::async, [&] {
        Workspace::SF_Telem root(std::make_unique<FakeSerialTransport>());
        root.InitializeServices();
        root.SetScreen(Workspace::SF_Telem::SCREEN_MAIN);
        auto &h = root.Hub();
        h.SetSessionName("wo06-queued");
        h.StartRecording();
        root.OnFixedUpdate(1);
        h.Recorder().SetFlushWriteBarrier([&] { gate.Wait(); });
        h.Recorder().Flush(Scratch("queued").string(), "old");
        gate.entered.get_future().wait();
        root.OnFixedUpdate(1);
        stopping.set_value();
        h.StopRecording();
        stopped.set_value();
        gate.ready.wait();
        root.OnDetach();
    });
    // The entire owning root must stay on its owner thread; no cross-thread calls.
    stopping.get_future().wait();
    auto result = stopped.get_future().wait_for(std::chrono::milliseconds(250));
    CHECK(result == std::future_status::ready);
    gate.release.set_value();
    owner.get();
    DataPlayer p;
    REQUIRE(p.Load("recordings/SF_Telem/wo06-queued"));
    CHECK(p.GetDuration() == 1);
}
TEST_CASE("WO-06 D05: CSV failure preserves last complete snapshot")
{
    auto dir = Scratch("csv-failure");
    DataRecorder r;
    auto id = r.Register("A", "tag", {"x"});
    r.Record(id, {1});
    r.Flush(dir.string(), "save");
    r.WaitForFlush();
    auto old = Bytes(dir / "save/scene.bin");
    fs::remove(dir / "save/A.csv");
    fs::create_directory(dir / "save/A.csv");
    r.Tick(1);
    r.Record(id, {2});
    r.Flush(dir.string(), "save");
    r.WaitForFlush();
    CHECK(Bytes(dir / "save/scene.bin") == old);
}
TEST_CASE("WO-06 D05: manual monitoring export failure remains dirty")
{
    Workspace::SF_Telem root(std::make_unique<FakeSerialTransport>());
    root.InitializeServices();
    root.SetScreen(Workspace::SF_Telem::SCREEN_MAIN);
    auto &hub = root.Hub();
    hub.SetSessionName("wo06-monitor-export");
    root.OnFixedUpdate(1);
    CHECK_FALSE(hub.RecordingDirty());
    SetWriteFaultForTesting(
        [](const std::string &, const char *stage) { return std::string(stage) == "write"; });
    hub.ExportRecording();
    hub.Recorder().WaitForFlush();
    SetWriteFaultForTesting({});
    root.OnUpdate(0);
    CHECK(hub.RecordingDirty());
    CHECK(hub.RecordingStatus().find("FAILED") != std::string::npos);
    root.OnDetach();
    DataPlayer p;
    CHECK(p.Load("recordings/SF_Telem/wo06-monitor-export"));
}
TEST_CASE("WO-06 D06: unsafe writer parameters are rejected before touching output")
{
    auto path = Scratch("csv-writer") / "a.csv";
    Text(path, "last good");
    CHECK_FALSE(DataExport::WriteCSV(path.string(), {"a,b"}, {{1}}));
    CHECK(Bytes(path) == "last good");
    CHECK_FALSE(DataExport::WriteCircularBuffer(path.string(), {"a"}, {nullptr}, -1, 0, 0));
    CHECK(Bytes(path) == "last good");
}
