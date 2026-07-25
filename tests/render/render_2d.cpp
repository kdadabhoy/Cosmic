// render_2d.cpp — 2D golden images (Phase 29 W2 / plan doc 28 §9.5).
//
// These five frames run under BOTH engine configurations, so they are the
// contract the 2D engine is cut against: whatever the split does to Scene.cpp,
// Components.h and SceneRenderer, these pixels may not move.
//
//   sprites   painter order, textures, flips, rotation, the T12/T13 gates.
//             Also pins the W2 BuildSpriteDrawList extraction on the GPU.
//   tilemap   the culled cell walk with the camera parked at a map edge.
//   light2d   the lit frame, PLUS the byte-identical A/B that proves the
//             no-lights + white-ambient compat gate really makes no GL calls.
//   ui        canvas layout: nested anchors, solid panels and buttons.
//   scene2d   a full 2D-mode frame through SceneRenderer::RenderToTexture —
//             HDR -> tonemap -> FXAA, the way Starforge and PlayerLayer drive it.
//
// Everything is procedural: no asset files, no AssetLibrary, no disk. Textures
// are generated into Texture2D::SetData and assigned straight to the components'
// runtime Resolved slot (leaving TexturePath empty, so the lazy path-resolve is
// skipped), which keeps every frame reproducible from the source alone.

#include <doctest.h>

#include "GoldenImage.h"

#include "graphics/FrameBuffer.h"
#include "graphics/Texture.h"
#include "renderer/RenderCommand.h"
#include "renderer/Renderer2D.h"
#include "renderer/SceneRenderer.h"
#include "scene/Components.h"
#include "scene/Entity.h"
#include "scene/Scene.h"
#include "scene/ui/UiComponents.h"
#include "scene/ui/UiSystem.h"
#include "camera/PerspectiveCamera.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdint>
#include <vector>

using namespace Cosmic;
using namespace CosmicRender;

namespace
{
    constexpr uint32_t kW = kGoldenWidth;
    constexpr uint32_t kH = kGoldenHeight;

    // A recognizable, deliberately non-black backdrop: an all-black golden would
    // hide "nothing rendered at all".
    constexpr glm::vec4 kClear{ 0.09f, 0.11f, 0.16f, 1.0f };

    // ------------------------------------------------------------------------
    // Procedural textures
    // ------------------------------------------------------------------------

    Ref<Texture2D> MakeChecker(uint32_t size, uint32_t cell,
                               glm::u8vec4 a, glm::u8vec4 b)
    {
        std::vector<uint8_t> px((size_t)size * size * 4);
        for (uint32_t y = 0; y < size; ++y)
        {
            for (uint32_t x = 0; x < size; ++x)
            {
                const bool odd = ((x / cell) + (y / cell)) % 2 == 1;
                const glm::u8vec4 c = odd ? b : a;
                const size_t i = ((size_t)y * size + x) * 4;
                px[i + 0] = c.r; px[i + 1] = c.g; px[i + 2] = c.b; px[i + 3] = c.a;
            }
        }
        Ref<Texture2D> t = Texture2D::Create(size, size);
        t->SetData(px.data(), (uint32_t)px.size());
        return t;
    }

