#include <doctest.h>
#include "FakeSerialTransport.h"
#include "../Projects/SF_Telem/src/SF_Telem.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <thread>

using namespace Cosmic;
using namespace Workspace;
namespace
{
    using Clock = std::chrono::steady_clock;
    template<class F> bool Until(F predicate, int ms = 2000)
    {
        auto deadline = Clock::now() + std::chrono::milliseconds(ms);
        while (!predicate() && Clock::now() < deadline) std::this_thread::yield();
        return predicate();
    }
    std::string Frame(char tag, int n = 0)
    {
        char payload[96], line[128];
        std::snprintf(payload, sizeof(payload), "%c,%d,%d,%d,%d,%d", tag,
            25 + n % 10, 1680 + n % 20, 420 + n % 30, 120 + n % 40, 350 + n % 50);
        unsigned checksum = 0; // independent PC XOR oracle, not firmware CRC8
        for (const char* c = payload; *c; ++c) checksum ^= static_cast<unsigned char>(*c);
        std::snprintf(line, sizeof(line), "$%s*%02X\n", payload, checksum);
        return line;
    }
    void VerifyDecoded(TelemHub& hub, int id, int n)
    {
        const double voltage=(1680+n%20)*0.01, current=(420+n%30)*0.01;
        const double erpm=(350+n%50)*100.0;
        const double motor=erpm/(IsDrive(id) ? 6.0 : 3.0);
        const double speed=IsDrive(id) ? motor/19.0/0.933*3.14159265*3.5/1056.0
                                      : motor/4.0*3.14159265*7.874/1056.0;
        const auto actual=IsDrive(id) ? hub.GetDrive(id).ToChannels() : hub.GetWeapon().ToChannels();
        const std::vector<double> expected=IsDrive(id)
            ? std::vector<double>{double(25+n%10),voltage,current,double(120+n%40),erpm,motor,speed,voltage*current}
            : std::vector<double>{double(25+n%10),voltage,current,double(120+n%40),erpm,motor,motor/4.0,speed,voltage*current};
        REQUIRE(actual.size()==expected.size());
        for (size_t channel=0; channel<expected.size(); ++channel)
            CHECK(actual[channel]==doctest::Approx(expected[channel]));
    }
    struct Rig
    {
        FakeSerialTransport* fake;
        std::shared_ptr<FakeSerialTransport::Counters> counts;
        std::unique_ptr<SF_Telem> root;
        Rig()
        {
            auto transport = std::make_unique<FakeSerialTransport>();
            fake = transport.get(); counts = fake->Counts();
            fake->SetAvailablePorts({ "COM_FAKE" });
            root = std::make_unique<SF_Telem>(std::move(transport));
            root->InitializeServices(); root->SetScreen(SF_Telem::SCREEN_MAIN);
            root->OnUpdate(0.0f);
        }
        ~Rig() { if (root) root->OnDetach(); }
        void Connect()
        {
            root->Link().Connect();
            REQUIRE(Until([&] { return root->Link().GetState() == SerialPort::State::Open; }));
            REQUIRE(Until([&] { return counts->reads == 1; }));
            root->OnUpdate(0.0f);
        }
        void Deliver(const std::string& bytes)
        {
            REQUIRE(Until([&] { return counts->reads == 1; }));
            const int calls = counts->readCalls;
            fake->PushBytes(bytes);
            REQUIRE(Until([&] { return counts->readCalls > calls; }));
            root->OnUpdate(0.0f);
        }
        void Balance()
        {
            root.reset();
            REQUIRE(Until([&] { return counts->destroyed.load(); }));
            CHECK(counts->handles == 0); CHECK(counts->opens == 0);
            CHECK(counts->reads == 0); CHECK(counts->writes == 0);
            CHECK(counts->successes.load() == counts->releases.load());
            CHECK(counts->cleanupRaces == 0);
        }
    };
    struct FlushGate
    {
        HANDLE entered = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        HANDLE release = CreateEvent(nullptr, TRUE, FALSE, nullptr);
        std::atomic<int> callbacks{0};
        std::atomic<bool> timeout{false};
        ~FlushGate() { CloseHandle(entered); CloseHandle(release); }
        void Wait()
        {
            ++callbacks; SetEvent(entered);
            if (WaitForSingleObject(release, 10000) != WAIT_OBJECT_0) timeout = true;
            --callbacks;
        }
    };
}

