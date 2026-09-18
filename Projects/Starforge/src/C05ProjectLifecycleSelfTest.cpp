// C05ProjectLifecycleSelfTest.cpp — WO-09 (2D stability): the C05 real-project
// lifecycle acceptance, driven inside the REAL Starforge editor.
//
// When armed by COSMIC_C05_SELFTEST=<result-file> the harness, from inside the
// editor and through the editor's OWN commands (never the serializer or the
// registry directly, except to read back what a command did):
//   1. CREATE  — scaffolds a real project (NewProjectAt), then creates a sprite, a
//                tilemap (painted through TileEdit), a canvas with a button, and a
//                2D light through Commands::Create / AddComponent / SetField;
//   2. IMPORT  — writes a PNG asset into project://textures and references it from
//                the sprite (the asset resolves when the viewport renders it), and
//                imports the F-CONTENT prefab (a sprite carrying a 3D MeshRenderer
//                block and an unknown FutureThing block, plus a child) through the
//                K13 drop path: SceneSerializer::InstantiatePrefab + RecordSpawn;
//   3. SAVE    — SaveScene (the real Save path);
//   4. REOPEN  — CloseProject + OpenProjectPath (the scene reloads from disk): every
//                entity is found again by UUID with every field intact, the imported
//                3D/unknown blocks are still opaque and verbatim, the parent links
//                hold, and nothing points at a UUID that no longer exists;
//   5. PLAY    — PlayScene, N frames ticked (the runtime scene is a snapshot), then
//   6. STOP    — StopScene: the edit scene is the untouched original (same UUIDs,
//                same fields), no runtime state leaked;
//   7. UNDO / REDO — Commands::Destroy on the sprite, Undo (back with the SAME UUID
//                and fields), Redo (gone), Undo (back); then a SetField, Undo, Redo;
//   8. DELETE  — Commands::Destroy on the tilemap (kept deleted), Save.
// It then writes an expected table next to the result so the runner wrapper can
// parse scenes/Main.cscene OUT OF PROCESS (an independent JSON reader, never the
// engine serializer) and compare. Every step is timed; the parse deadline is the
// U-case 10 s.
//
// Verdict -> exit code: PASS -> graceful close (main returns 0); FAIL ->
// quick_exit(1). A JSON result is always written.

#include "StarforgeApp.h"
#include "commands/EditorCommands.h"

#include <Cosmic.h>
#include "scene/SceneSerializer.h"
#include "scene/ui/UiComponents.h"
#include "utils/ImageIO.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#if defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace fs = std::filesystem;

namespace Starforge
{
    namespace
    {
        using Cosmic::Reflect::FieldValue;

        std::string Esc(const std::string& s)
        {
            std::string o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; if (c == '\n') { o += "\\n"; continue; } o += c; } return o;
        }
        bool WriteFile(const fs::path& p, const std::string& text)
        {
            std::error_code ec; fs::create_directories(p.parent_path(), ec);
            std::ofstream out(p, std::ios::binary | std::ios::trunc); out << text; return (bool)out;
        }
        std::string ReadFile(const fs::path& p)
        {
            std::ifstream in(p, std::ios::binary); std::stringstream ss; ss << in.rdbuf(); return ss.str();
        }
        const Cosmic::Reflect::TypeDescriptor* Desc(const char* name) { return Cosmic::Reflect::GetRegistry().FindByName(name); }