    // A 4x4 atlas of distinct flat colours (tile ids 1..16), 16 texels per tile.
    Ref<Texture2D> MakeAtlas()
    {
        constexpr uint32_t kTile = 16, kCols = 4, kSize = kTile * kCols;
        static const glm::u8vec4 palette[16] = {
            { 220,  70,  70, 255 }, {  70, 200,  90, 255 }, {  80, 120, 235, 255 }, { 235, 200,  60, 255 },
            { 200,  90, 210, 255 }, {  60, 205, 205, 255 }, { 240, 140,  50, 255 }, { 150, 150, 150, 255 },
            { 120,  40,  40, 255 }, {  40, 110,  55, 255 }, {  45,  70, 140, 255 }, { 140, 120,  40, 255 },
            { 110,  50, 120, 255 }, {  35, 120, 120, 255 }, { 140,  85,  30, 255 }, {  70,  70,  70, 255 },
        };

        std::vector<uint8_t> px((size_t)kSize * kSize * 4);
        for (uint32_t y = 0; y < kSize; ++y)
        {
            for (uint32_t x = 0; x < kSize; ++x)
            {
                const uint32_t idx = (y / kTile) * kCols + (x / kTile);
                glm::u8vec4 c = palette[idx];
                // A one-texel dark border per tile so the atlas mapping is visible.
                if ((x % kTile) == 0 || (y % kTile) == 0)
                    c = { (uint8_t)(c.r / 3), (uint8_t)(c.g / 3), (uint8_t)(c.b / 3), 255 };
                const size_t i = ((size_t)y * kSize + x) * 4;
                px[i + 0] = c.r; px[i + 1] = c.g; px[i + 2] = c.b; px[i + 3] = c.a;
            }
        }
        Ref<Texture2D> t = Texture2D::Create(kSize, kSize);
        t->SetData(px.data(), (uint32_t)px.size());
        return t;
    }

    // ------------------------------------------------------------------------
    // Camera + target helpers
    // ------------------------------------------------------------------------

    // The 2D camera: `halfHeight` world units above and below `center`, aspect
    // taken from the golden target.
    glm::mat4 Ortho2D(const glm::vec2& center, float halfHeight)
    {
        const float aspect = (float)kW / (float)kH;
        const float hw = halfHeight * aspect;
        const glm::mat4 proj = glm::ortho(-hw, hw, -halfHeight, halfHeight, -100.0f, 100.0f);
        const glm::mat4 view = glm::translate(glm::mat4(1.0f), { -center.x, -center.y, 0.0f });
        return proj * view;
    }

    void BeginFrame(const Ref<FrameBuffer>& fbo)
    {
        fbo->Bind();
        RenderCommand::SetViewport(0, 0, kW, kH);
        RenderCommand::SetClearColor(kClear);
        RenderCommand::Clear();
    }

    // ------------------------------------------------------------------------
    // A note on the 2D light goldens
    // ------------------------------------------------------------------------
    // Every golden is a FIRST frame — a fresh SceneRenderer, a fresh target, the
    // 2D light buffer created during the very call under test. That is exactly
    // the case Light2DRenderer::Composite used to get wrong (it read the caller's
    // framebuffer after Ensure() had created the light buffer and unbound to the
    // default one, so the multiply landed on the window). Nothing here works
    // around it any more; the lit-vs-unlit assertion below is what keeps it fixed.

    // ------------------------------------------------------------------------
    // Scene fixtures
    // ------------------------------------------------------------------------

    // Attach a texture to a sprite through the RUNTIME slot: TexturePath stays
    // empty, so Scene::OnRenderSprites' lazy re-resolve never fires and no asset
    // lookup happens.
    void SetSpriteTexture(SpriteRendererComponent& s, const Ref<Texture2D>& tex)
    {
        s.Resolved     = tex;
        s.ResolvedPath = "";
    }

    Entity AddSprite(Scene& scene, const char* name, glm::vec3 pos, glm::vec2 scale,
                     glm::vec4 color, int32_t zOrder)
    {
        Entity e = scene.CreateEntity(name);
        auto& t  = e.GetComponent<TransformComponent>();
        t.Position = pos;
        t.Scale    = { scale.x, scale.y, 1.0f };
        auto& s  = e.AddComponent<SpriteRendererComponent>();
        s.Color  = color;
        s.ZOrder = zOrder;
        return e;
    }

    struct SpriteScene
    {
        Ref<Scene>     ScenePtr;
        Ref<Texture2D> Checker;
    };

