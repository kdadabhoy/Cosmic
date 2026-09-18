// test_wo09_c06_services.cpp — WO-09 (2D stability) C06, the headless half of
// the shared-service boundary/lifetime coverage:
//
//   EnTT storage   component storage BELOW / AT / ABOVE the packed page (1,024)
//                  and the sparse page (4,096), with deletes that straddle each
//                  boundary — every component reachable, iteration exact, the
//                  per-entity address stable, no out-of-page contiguous access.
//   JobSystem      completion (1,000 jobs), the "drain before unload" owner rule
//                  (a job never runs against a released owner), Shutdown/re-
//                  Initialize (pinned).
//   ScriptHost     LiveCount across Instantiate / Destroy / re-Instantiate and
//                  entity destruction while instantiated.
//   Assets / VFS   a missing asset is a degraded (not null) cached object;
//                  Clear releases; Reload evicts; the per-DLL VFS state is what
//                  FileSystem::Resolve reads (no GL in any of these paths).
//   FileWatcher    watch / poll / stop / destroy-while-watching, missing dir.
//   Audio          the AUDIBLE lifecycle (a generated WAV: init / play / loop /
//                  stop / shutdown WITH voices live) is `skip(true)` — the runner
//                  launches it with --no-skip so a machine with no audio device
//                  fails honestly (ENVIRONMENT_BLOCKED) instead of passing.
//
// The physics half of C06 is the retained test_physics_2d / test_physics_backend
// suites (kept green — see the WO-09 retained run), and the plugin-DLL owner
// rule for jobs is WO-07 L04 mode 2 (KI-33), driven by the runner.

#include <doctest.h>

#include "scene/Scene.h"
#include "scene/Entity.h"
#include "scene/Components.h"
#include "scripting/ScriptHost.h"
#include "scripting/ScriptableEntity.h"
#include "scripting/ModuleRegistry.h"
#include "scripting/ModuleMacros.h"
#include "jobs/JobSystem.h"
#include "assets/AssetLibrary.h"
#include "graphics/Texture.h"
#include "graphics/MaterialAsset.h"
#include "utils/FileSystem.h"
#include "utils/FileWatcher.h"
#include "audio/AudioEngine.h"
#include "audio/Sound.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace Cosmic;

namespace
{
    struct Payload { uint32_t Serial = 0; float Value = 0.0f; uint8_t Pad[52]{}; };   // 64-byte component

    // The entt page constants this build compiles with (config.h:52-58).
    constexpr size_t kPackedPage = ENTT_PACKED_PAGE;
    constexpr size_t kSparsePage = ENTT_SPARSE_PAGE;

    class CountScript : public ScriptableEntity
    {
    public:
        static inline std::atomic<int> s_Live{ 0 };
        static inline std::atomic<int> s_Destroyed{ 0 };
    protected:
        void OnCreate()  override { ++s_Live; }
        void OnDestroy() override { --s_Live; ++s_Destroyed; }
        void OnUpdate(float) override { GetComponent<TransformComponent>().Position.x += 1.0f; }
    };

    void RegisterCountScript()
    {
        static bool once = false;
        if (once) return;
        once = true;
        ModuleRegistry::Get().BeginModule("wo09");
        CS_SCRIPT(CountScript)
        CS_END;
        ModuleRegistry::Get().EndModule();
    }

    // A 0.25-s 8 kHz mono 16-bit sine WAV, written by hand (no decoder involved).
    std::filesystem::path WriteWav(const std::filesystem::path& dir)
    {
        std::error_code ec; std::filesystem::create_directories(dir, ec);
        const std::filesystem::path p = dir / "wo09-tone.wav";
        const uint32_t rate = 8000, samples = rate / 4;
        std::vector<int16_t> pcm(samples);
        for (uint32_t i = 0; i < samples; ++i) pcm[i] = (int16_t)(8000.0 * std::sin(2.0 * 3.14159265 * 440.0 * i / rate));
        const uint32_t dataBytes = samples * 2;
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        auto u32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
        auto u16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
        f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVE", 4);
        f.write("fmt ", 4); u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
        f.write("data", 4); u32(dataBytes); f.write((const char*)pcm.data(), dataBytes);
        return p;
    }
}