TEST_CASE("WO-05 T03: root shared-link matrix, 100 barrier iterations per crossed schedule")
{
    // These are the real root's service entry points. Actual window event/deferred
    // DLL unload qualification is a separate host case, not inferred from this test.
    const char* schedules[] = { "delayed-success", "delayed-failure", "completed-unadopted-success",
        "pending-read", "silent-stall", "hard-disconnect", "in-flight-write", "reconnect", "closed" };
    const char* transitions[] = { "window-close-OnDetach", "return-launcher-OnDetach",
        "connection-controls-hide", "screen-switch", "explicit-disconnect" };
    const char* storage[] = { "monitor", "recording", "export-pending", "autosave-pending", "replay-loaded" };
    long long maxCloseMs = 0;
    for (int schedule = 0; schedule < 9; ++schedule)
    for (int transition = 0; transition < 5; ++transition)
    for (int state = 0; state < 5; ++state)
    for (int iteration = 0; iteration < 100; ++iteration)
    {
        CAPTURE(schedules[schedule]); CAPTURE(transitions[transition]);
        CAPTURE(storage[state]); CAPTURE(iteration);
        FlushGate gate;
        Rig rig;
        auto& root = *rig.root;
        auto& hub = root.Hub();
        std::thread writer;
        std::thread releaser;
        bool writeResult = true;
        if (schedule < 3)
        {
            rig.fake->SetOpenBarrier();
            rig.fake->SetOpenResult(schedule != 1);
            root.Link().Connect();
            REQUIRE(rig.fake->WaitOpenEntered());
            if (schedule == 2)
            {
                rig.fake->ReleaseOpen();
                REQUIRE(Until([&] { return rig.counts->opens == 0 && rig.counts->handles == 1; }));
                // Deliberately do not call a status accessor: owner has not adopted.
            }
        }
        else
        {
            rig.Connect();
            if (schedule == 4) { root.OnUpdate(1.1f); CHECK_FALSE(root.Link().IsReceiving()); }
            if (schedule == 5 || schedule == 7)
            {
                rig.Deliver(Frame('R'));
                CHECK(root.Link().IsReceiving());
                rig.fake->SignalDrop();
                REQUIRE(Until([&] { return root.Link().GetState() == SerialPort::State::Failed; }));
                if (schedule == 7)
                {
                    rig.fake->SetOpenBarrier();
                    root.OnUpdate(3.0f);
                    REQUIRE(rig.fake->WaitOpenEntered());
                    CHECK(rig.fake->OpenCount() == 2);
                }
            }
            if (schedule == 8) root.Link().Disconnect();
        }
        if (state != 0)
        {
            hub.SetSessionName("wo05-matrix");
            hub.StartRecording();
            root.OnFixedUpdate(1.0f / 60.0f);
            if (state == 2 || state == 3)
            {
                hub.Recorder().SetFlushWriteBarrier([&] { gate.Wait(); });
                if (state == 2) hub.StopRecording();
                else hub.Recorder().Tick(5.0f); // real autosave trigger
                REQUIRE(WaitForSingleObject(gate.entered, 2000) == WAIT_OBJECT_0);
                CHECK(hub.Recorder().IsFlushing()); CHECK(gate.callbacks == 1);
            }
            if (state == 4)
            {
                hub.StopRecording(); hub.Recorder().WaitForFlush();
                REQUIRE(hub.Player().Load("recordings/SF_Telem/wo05-matrix/scene.bin"));
                root.SetScreen(SF_Telem::SCREEN_REPLAY);
                CHECK(hub.Replaying());
            }
        }
        if (schedule == 6)
        {
            rig.fake->SetWritePending();
            writer = std::thread([&] { writeResult = root.Link().Write("in-flight"); });
            CHECK(Until([&] { return rig.counts->writes == 1; }));
        }
        // Panel hiding does not own/cancel the root connection. Switching all
        // five real screens likewise preserves connection intent.
        if (transition == 2)
        {
            // Serial Link has no independent close button (Begin has nullptr).
            // Home actually removes its drawing surface, retaining root ownership.
            root.SetScreen(SF_Telem::SCREEN_HOME);
            CHECK(root.Link().WantConnection() == (schedule != 8));
        }
        if (transition == 3)
        {
            for (int screen = 0; screen < SF_Telem::SCREEN_COUNT; ++screen)
                root.SetScreen(static_cast<SF_Telem::Screen>(screen));
            CHECK(root.Link().WantConnection() == (schedule != 8));
        }
        if (transition == 4) root.Link().Disconnect();
        if (state == 2 || state == 3)
        {
            const int closes = rig.fake->CloseCount();
            releaser = std::thread([&, closes]
            {
                // Release export only after the real serial teardown reaches Close.
                if (!Until([&] { return rig.fake->CloseCount() > closes; })) gate.timeout = true;
                SetEvent(gate.release);
            });
        }
        const auto start = Clock::now();
        root.OnDetach();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count();
        maxCloseMs = (std::max)(maxCloseMs, ms);
        if (writer.joinable()) writer.join();
        if (releaser.joinable()) releaser.join();
        CHECK(ms <= 2000);
        CHECK_FALSE((writeResult && schedule == 6));
        CHECK(root.Link().GetState() == SerialPort::State::Idle);
        CHECK_FALSE(root.Link().WantConnection());
        root.OnUpdate(10.0f); // root/link may not resurrect after teardown
        CHECK(root.Link().GetState() == SerialPort::State::Idle);
        CHECK_FALSE(hub.Recorder().IsFlushing());
        CHECK(gate.callbacks == 0); CHECK_FALSE(gate.timeout);
        if (state != 0)
        {
            DataPlayer saved;
            const char* path = state == 3 ? "recordings/SF_Telem/_autosave/wo05-matrix/scene.bin"
                                         : "recordings/SF_Telem/wo05-matrix/scene.bin";
            REQUIRE(saved.Load(path)); CHECK(saved.GetEntityNames().size() == 3);
            for (int id = 0; id < ESC_COUNT; ++id)
            {
                TelemetryFrame frame;
                REQUIRE(saved.SampleAt(IdEntity(id), 0, frame));
                CHECK(frame.values.size() == (IsDrive(id) ? DCH_COUNT : WCH_COUNT));
                TelemetryFrame expected;
                REQUIRE(hub.Recorder().GetCurrentFrame(IdEntity(id), expected));
                CHECK(frame.values == expected.values);
            }
        }
        rig.Balance();
    }
    std::printf("WO05 matrix: schedules=9 transitions=5 storage=5 iterations=100 total=22500 max_close_ms=%lld\n", maxCloseMs);
}