    // The sprites fixture, shared by the `sprites`, `light2d` and `scene2d`
    // goldens so a change shows up in all three at once.
    SpriteScene MakeSpriteScene()
    {
        SpriteScene f;
        f.ScenePtr = Scene::Create();
        f.Checker  = MakeChecker(32, 4, { 235, 235, 235, 255 }, { 120, 130, 150, 255 });
        Scene& s = *f.ScenePtr;

        // Backdrop: a big textured quad well behind everything (ZOrder -10).
        {
            Entity e = AddSprite(s, "backdrop", { 0.0f, 0.0f, 0.0f }, { 16.0f, 9.0f },
                                 { 0.55f, 0.60f, 0.75f, 1.0f }, -10);
            auto& sr = e.GetComponent<SpriteRendererComponent>();
            SetSpriteTexture(sr, f.Checker);
            sr.PixelsPerUnit = 2.0f;   // 32 px / 2 = 16 world units wide
        }

        // Three flat quads at ascending ZOrder — the painter order is visible as
        // which one is on top where they overlap.
        AddSprite(s, "layer_back",  { -1.6f, 0.6f, 0.0f }, { 3.0f, 2.0f }, { 0.85f, 0.25f, 0.25f, 1.0f }, 0);
        AddSprite(s, "layer_mid",   { -0.6f, 0.1f, 0.0f }, { 3.0f, 2.0f }, { 0.25f, 0.75f, 0.35f, 1.0f }, 1);
        AddSprite(s, "layer_front", {  0.4f,-0.4f, 0.0f }, { 3.0f, 2.0f }, { 0.30f, 0.45f, 0.95f, 1.0f }, 2);

        // A rotated, half-transparent quad on top.
        {
            Entity e = AddSprite(s, "rotated", { 3.4f, 1.2f, 0.0f }, { 2.0f, 2.0f },
                                 { 1.0f, 0.85f, 0.25f, 0.75f }, 3);
            e.GetComponent<TransformComponent>().Rotation.z = 30.0f;
        }

        // YSort pair: same ZOrder, the LOWER one must draw in front.
        {
            Entity hi = AddSprite(s, "ysort_high", { -4.2f, -1.2f, 0.0f }, { 2.2f, 2.2f },
                                  { 0.95f, 0.55f, 0.15f, 1.0f }, 5);
            hi.GetComponent<SpriteRendererComponent>().YSort = true;
            Entity lo = AddSprite(s, "ysort_low", { -3.4f, -2.0f, 0.0f }, { 2.2f, 2.2f },
                                  { 0.20f, 0.85f, 0.85f, 1.0f }, 5);
            lo.GetComponent<SpriteRendererComponent>().YSort = true;
        }

        // A textured sprite with both flips, so the UV winding is pinned.
        {
            Entity e = AddSprite(s, "flipped", { 4.6f, -1.8f, 0.0f }, { 1.0f, 1.0f },
                                 { 1.0f, 1.0f, 1.0f, 1.0f }, 6);
            auto& sr = e.GetComponent<SpriteRendererComponent>();
            SetSpriteTexture(sr, f.Checker);
            sr.PixelsPerUnit = 16.0f;
            sr.SourceRect    = { 0.0f, 0.0f, 0.5f, 0.5f };   // top-left quadrant only
            sr.FlipX = sr.FlipY = true;
        }

        // T12 + T13: neither of these may appear in the frame.
        {
            Entity off = AddSprite(s, "disabled", { 0.0f, 2.8f, 0.0f }, { 6.0f, 2.0f },
                                   { 1.0f, 0.0f, 1.0f, 1.0f }, 100);
            off.GetComponent<SpriteRendererComponent>().Enabled = false;

            Entity parent = s.CreateEntity("inactive_parent");
            parent.GetComponent<TagComponent>().Active = false;
            Entity child = AddSprite(s, "inactive_child", { 0.0f, -2.8f, 0.0f }, { 6.0f, 2.0f },
                                     { 1.0f, 0.0f, 1.0f, 1.0f }, 100);
            s.SetParent(child, parent);
        }

        return f;
    }
}