        // The values the harness sets and later expects (the expected table).
        const glm::vec3 kSpritePos{ 3.0f, 4.0f, 0.0f };
        const int32_t   kSpriteZ = 7;
        const float     kLampRadius = 3.5f;
        const char*     kTexVfs = "project://textures/c05.png";
    }

    struct StarforgeApp::C05LifecycleSelfTest
    {
        enum Phase { Boot, Create, Import, Save, Reopen, Play, Playing, Stop, UndoRedo, Delete, Finish, Done };
        Phase phase = Boot;
        int   frames = 0, playFrames = 0;
        std::string resultPath, projectRoot, projectDir, prefabFixture;
        std::chrono::steady_clock::time_point runStart = std::chrono::steady_clock::now();

        Cosmic::UUID spriteId, mapId, canvasId, buttonId, lampId, importedId, importedChildId;
        std::string importedOpaqueFuture, importedOpaqueMesh;   // verbatim text at import time
        size_t entitiesBeforeReopen = 0;

        int failures = 0;
        std::vector<std::string> log, checks, steps;
        void note(const char* fmt, ...)
        {
            char buf[768]; va_list ap; va_start(ap, fmt); _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
            log.emplace_back(buf); std::printf("[C05] %s\n", buf); std::fflush(stdout);
        }
        void fail(const char* fmt, ...)
        {
            char buf[768]; va_list ap; va_start(ap, fmt); _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
            ++failures; checks.emplace_back(buf);
            log.emplace_back(std::string("FAIL ") + buf); std::printf("[C05] FAIL %s\n", buf); std::fflush(stdout);
        }
        void step(const char* name, double ms) { steps.push_back("{ \"step\": \"" + std::string(name) + "\", \"ms\": " + std::to_string(ms) + " }"); note("%s done in %.1f ms", name, ms); }
    };

    void StarforgeApp::C05SelfTestInit()
    {
        const char* rp = std::getenv("COSMIC_C05_SELFTEST");
        if (!rp || !*rp) return;
        m_C05 = new C05LifecycleSelfTest();
        auto& t = *m_C05;
        t.resultPath = rp;
        if (const char* root = std::getenv("COSMIC_C05_PROJECT_ROOT")) t.projectRoot = root;
        if (const char* pf = std::getenv("COSMIC_C05_PREFAB")) t.prefabFixture = pf;
        if (t.projectRoot.empty()) t.projectRoot = (fs::current_path() / "c05-projects").generic_string();
#if defined(_DEBUG)
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
        SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
        m_OpenFirstRun = false;
        Cosmic::Application::Get().GetWindow().SetVSync(false);
        t.note("armed: result=%s projectRoot=%s prefab=%s", rp, t.projectRoot.c_str(), t.prefabFixture.c_str());
    }

    void StarforgeApp::C05SelfTestShutdown()
    {
        delete m_C05; m_C05 = nullptr;
    }

    void StarforgeApp::C05SelfTestTick()
    {
        if (!m_C05) return;
        auto& t = *m_C05;
        using P = C05LifecycleSelfTest;
        auto now = [] { return std::chrono::steady_clock::now(); };
        auto msSince = [](std::chrono::steady_clock::time_point t0) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); };
        auto find = [&](Cosmic::UUID id) -> Cosmic::Entity { return m_Ctx.Scene ? m_Ctx.Scene->FindByUUID(id) : Cosmic::Entity{}; };
        auto opaque = [](Cosmic::Entity e, const char* name) -> std::string
        {
            if (!e || !e.HasComponent<Cosmic::OpaqueComponentsComponent>()) return {};
            for (const auto& b : e.GetComponent<Cosmic::OpaqueComponentsComponent>().Blocks)
                if (b.first == name) return b.second;
            return {};
        };
        auto countEntities = [&]() -> size_t
        {
            size_t n = 0;
            if (m_Ctx.Scene) for (auto e : m_Ctx.Scene->GetRegistry().view<Cosmic::IDComponent>()) { (void)e; ++n; }
            return n;
        };
        // Every UUID referenced by a Relationship link must resolve — "no stale entity reference".
        auto checkLinks = [&](const char* when)
        {
            if (!m_Ctx.Scene) return;
            auto& reg = m_Ctx.Scene->GetRegistry();
            for (auto e : reg.view<Cosmic::RelationshipComponent>())
            {
                const auto& rel = reg.get<Cosmic::RelationshipComponent>(e);
                if (rel.Parent.IsValid() && !m_Ctx.Scene->FindByUUID(rel.Parent)) t.fail("%s: an entity's Parent link %s is stale", when, rel.Parent.ToString().c_str());
                for (const auto& c : rel.Children)
                    if (!m_Ctx.Scene->FindByUUID(c)) t.fail("%s: a Children link %s is stale", when, c.ToString().c_str());
            }
        };
        // The expected table, applied to the live scene after reopen / stop / undo.
        auto checkTable = [&](const char* when, bool spriteExpected, bool mapExpected)
        {
            Cosmic::Entity sprite = find(t.spriteId), map = find(t.mapId), canvas = find(t.canvasId),
                           button = find(t.buttonId), lamp = find(t.lampId), imp = find(t.importedId), impChild = find(t.importedChildId);
            if ((bool)sprite != spriteExpected) t.fail("%s: sprite %s", when, spriteExpected ? "missing" : "still present");
            if ((bool)map != mapExpected) t.fail("%s: tilemap %s", when, mapExpected ? "missing" : "still present");
            if (sprite)
            {
                const auto& tr = sprite.GetComponent<Cosmic::TransformComponent>();
                if (!glm::all(glm::epsilonEqual(tr.Position, kSpritePos, 1e-5f))) t.fail("%s: sprite position [%g,%g,%g]", when, tr.Position.x, tr.Position.y, tr.Position.z);
                if (!sprite.HasComponent<Cosmic::SpriteRendererComponent>()) t.fail("%s: sprite lost its SpriteRenderer", when);
                else
                {
                    const auto& sr = sprite.GetComponent<Cosmic::SpriteRendererComponent>();
                    if (sr.ZOrder != kSpriteZ) t.fail("%s: sprite ZOrder %d", when, sr.ZOrder);
                    if (sr.TexturePath != kTexVfs) t.fail("%s: sprite TexturePath '%s'", when, sr.TexturePath.c_str());
                    if (!sr.FlipX) t.fail("%s: sprite FlipX lost", when);
                }
                if (sprite.GetComponent<Cosmic::TagComponent>().Tag != "C05Sprite") t.fail("%s: sprite tag '%s'", when, sprite.GetComponent<Cosmic::TagComponent>().Tag.c_str());
            }
            if (map)
            {
                const auto& tm = map.GetComponent<Cosmic::TilemapComponent>();
                if (tm.GridW != 8 || tm.GridH != 4) t.fail("%s: tilemap grid %dx%d", when, tm.GridW, tm.GridH);
                if (tm.At(0, 0) != 3 || tm.At(7, 3) != 5 || tm.At(1, 1) != 0) t.fail("%s: tilemap cells changed (%d,%d,%d)", when, tm.At(0, 0), tm.At(7, 3), tm.At(1, 1));
            }
            if (!canvas || !button) t.fail("%s: canvas/button missing", when);
            else
            {
                if (!button.HasComponent<Cosmic::RelationshipComponent>() || button.GetComponent<Cosmic::RelationshipComponent>().Parent != t.canvasId) t.fail("%s: button is not a child of the canvas", when);
                if (!button.HasComponent<Cosmic::UiButtonComponent>() || button.GetComponent<Cosmic::UiButtonComponent>().Signal != "c05_play") t.fail("%s: button signal lost", when);
            }
            if (!lamp || !lamp.HasComponent<Cosmic::Light2DComponent>() || std::fabs(lamp.GetComponent<Cosmic::Light2DComponent>().Radius - kLampRadius) > 1e-5f) t.fail("%s: lamp radius lost", when);
            if (!imp) t.fail("%s: imported prefab root missing", when);
            else
            {
                if (opaque(imp, "FutureThing") != t.importedOpaqueFuture) t.fail("%s: imported FutureThing block changed: '%s'", when, opaque(imp, "FutureThing").c_str());
                if (opaque(imp, "MeshRenderer") != t.importedOpaqueMesh) t.fail("%s: imported MeshRenderer (3D) block changed: '%s'", when, opaque(imp, "MeshRenderer").c_str());
                if (!imp.HasComponent<Cosmic::SpriteRendererComponent>() || imp.GetComponent<Cosmic::SpriteRendererComponent>().ZOrder != 4) t.fail("%s: imported sprite lost", when);
                if (!impChild) t.fail("%s: imported child missing", when);
                else if (!impChild.HasComponent<Cosmic::RelationshipComponent>() || impChild.GetComponent<Cosmic::RelationshipComponent>().Parent != t.importedId) t.fail("%s: imported child unparented", when);
                else if (opaque(impChild, "DirectionalLight").find("3.5") == std::string::npos) t.fail("%s: imported child's 3D DirectionalLight block lost", when);
            }
            checkLinks(when);
        };

        switch (t.phase)
        {
        case P::Boot:
        {
            if (++t.frames < 5) return;
            const auto t0 = now();
            std::error_code ec; fs::create_directories(t.projectRoot, ec);
            if (!NewProjectAt("C05Life", t.projectRoot)) { t.fail("NewProjectAt failed"); t.phase = P::Finish; return; }
            t.projectDir = ProjectDir();
            if (!m_Ctx.Scene || m_Ctx.SceneVfsPath.empty()) { t.fail("no scene open after NewProjectAt"); t.phase = P::Finish; return; }
            t.step("create project", msSince(t0));
            t.note("project at %s, scene %s, %zu template entities", t.projectDir.c_str(), m_Ctx.SceneVfsPath.c_str(), countEntities());
            t.phase = P::Create;
            return;
        }
        case P::Create:
        {
            const auto t0 = now();
            const auto* dSprite = Desc("SpriteRenderer"); const auto* dMap = Desc("Tilemap"); const auto* dLight = Desc("Light2D");
            const auto* dCanvas = Desc("Canvas"); const auto* dRect = Desc("RectTransform"); const auto* dButton = Desc("UiButton");
            const auto* dTransform = Desc("Transform"); const auto* dTag = Desc("Tag");
            if (!dSprite || !dMap || !dLight || !dCanvas || !dRect || !dButton || !dTransform || !dTag) { t.fail("a reflected 2D component is not registered"); t.phase = P::Finish; return; }
            const size_t before = countEntities();
            // Sprite: create, add the component, set fields through the real commands.
            Cosmic::Entity sprite = Commands::Create(m_Ctx, "C05Sprite", Cosmic::Entity{}, [](Cosmic::Entity) {});
            t.spriteId = sprite.GetComponent<Cosmic::IDComponent>().ID;
            Commands::AddComponent(m_Ctx, sprite, dSprite->TypeId);
            Commands::SetField(m_Ctx, sprite, dTransform->TypeId, "Position", FieldValue{ kSpritePos });
            Commands::SetField(m_Ctx, sprite, dSprite->TypeId, "ZOrder", FieldValue{ kSpriteZ });
            Commands::SetField(m_Ctx, sprite, dSprite->TypeId, "FlipX", FieldValue{ true });
            // Tilemap: create + paint three cells through TileEdit (one undo stroke each).
            Cosmic::Entity map = Commands::Create(m_Ctx, "C05Map", Cosmic::Entity{}, [](Cosmic::Entity e) { auto& tm = e.AddComponent<Cosmic::TilemapComponent>(); tm.GridW = 8; tm.GridH = 4; tm.EnsureCells(); });
            t.mapId = map.GetComponent<Cosmic::IDComponent>().ID;
            Commands::TileEdit(m_Ctx, map, 0, 0, 3, 1);
            Commands::TileEdit(m_Ctx, map, 7, 3, 5, 2);
            Commands::TileEdit(m_Ctx, map, 1, 1, 9, 3);
            Commands::TileEdit(m_Ctx, map, 1, 1, 0, 4);   // erased again
            // Canvas + button child.
            Cosmic::Entity canvas = Commands::Create(m_Ctx, "C05Canvas", Cosmic::Entity{}, [](Cosmic::Entity e) { e.AddComponent<Cosmic::CanvasComponent>(); });
            t.canvasId = canvas.GetComponent<Cosmic::IDComponent>().ID;
            Cosmic::Entity button = Commands::Create(m_Ctx, "C05Button", canvas, [](Cosmic::Entity e) { e.AddComponent<Cosmic::RectTransformComponent>(); e.AddComponent<Cosmic::UiButtonComponent>().Signal = "c05_play"; e.AddComponent<Cosmic::UiTextComponent>().Text = "Play"; });
            t.buttonId = button.GetComponent<Cosmic::IDComponent>().ID;
            // Light.
            Cosmic::Entity lamp = Commands::Create(m_Ctx, "C05Lamp", Cosmic::Entity{}, [](Cosmic::Entity) {});
            t.lampId = lamp.GetComponent<Cosmic::IDComponent>().ID;
            Commands::AddComponent(m_Ctx, lamp, dLight->TypeId);
            Commands::SetField(m_Ctx, lamp, dLight->TypeId, "Radius", FieldValue{ kLampRadius });
            if (countEntities() != before + 5) t.fail("create: %zu entities, expected %zu", countEntities(), before + 5);
            if (!m_Ctx.Commands.CanUndo()) t.fail("create: the commands recorded no undo");
            if (!m_Ctx.Dirty) t.fail("create: the scene is not dirty after edits");
            t.step("create content", msSince(t0));
            t.phase = P::Import;
            return;
        }
        case P::Import:
        {
            const auto t0 = now();
            // Asset import: a 4x4 PNG written into the project's textures folder, referenced by VFS path.
            {
                std::error_code ec;
                fs::create_directories(fs::path(t.projectDir) / "textures", ec);
                uint8_t px[4 * 4 * 4];
                for (int i = 0; i < 16; ++i) { px[i * 4] = (uint8_t)(i * 16); px[i * 4 + 1] = 200; px[i * 4 + 2] = 40; px[i * 4 + 3] = 255; }
                const std::string disk = Cosmic::FileSystem::Resolve(kTexVfs);
                if (!Cosmic::ImageIO::WritePNG(disk, 4, 4, 4, px)) t.fail("import: could not write %s", disk.c_str());
                Cosmic::Entity sprite = find(t.spriteId);
                Commands::SetField(m_Ctx, sprite, Desc("SpriteRenderer")->TypeId, "TexturePath", FieldValue{ std::string(kTexVfs) });
            }
            // Prefab import (the K13 drop path): instantiate + record as one undo step.
            if (t.prefabFixture.empty() || !fs::exists(t.prefabFixture)) { t.fail("import: prefab fixture missing: %s", t.prefabFixture.c_str()); t.phase = P::Finish; return; }
            const size_t before = countEntities();
            Cosmic::Entity root = Cosmic::SceneSerializer::InstantiatePrefab(*m_Ctx.Scene, t.prefabFixture);
            if (!root) { t.fail("import: InstantiatePrefab returned no root"); t.phase = P::Finish; return; }
            Commands::RecordSpawn(m_Ctx, root, "Import prefab");
            t.importedId = root.GetComponent<Cosmic::IDComponent>().ID;
            if (countEntities() != before + 2) t.fail("import: %zu entities after the prefab, expected %zu", countEntities(), before + 2);
            const auto& rel = root.HasComponent<Cosmic::RelationshipComponent>() ? root.GetComponent<Cosmic::RelationshipComponent>().Children : std::vector<Cosmic::UUID>{};
            if (rel.size() != 1) t.fail("import: prefab root has %zu children, expected 1", rel.size());
            else t.importedChildId = rel[0];
            t.importedOpaqueFuture = opaque(root, "FutureThing");
            t.importedOpaqueMesh   = opaque(root, "MeshRenderer");
            if (t.importedOpaqueFuture.find("survives every build") == std::string::npos) t.fail("import: the unknown FutureThing block was not preserved");
            if (t.importedOpaqueMesh.find("crate.obj") == std::string::npos) t.fail("import: the 3D MeshRenderer block was not preserved opaquely");
            if (root.HasComponent<Cosmic::TagComponent>() && root.GetComponent<Cosmic::TagComponent>().Tag != "Imported") t.fail("import: root tag");
            t.step("import asset + prefab", msSince(t0));
            checkTable("after import", true, true);
            t.phase = P::Save;
            return;
        }
        case P::Save:
        {
            const auto t0 = now();
            if (!SaveScene()) t.fail("save: SaveScene returned false (needs a name?)");
            if (m_Ctx.Dirty) t.fail("save: scene still dirty");
            const fs::path disk = fs::path(t.projectDir) / "scenes" / "Main.cscene";
            if (!fs::exists(disk)) t.fail("save: %s not written", disk.generic_string().c_str());
            t.entitiesBeforeReopen = countEntities();
            t.step("save", msSince(t0));
            t.phase = P::Reopen;
            return;
        }
        case P::Reopen:
        {
            const auto t0 = now();
            const std::string root = t.projectDir;
            CloseProject();
            if (m_Ctx.Scene) t.fail("reopen: scene still held after CloseProject");
            if (!OpenProjectPath(root)) { t.fail("reopen: OpenProjectPath failed"); t.phase = P::Finish; return; }
            if (!m_Ctx.Scene) { t.fail("reopen: no scene after OpenProjectPath"); t.phase = P::Finish; return; }
            if (countEntities() != t.entitiesBeforeReopen) t.fail("reopen: %zu entities, expected %zu", countEntities(), t.entitiesBeforeReopen);
            if (m_Ctx.Commands.CanUndo()) t.fail("reopen: undo history survived a reopen");
            t.step("close + reopen", msSince(t0));
            checkTable("after reopen", true, true);
            t.phase = P::Play;
            return;
        }
        case P::Play:
        {
            const auto t0 = now();
            // Nudge the sprite so Play has a non-default edit scene to restore.
            PlayScene();
            if (!IsPlaying()) { t.fail("play: PlayScene did not enter Play"); t.phase = P::Finish; return; }
            if (!find(t.spriteId)) t.fail("play: the runtime scene lost the sprite's UUID");
            t.step("play", msSince(t0));
            t.playFrames = 0;
            t.phase = P::Playing;
            return;
        }
        case P::Playing:
        {
            // Mutate the RUNTIME scene like gameplay would; the edit scene must not see it.
            if (Cosmic::Entity s = find(t.spriteId)) s.GetComponent<Cosmic::TransformComponent>().Position.x += 1.0f;
            if (++t.playFrames >= 30) t.phase = P::Stop;
            return;
        }
        case P::Stop:
        {
            const auto t0 = now();
            StopScene();
            if (IsPlaying()) t.fail("stop: still playing");
            t.step("stop after 30 frames", msSince(t0));
            checkTable("after stop", true, true);       // the sprite is back at kSpritePos: runtime drift did not leak
            if (m_Ctx.Dirty) t.fail("stop: the edit scene came back dirty");
            t.phase = P::UndoRedo;
            return;
        }
        case P::UndoRedo:
        {
            const auto t0 = now();
            Cosmic::Entity sprite = find(t.spriteId);
            if (!sprite) { t.fail("undo: sprite missing before the delete"); t.phase = P::Finish; return; }
            const size_t n0 = countEntities();
            Commands::Destroy(m_Ctx, sprite);
            if (find(t.spriteId)) t.fail("undo: sprite still present after Destroy");
            if (countEntities() != n0 - 1) t.fail("undo: entity count %zu after Destroy", countEntities());
            if (!m_Ctx.Commands.Undo()) t.fail("undo: Undo returned false");
            checkTable("after undo of delete", true, true);   // SAME UUID, same fields
            if (countEntities() != n0) t.fail("undo: entity count %zu after Undo", countEntities());
            if (!m_Ctx.Commands.Redo()) t.fail("redo: Redo returned false");
            if (find(t.spriteId)) t.fail("redo: sprite present after Redo of the delete");
            checkTable("after redo of delete", false, true);
            if (!m_Ctx.Commands.Undo()) t.fail("undo(2): Undo returned false");
            checkTable("after second undo", true, true);
            // A field edit: set, undo, redo.
            sprite = find(t.spriteId);
            Commands::SetField(m_Ctx, sprite, Desc("SpriteRenderer")->TypeId, "ZOrder", FieldValue{ (int32_t)99 });
            if (sprite.GetComponent<Cosmic::SpriteRendererComponent>().ZOrder != 99) t.fail("setfield: ZOrder not applied");
            m_Ctx.Commands.Undo();
            if (sprite.GetComponent<Cosmic::SpriteRendererComponent>().ZOrder != kSpriteZ) t.fail("setfield undo: ZOrder %d", sprite.GetComponent<Cosmic::SpriteRendererComponent>().ZOrder);
            m_Ctx.Commands.Redo();
            if (sprite.GetComponent<Cosmic::SpriteRendererComponent>().ZOrder != 99) t.fail("setfield redo: ZOrder %d", sprite.GetComponent<Cosmic::SpriteRendererComponent>().ZOrder);
            m_Ctx.Commands.Undo();
            checkTable("after field undo", true, true);
            t.step("undo / redo", msSince(t0));
            t.phase = P::Delete;
            return;
        }
        case P::Delete:
        {
            const auto t0 = now();
            Cosmic::Entity map = find(t.mapId);
            if (!map) { t.fail("delete: tilemap missing"); t.phase = P::Finish; return; }
            Commands::Destroy(m_Ctx, map);
            if (find(t.mapId)) t.fail("delete: tilemap still present");
            checkTable("after delete", true, false);
            if (!SaveScene()) t.fail("delete: final SaveScene failed");
            // Bounded parsing of the saved file: the load must stay inside the U-case deadline.
            {
                const fs::path disk = fs::path(t.projectDir) / "scenes" / "Main.cscene";
                const std::string text = ReadFile(disk);
                Cosmic::Scene probe;
                const auto p0 = now();
                const bool ok = Cosmic::SceneSerializer::LoadFromString(probe, text);
                const double ms = msSince(p0);
                if (!ok) t.fail("final scene does not load back");
                if (ms > 10000.0) t.fail("final scene load took %.0f ms (> 10 s)", ms);
                t.note("final scene: %zu bytes, reloads in %.2f ms", text.size(), ms);
            }
            t.step("delete + save", msSince(t0));
            t.phase = P::Finish;
            return;
        }
        case P::Finish:
        {
            const double total = std::chrono::duration<double>(now() - t.runStart).count();
            const bool pass = t.failures == 0 && t.phase == P::Finish;
#if defined(NDEBUG)
            const char* cfg = "Release";
#else
            const char* cfg = "Debug";
#endif
            // The expected table for the OUT-OF-PROCESS oracle.
            {
                std::ofstream f(fs::path(t.resultPath).parent_path() / "c05-expected-final.json", std::ios::trunc);
                f << "{\n  \"scene\": \"" << Esc((fs::path(t.projectDir) / "scenes" / "Main.cscene").generic_string()) << "\",\n";
                f << "  \"present\": { \"sprite\": \"" << t.spriteId.ToString() << "\", \"canvas\": \"" << t.canvasId.ToString() << "\", \"button\": \"" << t.buttonId.ToString()
                  << "\", \"lamp\": \"" << t.lampId.ToString() << "\", \"imported\": \"" << t.importedId.ToString() << "\", \"importedChild\": \"" << t.importedChildId.ToString() << "\" },\n";
                f << "  \"absent\": [ \"" << t.mapId.ToString() << "\" ],\n";
                f << "  \"sprite\": { \"Tag\": \"C05Sprite\", \"Position\": [" << kSpritePos.x << ", " << kSpritePos.y << ", " << kSpritePos.z << "], \"ZOrder\": " << kSpriteZ << ", \"TexturePath\": \"" << kTexVfs << "\", \"FlipX\": true },\n";
                f << "  \"lamp\": { \"Radius\": " << kLampRadius << " },\n";
                f << "  \"button\": { \"Signal\": \"c05_play\" },\n";
                f << "  \"imported\": { \"FutureThing\": { \"Text\": \"survives every build\", \"Flag\": true, \"Deep\": [1, 2, 3] }, \"MeshRenderer\": { \"MeshPath\": \"assets/models/crate.obj\", \"CastShadows\": false } },\n";
                f << "  \"importedChild\": { \"DirectionalLight\": { \"Intensity\": 3.5 }, \"Light2D\": { \"Radius\": 2.5 } }\n}\n";
            }
            std::ofstream f(t.resultPath, std::ios::trunc);
            if (f)
            {
                f << "{\n  \"work_order\": \"WO-09\",\n  \"case\": \"C05 real-project lifecycle\",\n  \"config\": \"" << cfg << "\",\n";
                f << "  \"path\": \"NewProjectAt -> Commands::Create/AddComponent/SetField/TileEdit -> ImageIO + InstantiatePrefab/RecordSpawn -> SaveScene -> CloseProject/OpenProjectPath -> PlayScene/StopScene -> Commands::Destroy + Undo/Redo -> SaveScene\",\n";
                f << "  \"project\": \"" << Esc(t.projectDir) << "\",\n  \"failed_checks\": " << t.failures << ",\n  \"total_seconds\": " << total << ",\n";
                f << "  \"verdict\": \"" << (pass ? "PASS" : "FAIL") << "\",\n  \"steps\": [\n";
                for (size_t i = 0; i < t.steps.size(); ++i) f << "    " << t.steps[i] << (i + 1 < t.steps.size() ? "," : "") << "\n";
                f << "  ],\n  \"checks_failed\": [\n";
                for (size_t i = 0; i < t.checks.size(); ++i) f << "    \"" << Esc(t.checks[i]) << "\"" << (i + 1 < t.checks.size() ? "," : "") << "\n";
                f << "  ],\n  \"log\": [\n";
                for (size_t i = 0; i < t.log.size(); ++i) f << "    \"" << Esc(t.log[i]) << "\"" << (i + 1 < t.log.size() ? "," : "") << "\n";
                f << "  ]\n}\n";
            }
            {
                std::ofstream c(fs::path(t.resultPath).parent_path() / "c05-editor-console.txt", std::ios::trunc);
                for (const auto& l : m_Ctx.ConsoleLines) c << l.Text << "\n";
            }
            std::printf("C05_SELFTEST_RESULT=%s config=%s failedChecks=%d steps=%zu seconds=%.1f\n", pass ? "PASS" : "FAIL", cfg, t.failures, t.steps.size(), total);
            std::fflush(stdout);
            t.phase = P::Done;
            if (pass) Cosmic::Application::Get().Close();
            else      std::quick_exit(1);
            return;
        }
        case P::Done:
        default:
            return;
        }
    }
}