TEST_CASE("WO-05 T03: non-cooperative late-open success survives destruction, 100 iterations")
{
    for (int iteration = 0; iteration < 100; ++iteration)
    {
        CAPTURE(iteration);
        Rig rig;
        rig.fake->SetOpenBarrier(false); // driver deliberately ignores stop
        rig.root->Link().Connect();
        REQUIRE(rig.fake->WaitOpenEntered());
        const auto start = Clock::now();
        rig.root->OnDetach();
        CHECK(Clock::now() - start < std::chrono::seconds(2));
        rig.root->Link().Connect();
        CHECK(rig.fake->OpenCount() == 1); // no stacked worker during cancellation
        auto counts = rig.counts;
        rig.root.reset(); // worker must not reference this destroyed ownership chain
        CHECK_FALSE(counts->destroyed);
        SetEvent(counts->release);
        REQUIRE(Until([&] { return counts->destroyed.load(); }));
        CHECK(counts->opens == 0); CHECK(counts->reads == 0); CHECK(counts->handles == 0);
        CHECK(counts->successes==1); CHECK(counts->releases==1); CHECK(counts->cleanupRaces==0);
    }
}

TEST_CASE("WO-05 T03: cancel a non-cooperative open keeps the owner responsive, 100 iterations")
{
    std::vector<double> elapsed;
    for (int iteration=0;iteration<100;++iteration)
    {
        Rig rig; rig.fake->SetOpenBarrier(false); rig.root->Link().Connect();
        REQUIRE(rig.fake->WaitOpenEntered());
        const auto start=Clock::now(); rig.root->Link().Disconnect();
        const auto duration=Clock::now()-start;
        elapsed.push_back(std::chrono::duration<double,std::milli>(duration).count());
        CHECK(duration<=std::chrono::milliseconds(250));
        CHECK_FALSE(rig.root->Link().WantConnection());
        const auto counts=rig.counts; rig.root.reset(); SetEvent(counts->release);
        REQUIRE(Until([&] { return counts->destroyed.load(); }));
        CHECK(counts->handles==0); CHECK(counts->opens==0);
        CHECK(counts->successes==1); CHECK(counts->releases==1);
    }
    std::sort(elapsed.begin(),elapsed.end());
    std::printf("WO05 cancel owner ms p95=%.3f p99=%.3f max=%.3f\n",elapsed[94],elapsed[98],elapsed.back());
}