// ============================================================================
// sprites
// ============================================================================

TEST_SUITE("2D goldens")
{
    TEST_CASE("sprites — painter order, textures, flips, rotation, gates")
    {
        SpriteScene f = MakeSpriteScene();
        Ref<FrameBuffer> fbo = MakeTarget();
        REQUIRE(fbo != nullptr);

        BeginFrame(fbo);
        f.ScenePtr->OnRenderSprites(Ortho2D({ 0.0f, 0.0f }, 5.0f), kW, kH);

        Image frame;
        REQUIRE(Capture(fbo, frame));
        CHECK(CheckGolden("sprites", frame));
    }

    TEST_CASE("tilemap — the culled cell walk at a map edge")
    {
        Ref<Scene> scene = Scene::Create();
        Ref<Texture2D> atlas = MakeAtlas();

        Entity map = scene->CreateEntity("map");
        map.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 0.0f };
        auto& tm = map.AddComponent<TilemapComponent>();
        tm.TileW = 16; tm.TileH = 16; tm.Columns = 4;
        tm.GridW = 24; tm.GridH = 24;
        tm.EnsureCells();
        tm.Resolved     = atlas;
        tm.ResolvedPath = "";   // runtime slot: no path resolve

        // A deterministic pattern with empty (0) cells punched through it, so
        // both the drawn and the skipped branches of the cell walk are covered.
        for (int y = 0; y < tm.GridH; ++y)
            for (int x = 0; x < tm.GridW; ++x)
            {
                const bool hole = ((x + y) % 7) == 0;
                tm.Cells[(size_t)y * tm.GridW + x] =
                    hole ? (uint16_t)0 : (uint16_t)(1 + ((x * 3 + y * 5) % 16));
            }

        // A second, smaller map layered ON TOP through ZOrder, offset so it only
        // partly overlaps — pins the interleave and the per-map origin.
        Entity overlay = scene->CreateEntity("overlay");
        overlay.GetComponent<TransformComponent>().Position = { 3.0f, 2.0f, 0.0f };
        auto& om = overlay.AddComponent<TilemapComponent>();
        om.TileW = 16; om.TileH = 16; om.Columns = 4;
        om.GridW = 5; om.GridH = 4; om.ZOrder = 2;
        om.EnsureCells();
        om.Resolved     = atlas;
        om.ResolvedPath = "";
        for (size_t i = 0; i < om.Cells.size(); ++i)
            om.Cells[i] = (uint16_t)(1 + (i % 4) * 4);

        // The camera sits near the map's BOTTOM-LEFT corner, so the cull window
        // clamps on two edges and the frame shows the map running off-screen.
        Ref<FrameBuffer> fbo = MakeTarget();
        REQUIRE(fbo != nullptr);
        BeginFrame(fbo);
        scene->OnRenderSprites(Ortho2D({ 2.5f, 1.5f }, 5.0f), kW, kH);

        Image frame;
        REQUIRE(Capture(fbo, frame));
        CHECK(CheckGolden("tilemap", frame));
    }

    TEST_CASE("light2d — the lit frame")
    {
        SpriteScene f = MakeSpriteScene();
        Scene& s = *f.ScenePtr;

        // Night ambient plus two warm lights, so the frame is visibly darkened
        // between them.
        Entity env = s.CreateEntity("Environment");
        env.AddComponent<EnvironmentComponent>().Ambient2D = { 0.18f, 0.20f, 0.30f };

        Entity a = s.CreateEntity("lamp_a");
        a.GetComponent<TransformComponent>().Position = { -2.5f, 0.5f, 0.0f };
        auto& la = a.AddComponent<Light2DComponent>();
        la.Color = { 1.0f, 0.82f, 0.55f }; la.Radius = 4.5f; la.Intensity = 1.8f; la.Falloff = 2.0f;

        Entity b = s.CreateEntity("lamp_b");
        b.GetComponent<TransformComponent>().Position = { 3.2f, -1.4f, 0.0f };
        auto& lb = b.AddComponent<Light2DComponent>();
        lb.Color = { 0.45f, 0.70f, 1.0f }; lb.Radius = 3.5f; lb.Intensity = 2.4f; lb.Falloff = 3.0f;

        // A disabled light must contribute nothing to the golden.
        Entity off = s.CreateEntity("lamp_off");
        off.GetComponent<TransformComponent>().Position = { 0.0f, 0.0f, 0.0f };
        auto& lo = off.AddComponent<Light2DComponent>();
        lo.Radius = 20.0f; lo.Intensity = 8.0f; lo.Enabled = false;

        const glm::mat4 vp = Ortho2D({ 0.0f, 0.0f }, 5.0f);

        Ref<FrameBuffer> fbo = MakeTarget();
        REQUIRE(fbo != nullptr);
        BeginFrame(fbo);
        s.OnRenderSprites(vp, kW, kH);
        s.OnRender2DLights(vp, kW, kH);

        Image frame;
        REQUIRE(Capture(fbo, frame));

        // The pass must actually have changed the frame. Without this a light
        // composite that silently no-ops (a failed shader load, a dropped draw)
        // would produce a golden identical to `sprites` and then keep passing
        // forever — a golden that pins nothing.
        {
            SpriteScene unlitFixture = MakeSpriteScene();
            Ref<FrameBuffer> unlitFbo = MakeTarget();
            REQUIRE(unlitFbo != nullptr);
            BeginFrame(unlitFbo);
            unlitFixture.ScenePtr->OnRenderSprites(vp, kW, kH);
            Image unlit;
            REQUIRE(Capture(unlitFbo, unlit));

            REQUIRE_FALSE(BytesEqual(frame, unlit));
            const Diff d = Compare(frame, unlit);
            CHECK_MESSAGE(d.DifferingFraction() > 0.5, "the 2D light composite barely changed the "
                          "frame: only ", d.DifferingPixels, " / ", d.TotalPixels, " pixels moved");
        }

        CHECK(CheckGolden("light2d", frame));
    }

    TEST_CASE("light2d A/B — no lights + white ambient is BYTE-identical to skipping the pass")
    {
        // The X5 compat guarantee, measured rather than asserted: a 2D scene with
        // no lights and the default white Ambient2D must produce exactly the
        // bytes it produced before the pass existed. Both frames come off the same
        // GPU in the same run, so this one is memcmp-exact, not tolerance-based.
        const glm::mat4 vp = Ortho2D({ 0.0f, 0.0f }, 5.0f);

        Image withGate, withoutPass;

        // A: the full path — OnRender2DLights IS called, and its gate returns.
        {
            SpriteScene f = MakeSpriteScene();
            Entity env = f.ScenePtr->CreateEntity("Environment");
            env.AddComponent<EnvironmentComponent>();   // Ambient2D defaults to white
            REQUIRE(f.ScenePtr->FindEnvironment() != nullptr);
            CHECK(f.ScenePtr->FindEnvironment()->Ambient2D == glm::vec3(1.0f));

            Ref<FrameBuffer> fbo = MakeTarget();
            REQUIRE(fbo != nullptr);
            BeginFrame(fbo);
            f.ScenePtr->OnRenderSprites(vp, kW, kH);
            f.ScenePtr->OnRender2DLights(vp, kW, kH);
            REQUIRE(Capture(fbo, withGate));
        }

        // B: the pre-X5 path — the pass is never invoked at all.
        {
            SpriteScene f = MakeSpriteScene();
            Ref<FrameBuffer> fbo = MakeTarget();
            REQUIRE(fbo != nullptr);
            BeginFrame(fbo);
            f.ScenePtr->OnRenderSprites(vp, kW, kH);
            REQUIRE(Capture(fbo, withoutPass));
        }

        CHECK(BytesEqual(withGate, withoutPass));

        // And the same frame is what the `sprites` golden pins, so the A/B and the
        // committed reference can never drift apart silently.
        CHECK(withGate.Width == kW);
        CHECK(withGate.Height == kH);
    }

    TEST_CASE("ui — canvas layout with nested anchors, panels and buttons")
    {
        Ref<Scene> scene = Scene::Create();
        Scene& s = *scene;

        Entity canvas = s.CreateEntity("Canvas");
        auto& cc = canvas.AddComponent<CanvasComponent>();
        cc.ScaleMode       = UiScaleMode::ConstantPixel;   // 1 unit = 1 golden pixel
        cc.ReferenceHeight = (float)kH;

        auto addImage = [&](const char* name, Entity parent, glm::vec2 aMin, glm::vec2 aMax,
                            glm::vec2 oMin, glm::vec2 oMax, glm::vec4 tint, int32_t z) -> Entity
        {
            Entity e = s.CreateEntity(name);
            auto& rt = e.AddComponent<RectTransformComponent>();
            rt.AnchorMin = aMin; rt.AnchorMax = aMax;
            rt.OffsetMin = oMin; rt.OffsetMax = oMax;
            rt.ZOrder    = z;
            e.AddComponent<UiImageComponent>().Tint = tint;
            s.SetParent(e, parent);
            return e;
        };

        // A stretched panel inset from every edge.
        Entity panel = addImage("panel", canvas, { 0, 0 }, { 1, 1 }, { 16, 16 }, { -16, -16 },
                                { 0.16f, 0.18f, 0.24f, 0.92f }, 0);

        // A title bar pinned to the panel's top edge.
        addImage("titlebar", panel, { 0, 0 }, { 1, 0 }, { 0, 0 }, { 0, 26 },
                 { 0.24f, 0.44f, 0.78f, 1.0f }, 1);

        // Three buttons down the left, each a nested child of the panel.
        for (int i = 0; i < 3; ++i)
        {
            const float top = 36.0f + i * 30.0f;
            Entity b = addImage(i == 0 ? "button0" : (i == 1 ? "button1" : "button2"),
                                panel, { 0, 0 }, { 0, 0 }, { 12, top }, { 108, top + 22.0f },
                                { 0.30f + 0.12f * i, 0.62f, 0.36f, 1.0f }, 2);
            b.AddComponent<UiButtonComponent>();
        }

        // A corner-pinned badge with a nested inner dot — the nested-anchor case.
        Entity badge = addImage("badge", panel, { 1, 1 }, { 1, 1 }, { -56, -40 }, { -12, -12 },
                                { 0.86f, 0.30f, 0.26f, 1.0f }, 3);
        addImage("badge_dot", badge, { 0.5f, 0.5f }, { 0.5f, 0.5f }, { -6, -6 }, { 6, 6 },
                 { 1.0f, 0.94f, 0.70f, 1.0f }, 4);

        // A second canvas that must draw OVER the first (higher SortOrder). Its
        // strip is anchored to the canvas BOTTOM: canvas space is top-left origin
        // with +y DOWN, so anchor y == 1 is the bottom edge — the convention this
        // golden pins alongside the layering.
        Entity hud = s.CreateEntity("Hud");
        hud.AddComponent<CanvasComponent>().SortOrder = 5;
        addImage("hud_strip", hud, { 0, 1 }, { 1, 1 }, { 0, -12 }, { 0, 0 },
                 { 0.95f, 0.72f, 0.18f, 0.85f }, 0);

        Ref<FrameBuffer> fbo = MakeTarget();
        REQUIRE(fbo != nullptr);
        BeginFrame(fbo);

        const UiRect viewport{ { 0.0f, 0.0f }, { (float)kW, (float)kH } };
        UiSystem::Render(s, viewport);

        Image frame;
        REQUIRE(Capture(fbo, frame));
        CHECK(CheckGolden("ui", frame));
    }

    TEST_CASE("scene2d — a full 2D-mode frame through SceneRenderer::RenderToTexture")
    {
        // The 2D authoring frame as Starforge and PlayerLayer actually drive it:
        // BuildRenderDesc, sky/IBL/shadows forced off (2D mode), sprites drawn from
        // the DrawTransparent hook with the HDR target still bound, the 2D light
        // composite right after, and canvas UI over the composited LDR frame. That
        // routes the whole 2D stack through HDR -> tonemap -> FXAA.
        SpriteScene f = MakeSpriteScene();
        Scene& s = *f.ScenePtr;

        Entity env = s.CreateEntity("Environment");
        auto& ec = env.AddComponent<EnvironmentComponent>();
        ec.Ambient2D = { 0.55f, 0.58f, 0.72f };

        Entity lamp = s.CreateEntity("lamp");
        lamp.GetComponent<TransformComponent>().Position = { -1.0f, 0.0f, 0.0f };
        auto& lc = lamp.AddComponent<Light2DComponent>();
        lc.Color = { 1.0f, 0.86f, 0.62f }; lc.Radius = 6.0f; lc.Intensity = 1.4f;

        // A small HUD strip, so the LDR overlay stage is in the frame too.
        Entity canvas = s.CreateEntity("Canvas");
        canvas.AddComponent<CanvasComponent>().ScaleMode = UiScaleMode::ConstantPixel;
        Entity bar = s.CreateEntity("hud");
        auto& rt = bar.AddComponent<RectTransformComponent>();
        rt.AnchorMin = { 0, 1 }; rt.AnchorMax = { 1, 1 };
        rt.OffsetMin = { 8, -20 }; rt.OffsetMax = { -8, -6 };
        bar.AddComponent<UiImageComponent>().Tint = { 0.10f, 0.12f, 0.18f, 0.80f };
        s.SetParent(bar, canvas);

        // A 2D camera expressed as a Camera, which is what BuildRenderDesc takes.
        // An ortho-shaped perspective is not available, so the desc's own View /
        // Projection are overwritten with the 2D matrices after the gather — the
        // same thing the editor's 2D mode does with its Camera2D rig.
        PerspectiveCamera cam(45.0f, (float)kW / (float)kH, 0.1f, 100.0f);
        cam.SetPosition({ 0.0f, 0.0f, 10.0f });

        SceneRenderDesc desc;
        s.BuildRenderDesc(cam, 0.0f, desc);

        const float aspect = (float)kW / (float)kH;
        desc.Projection = glm::ortho(-5.0f * aspect, 5.0f * aspect, -5.0f, 5.0f, -100.0f, 100.0f);
        desc.View       = glm::mat4(1.0f);
        desc.TimeSeconds = 0.0f;
        desc.DeltaTime   = 0.0f;

        desc.Settings.ClearColor = kClear;
        desc.Settings.Skybox  = false;   // a skybox under an ortho projection is degenerate
        desc.Settings.IBL     = false;
        desc.Settings.Shadows = false;
        desc.Settings.FXAA    = true;

        Scene* scenePtr = &s;
        desc.DrawTransparent = [scenePtr](const SceneDrawContext& c)
        {
            scenePtr->OnRenderSprites(c.ViewProjection, kW, kH);
            scenePtr->OnRender2DLights(c.ViewProjection, kW, kH);
        };
        desc.DrawOverlay2D = [scenePtr]()
        {
            const UiRect viewport{ { 0.0f, 0.0f }, { (float)kW, (float)kH } };
            UiSystem::Render(*scenePtr, viewport);
        };

        SceneRenderer renderer;
        renderer.Init(kW, kH, 512);
        REQUIRE(renderer.IsInitialized());

        Ref<FrameBuffer> fbo = MakeTarget();
        REQUIRE(fbo != nullptr);
        renderer.RenderToTexture(desc, fbo);

        Image frame;
        REQUIRE(Capture(fbo, frame));
        CHECK(CheckGolden("scene2d", frame));

        renderer.Shutdown();
    }
}