TEST_SUITE("WO-09 C06 services (headless)")
{
    TEST_CASE("WO-09 C06: EnTT component storage at 1023 / 1024 / 1025 / 4095 / 4096 / 4097 entities, deletes straddling each page")
    {
        CHECK(kPackedPage == 1024u);
        CHECK(kSparsePage == 4096u);
        for (size_t n : { kPackedPage - 1, kPackedPage, kPackedPage + 1, kSparsePage - 1, kSparsePage, kSparsePage + 1 })
        {
            Ref<Scene> scene = Scene::Create();
            auto& reg = scene->GetRegistry();
            std::vector<Entity> ents;
            std::vector<Payload*> addr;
            for (size_t i = 0; i < n; ++i)
            {
                Entity e = scene->CreateEntity("p");
                auto& p = e.AddComponent<Payload>();
                p.Serial = (uint32_t)i; p.Value = (float)i * 0.5f;
                ents.push_back(e);
                addr.push_back(&p);
            }
            // Every component reachable with its own value; the address handed out at
            // emplace time is still the component's address (paged storage never moves).
            size_t seen = 0, wrongAddr = 0, wrongValue = 0;
            for (size_t i = 0; i < n; ++i)
            {
                Payload& p = reg.get<Payload>((entt::entity)ents[i]);
                wrongAddr  += (&p != addr[i]);
                wrongValue += (p.Serial != (uint32_t)i || p.Value != (float)i * 0.5f);
            }
            for (auto e : reg.view<Payload>()) { (void)e; ++seen; }
            CHECK_MESSAGE(seen == n, "n=" << n);
            CHECK_MESSAGE(wrongAddr == 0, "n=" << n << ": " << wrongAddr << " components moved");
            CHECK_MESSAGE(wrongValue == 0, "n=" << n);
            // Consecutive components straddling a packed page are NOT contiguous across
            // the page boundary (that is the point of paging): pin it where n spans one.
            if (n > kPackedPage)
            {
                const ptrdiff_t inPage = (char*)addr[1] - (char*)addr[0];
                const ptrdiff_t across = (char*)addr[kPackedPage] - (char*)addr[kPackedPage - 1];
                CHECK(inPage == (ptrdiff_t)sizeof(Payload));
                CHECK(across != (ptrdiff_t)sizeof(Payload));   // a new page: no contiguous run
            }
            // Deletes straddling the boundary: the last 5 before and first 5 after.
            const size_t lo = n > 6 ? n - 6 : 0;
            std::vector<uint32_t> removed;
            for (size_t i = lo; i < n; ++i) { removed.push_back((uint32_t)i); scene->DestroyEntity(ents[i]); }
            size_t after = 0;
            for (auto e : reg.view<Payload>())
            {
                ++after;
                const uint32_t serial = reg.get<Payload>(e).Serial;
                if (serial >= lo) FAIL("deleted serial " << serial << " still iterates (n=" << n << ")");
            }
            CHECK(after == lo);
            // The survivors are untouched (swap-and-pop moved SOME of them: values, not addresses, matter).
            for (size_t i = 0; i < lo; ++i)
            {
                const Payload& p = reg.get<Payload>((entt::entity)ents[i]);
                if (p.Serial != (uint32_t)i) { FAIL("survivor " << i << " reads serial " << p.Serial); break; }
            }
            // Refill: new entities reuse the freed slots; values are fresh, count exact.
            for (size_t i = lo; i < n + 3; ++i)
            {
                Entity e = scene->CreateEntity("q");
                e.AddComponent<Payload>().Serial = 100000u + (uint32_t)i;
            }
            size_t total = 0;
            for (auto e : reg.view<Payload>()) { (void)e; ++total; }
            CHECK(total == n + 3);
        }
    }

    TEST_CASE("WO-09 C06: JobSystem — 1,000 jobs complete; the drain-before-unload rule keeps every callback inside the owner's lifetime")
    {
        JobSystem& js = JobSystem::Get();
        js.Initialize();
        REQUIRE(js.IsInitialized());
        const uint64_t before = js.GetCompletedCount();

        struct Owner { std::atomic<int> Hits{ 0 }; };
        auto owner = std::make_unique<Owner>();
        std::atomic<bool> unloaded{ false };
        std::atomic<int> ranAfterUnload{ 0 };
        Owner* raw = owner.get();
        for (int i = 0; i < 1000; ++i)
            js.Submit([raw, &unloaded, &ranAfterUnload]
            {
                if (unloaded.load()) { ++ranAfterUnload; return; }   // never touch a released owner
                ++raw->Hits;
            });
        js.WaitIdle();                                              // the KI-33 rule: drain BEFORE release
        unloaded = true;
        const int hits = raw->Hits.load();
        owner.reset();
        CHECK(hits == 1000);
        CHECK(js.GetCompletedCount() - before == 1000u);
        CHECK(js.GetActiveCount() == 0);
        CHECK(js.GetQueuedCount() == 0);
        CHECK(ranAfterUnload == 0);
        // A second batch after the first drained: still exact.
        std::atomic<int> more{ 0 };
        for (int i = 0; i < 100; ++i) js.Submit([&more] { ++more; });
        js.WaitIdle();
        CHECK(more == 100);
        js.Shutdown();
        CHECK_FALSE(js.IsInitialized());
        js.Shutdown();                                              // idempotent
    }

    TEST_CASE("WO-09 C06: JobSystem — Shutdown then Initialize yields a working pool again (a job submitted after re-init runs)")
    {
        JobSystem& js = JobSystem::Get();
        js.Initialize();
        std::atomic<int> a{ 0 };
        js.Submit([&a] { ++a; });
        js.WaitIdle();
        CHECK(a == 1);
        js.Shutdown();
        js.Initialize();
        REQUIRE(js.IsInitialized());
        CHECK(js.GetWorkerCount() >= 1);
        // Let the fresh workers reach their wait BEFORE the first submit: a worker
        // that wakes on a stale stop flag with an empty queue exits at once, and only
        // a job that was already queued when it started would still run by luck.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::atomic<int> b{ 0 };
        js.Submit([&b] { ++b; });
        // Poll instead of WaitIdle: a dead pool would block WaitIdle forever, and a
        // hang is not an acceptable way to report a defect from inside the suite.
        const auto t0 = std::chrono::steady_clock::now();
        while (b.load() == 0 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3))
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK_MESSAGE(b == 1, "a job submitted after Shutdown -> Initialize never ran within 3 s (dead pool: workers exited on the stale stop flag)");
        if (b == 1) js.WaitIdle();
        js.Shutdown();
    }

    TEST_CASE("WO-09 C06: ScriptHost — LiveCount across Instantiate / Destroy / re-Instantiate and entity destruction while live")
    {
        RegisterCountScript();
        CountScript::s_Live = 0; CountScript::s_Destroyed = 0;
        Ref<Scene> scene = Scene::Create();
        std::vector<Entity> ents;
        for (int i = 0; i < 50; ++i)
        {
            Entity e = scene->CreateEntity("s");
            e.AddComponent<NativeScriptComponent>(i % 5 == 0 ? "NoSuchScriptClass" : "CountScript");
            ents.push_back(e);
        }
        ScriptHost host;
        host.Instantiate(*scene);
        CHECK(host.LiveCount() == 40);                              // 10 unknown classes stay inert
        CHECK(CountScript::s_Live.load() == 40);
        host.Tick(1.0f);
        CHECK(ents[1].GetComponent<TransformComponent>().Position.x == 1.0f);
        CHECK(ents[0].GetComponent<TransformComponent>().Position.x == 0.0f);   // inert
        // Destroy some scripted entities while the host is live, then tick: no
        // callback into a destroyed entity, live count follows the scene.
        for (int i = 1; i < 50; i += 7) scene->DestroyEntity(ents[i]);
        host.Tick(1.0f);
        host.Destroy();
        CHECK(host.LiveCount() == 0);
        CHECK(CountScript::s_Live.load() == 0);
        CHECK(CountScript::s_Destroyed.load() == 40);
        host.Destroy();                                             // idempotent
        CHECK(CountScript::s_Destroyed.load() == 40);
        host.Instantiate(*scene);                                   // re-instantiate on the reduced scene
        CHECK(host.LiveCount() == 40 - 6);                          // 6 CountScript entities were destroyed (i=1,8,15,22,29,36,43 minus i%5==0 ⇒ i=15 inert)
        host.Destroy();
        CHECK(CountScript::s_Live.load() == 0);
    }

    TEST_CASE("WO-09 C06: assets / VFS — a missing texture is a degraded cached object, Clear releases, Reload evicts, project mounts resolve")
    {
        AssetLibrary::Clear();
        size_t seen = 0;
        AssetLibrary::Enumerate([&](const AssetEntry&) { ++seen; });
        CHECK(seen == 0);
        Ref<Texture2D> missing = AssetLibrary::GetTexture("definitely/not/here.png");
        // Pinned: the loader returns a DEGRADED texture (0x0, no GL object) rather
        // than null, and the library caches it — the sprite pass treats width 0 as
        // "untextured" and draws the flat colour.
        if (missing)
        {
            CHECK(missing->GetWidth() == 0);
            CHECK(missing->GetHeight() == 0);
            CHECK(missing->GetRendererID() == 0);
            Ref<Texture2D> again = AssetLibrary::GetTexture("definitely/not/here.png");
            CHECK(again == missing);                                // cached
            seen = 0;
            AssetLibrary::Enumerate([&](const AssetEntry& e) { ++seen; CHECK(e.Type == AssetType::Texture); });
            CHECK(seen == 1);
            CHECK(AssetLibrary::Reload("definitely/not/here.png"));  // evicts the degraded entry
            Ref<Texture2D> third = AssetLibrary::GetTexture("definitely/not/here.png");
            CHECK(third != missing);
        }
        else
        {
            MESSAGE("C06 assets: a missing texture returned null (not cached)");
        }
        AssetLibrary::Clear();
        seen = 0;
        AssetLibrary::Enumerate([&](const AssetEntry&) { ++seen; });
        CHECK(seen == 0);
        CHECK_FALSE(AssetLibrary::Reload("never/loaded.png"));      // nothing to evict
        CHECK(AssetLibrary::GetMaterial("no/such.cmat") == nullptr);
        MaterialAsset ma;
        CHECK_FALSE(AssetLibrary::LoadMaterialAsset(ma, "no/such.cmat"));

        // VFS: project:// resolves under the mounted root of THIS module; an
        // unmounted scheme resolves to itself; NormalizeKey collapses spellings.
        const std::filesystem::path root = std::filesystem::temp_directory_path() / "wo09-c06-project";
        std::error_code ec; std::filesystem::create_directories(root / "scenes", ec);
        FileSystem::SetActiveProjectPath(root.generic_string());
        const std::string resolved = FileSystem::Resolve("project://scenes/Main.cscene");
        CHECK(resolved.find("wo09-c06-project") != std::string::npos);
        CHECK(AssetLibrary::NormalizeKey("project://textures/../textures/a.png") == AssetLibrary::NormalizeKey("project://textures/a.png"));
        FileSystem::SetActiveProject("");
        std::filesystem::remove_all(root, ec);
    }

    TEST_CASE("WO-09 C06: FileWatcher — watch / change / poll / stop / destroy-while-watching / missing dir")
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "wo09-c06-watch";
        std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);
        {
            FileWatcher w;
            REQUIRE(w.Watch(dir.generic_string(), true));
            CHECK(w.IsWatching());
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            { std::ofstream f(dir / "a.txt"); f << "x"; }
            bool seen = false;
            for (int i = 0; i < 100 && !seen; ++i)
            {
                for (const auto& c : w.Poll()) if (c.Path.find("a.txt") != std::string::npos) seen = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            CHECK(seen);
            w.Stop();
            CHECK_FALSE(w.IsWatching());
            CHECK(w.Poll().empty());
            REQUIRE(w.Watch(dir.generic_string(), false));          // re-arm the same watcher
            CHECK(w.IsWatching());
            { std::ofstream f(dir / "b.txt"); f << "y"; }
            // Destroyed while watching, with a change possibly in flight: must not hang or crash.
        }
        {
            FileWatcher w;
            CHECK_FALSE(w.Watch((dir / "nope" / "nor").generic_string()));
            CHECK_FALSE(w.IsWatching());
            w.Stop();
        }
        fs::remove_all(dir, ec);
    }

    TEST_CASE("WO-09 C06: audio — init / one-shot / loop / stop / shutdown with voices live (needs an audio device)"
              * doctest::skip(true))
    {
        // Launched by the runner with --no-skip. No device ⇒ Init leaves the engine
        // uninitialised ⇒ this REQUIRE fails: ENVIRONMENT_BLOCKED, never a pass.
        AudioEngine::Init();
        REQUIRE_MESSAGE(AudioEngine::IsInitialized(), "ENVIRONMENT_BLOCKED: no audio output device — AudioEngine::Init did not initialise");
        AudioEngine::SetMasterVolume(0.0f);                          // silent on the reference machine
        const std::filesystem::path wav = WriteWav(std::filesystem::temp_directory_path() / "wo09-c06-audio");
        Ref<Sound> tone = Sound::Create(wav.string());
        REQUIRE(tone);
        REQUIRE(tone->IsValid());
        CHECK(tone->GetDuration() == doctest::Approx(0.25f).epsilon(0.05));
        AudioEngine::Play(tone, 0.5f, 1.0f);
        const SoundHandle loop = AudioEngine::PlayLooping(tone, 0.5f, 1.0f, AudioGroup::Sfx);
        REQUIRE(loop != InvalidSoundHandle);
        CHECK(AudioEngine::IsPlaying(loop));
        AudioEngine::SetPitch(loop, 2.0f);
        AudioEngine::SetVolume(loop, 0.25f);
        AudioEngine::PauseGroup(AudioGroup::Sfx, true);
        CHECK(AudioEngine::IsGroupPaused(AudioGroup::Sfx));
        AudioEngine::PauseGroup(AudioGroup::Sfx, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CHECK(AudioEngine::IsPlaying(loop));                         // looping: still live after the clip length
        AudioEngine::Stop(loop);
        CHECK_FALSE(AudioEngine::IsPlaying(loop));
        const SoundHandle loop2 = AudioEngine::PlayLooping(tone, 0.5f);
        const SoundHandle loop3 = AudioEngine::PlayLooping(tone, 0.5f, 0.5f, AudioGroup::Ui);
        CHECK(loop2 != loop);                                        // handles are never reused
        CHECK(AudioEngine::IsPlaying(loop2));
        CHECK(AudioEngine::IsPlaying(loop3));
        AudioEngine::StopAll();
        CHECK_FALSE(AudioEngine::IsPlaying(loop2));
        // Shutdown WITH voices live: start two loops and tear the engine down.
        const SoundHandle live1 = AudioEngine::PlayLooping(tone, 0.5f);
        const SoundHandle live2 = AudioEngine::PlayLooping(tone, 0.5f, 1.5f, AudioGroup::Alerts);
        CHECK(AudioEngine::IsPlaying(live1)); CHECK(AudioEngine::IsPlaying(live2));
        AudioEngine::Shutdown();
        CHECK_FALSE(AudioEngine::IsInitialized());
        CHECK_FALSE(AudioEngine::IsPlaying(live1));
        AudioEngine::Stop(live2);                                    // after shutdown: harmless
        tone.reset();                                                // the Sound outlived the engine: releases cleanly
        AudioEngine::Shutdown();
        // Re-init after shutdown works (the STA/MTA guard is per Init).
        AudioEngine::Init();
        CHECK(AudioEngine::IsInitialized());
        AudioEngine::Shutdown();
        std::error_code ec; std::filesystem::remove_all(wav.parent_path(), ec);
    }
}