TEST_CASE("WO-05 T03: close drains an app-owned in-flight writer before releasing its session")
{
    Rig rig;
    rig.Connect(); rig.fake->SetWritePending();
    bool result=true;
    std::thread writer([&] { result=rig.root->Link().Write("pending"); });
    CHECK(Until([&] { return rig.counts->writes==1; }));
    rig.root->OnDetach();
    CHECK(rig.counts->writes==0);
    writer.join(); // keep the actual root/link alive even when the old code fails
    CHECK_FALSE(result);
    rig.Balance();
}

TEST_CASE("WO-05 T06: stalled consumer serial receive queue is bounded")
{
    Rig rig; rig.Connect();
    constexpr size_t capacity=1024*1024;
    const size_t bytes=capacity+512;
    const int calls=rig.counts->readCalls;
    rig.fake->PushBytes(std::string(bytes,'x'));
    REQUIRE(Until([&] { return rig.counts->readCalls>=calls+static_cast<int>(bytes/256); }));
    const auto buffered=rig.root->Link().Poll();
    CHECK(buffered.size()<=capacity);
    CHECK(rig.root->Link().ReceivedBytes()==bytes);
    CHECK(rig.root->Link().OverflowBytes()==512);
    CHECK(rig.root->Link().DiscardedOnCloseBytes()==0);
    REQUIRE(Until([&] { return rig.counts->reads==1; }));
    const int closeCalls=rig.counts->readCalls;
    rig.fake->PushBytes(std::string(256,'z'));
    REQUIRE(Until([&] { return rig.counts->readCalls>closeCalls; }));
    rig.root->OnDetach();
    CHECK(rig.root->Link().ReceivedBytes()==bytes+256);
    CHECK(rig.root->Link().OverflowBytes()==512);
    CHECK(rig.root->Link().DiscardedOnCloseBytes()==256);
}

TEST_CASE("WO-05 T06: rapid silent-stall reconnect resets the actual hub accumulator")
{
    Rig rig; rig.Connect();
    rig.Deliver("$R,25,");
    REQUIRE(rig.root->Hub().PendingTextBytes()==6);
    // The root-owned link sees no closed frame: real policy starts the retry,
    // worker completes, then the next root update observes Open again.
    rig.root->Link().OnUpdate(3.0f);
    REQUIRE(Until([&] { return rig.root->Link().GetState()==SerialPort::State::Open; }));
    rig.root->OnUpdate(0.0f);
    CHECK(rig.root->Hub().PendingTextBytes()==0);
    rig.Deliver(Frame('R'));
    CHECK(rig.root->Hub().GoodFrames()==1); CHECK(rig.root->Hub().BadFrames()==0);
}

TEST_CASE("WO-05 T01: PC text fixture every split, bytewise, seeded and coalesced routing")
{
    const std::string fixture = Frame('R') + Frame('L', 1) + Frame('W', 2);
    for (size_t split = 0; split <= fixture.size(); ++split)
    {
        Rig rig;
        rig.root->Hub().IngestChunk(fixture.substr(0, split));
        rig.root->Hub().IngestChunk(fixture.substr(split));
        CHECK(rig.root->Hub().GoodFrames() == 3); CHECK(rig.root->Hub().BadFrames() == 0);
        for (int id = 0; id < ESC_COUNT; ++id)
        {
            CHECK(rig.root->Hub().PacketCount(id) == 1);
            CHECK(rig.root->Hub().Volt(id) == doctest::Approx((1680+id)*0.01));
            CHECK(rig.root->Hub().Cur(id) == doctest::Approx((420+id)*0.01));
            VerifyDecoded(rig.root->Hub(), id, id);
        }
    }
    for (int mode = 0; mode < 3; ++mode)
    {
        Rig rig;
        std::mt19937 random(5);
        for (size_t pos = 0; pos < fixture.size();)
        {
            size_t len = mode == 0 ? 1 : mode == 1 ? 1 + random()%19 : fixture.size();
            len = (std::min)(len, fixture.size()-pos);
            rig.root->Hub().IngestChunk(fixture.substr(pos, len)); pos += len;
        }
        CHECK(rig.root->Hub().GoodFrames() == 3); CHECK(rig.root->Hub().BadFrames() == 0);
    }
}

TEST_CASE("WO-05 T02: exact purge boundary, seeded faults, recovery and 1.5-second staleness")
{
    Rig rig;
    auto& hub = rig.root->Hub();
    hub.IngestChunk(std::string(4096, 'x')); CHECK(hub.PendingTextBytes() == 4096);
    hub.IngestChunk("x"); CHECK(hub.PendingTextBytes() == 0);
    hub.IngestChunk(Frame('R')); CHECK(hub.GoodFrames() == 1);
    rig.root->OnUpdate(1.5f); CHECK_FALSE(hub.Stale(ESC_RIGHT));
    rig.root->OnUpdate(0.0001f); CHECK(hub.Stale(ESC_RIGHT));
    std::mt19937 random(5);
    for (int n = 0; n < 1000; ++n)
    {
        auto corrupt = Frame('L');
        corrupt[3 + random()%12] ^= 0x40; // one payload bit; XOR must reject
        hub.IngestChunk(corrupt);
        CHECK(hub.PendingTextBytes() <= 4096);
    }
    CHECK(hub.BadFrames() == 1000); CHECK(hub.PacketCount(ESC_LEFT) == 0);
    hub.IngestChunk("\n" + Frame('W')); // known delimiter recovery
    CHECK(hub.GoodFrames() == 2); CHECK(hub.PacketCount(ESC_WEAPON) == 1);
}

TEST_CASE("WO-05 T05: controlled 30-minute 40-Hz per-ESC acquisition and 60-Hz recording")
{
    Rig rig;
    auto& hub = rig.root->Hub();
    // Preserve the recorded data in memory; dirty-save durability is WO-06.
    hub.SetSessionName("wo05-long"); hub.StartRecording();
    for (int step = 0; step < 1800*120; ++step)
    {
        if (step%3 == 0)
        {
            const int n = step/3;
            hub.IngestChunk(Frame('R', n) + Frame('L', n) + Frame('W', n));
            for (int id = 0; id < ESC_COUNT; ++id)
            {
                VerifyDecoded(hub, id, n);
            }
        }
        rig.root->OnUpdate(1.0f/120.0f);
        if (step%2 == 0) rig.root->OnFixedUpdate(1.0f/60.0f);
    }
    CHECK(hub.GoodFrames() == 216000); CHECK(hub.BadFrames() == 0);
    CHECK(hub.Recorder().GetTotalFrameCount() == 108000);
    for (int id = 0; id < ESC_COUNT; ++id)
    {
        CHECK(hub.PacketCount(id) == 72000);
        CHECK(hub.PlotRing(id).count == TelemHub::Ring::Cap);
    }
    hub.StopRecording(); hub.Recorder().WaitForFlush();
}

TEST_CASE("WO-05 T05: screen and paused-update acquisition policy uses the shared root")
{
    Rig rig; rig.Connect(); auto& root=*rig.root; auto& hub=root.Hub();
    rig.Deliver(Frame('R')); CHECK(hub.GoodFrames()==1);
    root.SetScreen(SF_Telem::SCREEN_HOME);
    const int calls=rig.counts->readCalls;
    rig.fake->PushBytes(Frame('L'));
    REQUIRE(Until([&] { return rig.counts->readCalls>calls; }));
    root.OnUpdate(0.0f); CHECK(hub.GoodFrames()==1); // Home retains, does not drain
    root.SetScreen(SF_Telem::SCREEN_MAIN);
    root.OnUpdate(0.0f); CHECK(hub.GoodFrames()==2);
    hub.SetSessionName("wo05-policy"); hub.StartRecording(); root.OnFixedUpdate(1.0f/60.0f);
    CHECK(hub.Recorder().GetTotalFrameCount()==1);
    rig.Deliver(Frame('W')); // paused host still calls variable update with dt=0
    CHECK(hub.GoodFrames()==3); CHECK(hub.Recorder().GetTotalFrameCount()==1);
    root.SetScreen(SF_Telem::SCREEN_ANALYSIS); root.OnFixedUpdate(1.0f/60.0f);
    CHECK(hub.Recorder().GetTotalFrameCount()==1);
    root.SetScreen(SF_Telem::SCREEN_MAIN);
    hub.StopRecording(); hub.Recorder().WaitForFlush();
    REQUIRE(hub.Player().Load("recordings/SF_Telem/wo05-policy/scene.bin"));
    root.SetScreen(SF_Telem::SCREEN_REPLAY); root.OnUpdate(0.0f);
    const float replayVoltage=hub.Volt(ESC_RIGHT);
    rig.Deliver(Frame('R',9));
    CHECK(hub.Replaying()); CHECK(hub.Volt(ESC_RIGHT)==replayVoltage);
    CHECK(hub.GoodFrames()==3);
    root.SetScreen(SF_Telem::SCREEN_MAIN); root.OnUpdate(0.0f);
    CHECK(hub.GoodFrames()==4); CHECK(hub.Volt(ESC_RIGHT)==doctest::Approx(16.89));
}

TEST_CASE("WO-05 T06: 10x bursts, silence and 20 real-link reconnects discard partial bytes")
{
    Rig rig;
    auto& hub = rig.root->Hub();
    for (int second = 0; second < 60; ++second)
    {
        std::string burst;
        for (int n = 0; n < 400; ++n) burst += Frame('R', n)+Frame('L', n)+Frame('W', n);
        hub.IngestChunk(burst);
        CHECK(hub.PendingTextBytes() == 0);
    }
    CHECK(hub.GoodFrames() == 72000); CHECK(hub.BadFrames() == 0);
    rig.root->OnUpdate(1.0f); CHECK_FALSE(hub.Stale(ESC_RIGHT));
    rig.root->OnUpdate(10.0f); CHECK(hub.Stale(ESC_RIGHT));
    rig.Connect();
    for (int cycle = 0; cycle < 20; ++cycle)
    {
        rig.Deliver("$R,25,"); CHECK(hub.PendingTextBytes() > 0);
        rig.fake->SignalDrop();
        REQUIRE(Until([&] { return rig.root->Link().GetState() == SerialPort::State::Failed; }));
        rig.root->OnUpdate(3.0f);
        REQUIRE(Until([&] { return rig.root->Link().GetState() == SerialPort::State::Open; }));
        rig.root->OnUpdate(0.0f);
        CHECK(hub.PendingTextBytes() == 0);
        rig.Deliver(Frame('W'));
    }
    hub.IngestChunk("$R,25,1,1,1,1*XX\n"+Frame('L'));
    CHECK(hub.GoodFrames() == 72021); CHECK(hub.BadFrames() == 1);
    rig.root->OnDetach(); rig.Balance();
}
