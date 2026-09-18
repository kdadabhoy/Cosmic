// render_wo09_sprites.cpp — WO-09 (2D stability) C01, the GPU half: the sprite
// pass (Scene::OnRenderSprites) on a real target, with pixel sentinels — no new
// golden (the `sprites` golden already pins the reference frame; these cases pin
// ORDER, FLIPS and the zero / negative / nonfinite transform policy exactly).
//
//   ordering   equal (ZOrder, key) sprites drawn as an overlapping chain of
//              index-coded quads: the pixel a later item covers must decode to
//              the later item, in exactly the order BuildSpriteDrawList returns
//              (the documented entt-handle tie-break), before and after recycling
//              entity slots; and a NaN-keyed sprite in the layer cannot disturb
//              the finite ones (KI-40);
//   flips      a 2x2 four-colour texture at 20 px per texel: FlipX / FlipY / both
//              move the quadrant colours exactly;
//   transforms zero scale draws nothing, negative scale mirrors, NaN / inf
//              position or scale draws NOTHING ANYWHERE (no ink of that colour in
//              the frame) and the finite sprites around them are untouched.
//
// Everything renders with the pass's own state (depth test ON, depth write OFF,
// alpha) into a pixel-exact camera so sentinel positions are integers.

#include "wo08_common.h"

#include "scene/Components.h"
#include "scene/Entity.h"
#include "scene/Scene.h"

#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace Cosmic;
using namespace CosmicRender;
using namespace Wo08;

namespace
{
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float kInf = std::numeric_limits<float>::infinity();

    Entity AddSprite(Scene& s, const char* name, glm::vec3 pos, glm::vec2 size, glm::vec4 color, int32_t z)
    {
        Entity e = s.CreateEntity(name);
        auto& t  = e.GetComponent<TransformComponent>();
        t.Position = pos;
        t.Scale    = { size.x, size.y, 1.0f };
        auto& sr   = e.AddComponent<SpriteRendererComponent>();
        sr.Color   = color;
        sr.ZOrder  = z;
        return e;
    }

    void RenderSprites(Scene& s, const Ref<FrameBuffer>& fbo, Image& out)
    {
        BeginFrame(fbo);
        s.OnRenderSprites(PixelOrtho(fbo->GetWidth(), fbo->GetHeight()), fbo->GetWidth(), fbo->GetHeight());
        REQUIRE(Capture(fbo, out));
    }

    // A 2x2 RGBA texture: texel (0,0) = bottom-left (GL): BL blue, BR yellow, TL red, TR green.
    Ref<Texture2D> MakeQuadrants()
    {
        const uint8_t px[16] = {
              0,   0, 255, 255,    255, 255,   0, 255,    // row 0 (bottom): blue, yellow
            255,   0,   0, 255,      0, 255,   0, 255,    // row 1 (top):    red,  green
        };
        Ref<Texture2D> t = Texture2D::Create(2, 2);
        t->SetData((void*)px, 16);
        t->SetSampling(TextureFilter::Nearest, TextureWrap::ClampToEdge);
        return t;
    }
}

TEST_SUITE("WO-09 C01")
{
    TEST_CASE("C01 GPU: equal-key ordering — an overlapping chain draws in BuildSpriteDrawList order, before and after slot recycling, with a NaN key in the layer")
    {
        const uint32_t W = 640, H = 64;
        Ref<FrameBuffer> fbo = MakeRgba8Target(W, H);
        REQUIRE(fbo != nullptr);
        Ref<Scene> scene = Scene::Create();
        Scene& s = *scene;

        // 60 sprites, all ZOrder 3, all Position.z 0.5 (equal keys). Sprite i is a
        // 20x40 quad centred at x = 10*i + 10, so sprite i+1 covers the RIGHT half of
        // sprite i. The list order (entt handle) decides which one shows.
        std::vector<Entity> made;
        for (int i = 0; i < 60; ++i)
            made.push_back(AddSprite(s, "c", { 10.0f * i + 10.0f, 32.0f, 0.5f }, { 20.0f, 40.0f }, EncodeIndex((uint32_t)i), 3));

        auto verify = [&](const char* label)
        {
            const auto list = s.BuildSpriteDrawList();
            REQUIRE(list.size() == 60);
            // Every item's colour index (its creation index) from the registry.
            std::vector<uint32_t> orderIdx;
            for (const auto& it : list)
                orderIdx.push_back((uint32_t)std::lround(s.GetRegistry().get<SpriteRendererComponent>(it.E).Color.r * 255.0f)
                                   | ((uint32_t)std::lround(s.GetRegistry().get<SpriteRendererComponent>(it.E).Color.g * 255.0f) << 8));
            Image frame;
            RenderSprites(s, fbo, frame);
            WriteEvidence(std::string("c01-order-") + label, frame);
            // The pixel at x = 10*i + 15 (the overlap of sprite i and sprite i+1, both
            // drawn) must show whichever of the two the list draws LATER; the pixel at
            // x = 5 (only sprite 0) shows sprite 0.
            CHECK(DecodeIndex(PixelAtGl(frame, 5, 32)) == 0);
            int wrong = 0;
            for (int i = 0; i + 1 < 60; ++i)
            {
                // position of i and i+1 in the draw list
                const auto pi = std::find(orderIdx.begin(), orderIdx.end(), (uint32_t)i) - orderIdx.begin();
                const auto pj = std::find(orderIdx.begin(), orderIdx.end(), (uint32_t)i + 1) - orderIdx.begin();
                const uint32_t expect = pi > pj ? (uint32_t)i : (uint32_t)i + 1;
                const int64_t got = DecodeIndex(PixelAtGl(frame, (uint32_t)(10 * i + 15), 32));
                if (got != (int64_t)expect) ++wrong;
            }
            CHECK_MESSAGE(wrong == 0, label << ": " << wrong << " overlap pixels disagree with the draw-list order");
        };
        verify("fresh");

        // Recycle: destroy the even sprites, recreate them with the same transforms.
        // Their handles now carry a higher version, so they draw LAST (after every
        // never-recycled odd sprite) — the tie-break the headless half pinned.
        for (int i = 0; i < 60; i += 2) s.DestroyEntity(made[i]);
        for (int i = 0; i < 60; i += 2)
            made[i] = AddSprite(s, "r", { 10.0f * i + 10.0f, 32.0f, 0.5f }, { 20.0f, 40.0f }, EncodeIndex((uint32_t)i), 3);
        verify("recycled");
        {
            Image frame; RenderSprites(s, fbo, frame);
            for (int i = 0; i + 1 < 60; ++i)
            {
                // even (recycled) covers odd at every overlap
                const uint32_t expect = (i % 2 == 0) ? (uint32_t)i : (uint32_t)i + 1;
                CHECK(DecodeIndex(PixelAtGl(frame, (uint32_t)(10 * i + 15), 32)) == (int64_t)expect);
            }
        }

        // A NaN-keyed sprite in the same layer (KI-40): it sorts LAST and — its key IS
        // a NaN coordinate, so its quad has no finite vertex — paints nothing; the
        // finite chain's pixels are unchanged.
        Image before; RenderSprites(s, fbo, before);
        Entity nan = AddSprite(s, "nan", { 620.0f, 32.0f, kNaN }, { 20.0f, 40.0f }, EncodeIndex(500), 3);
        const auto list = s.BuildSpriteDrawList();
        REQUIRE(list.size() == 61);
        CHECK(list.back().E == (entt::entity)nan);
        Image after; RenderSprites(s, fbo, after);
        WriteEvidence("c01-order-nan-key", after);
        int changed = 0;
        for (uint32_t x = 0; x < W; ++x)
            for (uint32_t y = 0; y < H; ++y)
                changed += !Near(PixelAtGl(before, x, y), PixelAtGl(after, x, y), 0);
        CHECK(changed == 0);
        CHECK(CountColor(after, 0, 0, W, H, EncodeIndexU8(500), 0) == 0);
    }

    TEST_CASE("C01 GPU: flips move the texture quadrants exactly; negative scale is the same mirror; zero scale draws nothing")
    {
        const uint32_t W = 200, H = 60;
        Ref<FrameBuffer> fbo = MakeRgba8Target(W, H);
        REQUIRE(fbo != nullptr);
        Ref<Scene> scene = Scene::Create();
        Scene& s = *scene;
        Ref<Texture2D> quad = MakeQuadrants();

        // Four 40x40 textured sprites: none / FlipX / FlipY / both, 50 px apart.
        const bool flips[4][2] = { { false, false }, { true, false }, { false, true }, { true, true } };
        for (int i = 0; i < 4; ++i)
        {
            Entity e = AddSprite(s, "f", { 25.0f + 50.0f * i, 30.0f, 0.0f }, { 1.0f, 1.0f }, { 1, 1, 1, 1 }, 0);
            auto& sr = e.GetComponent<SpriteRendererComponent>();
            sr.Resolved = quad; sr.ResolvedPath = "";
            sr.PixelsPerUnit = 0.05f;   // 2 texels / 0.05 = 40 world units = 40 px
            sr.FlipX = flips[i][0]; sr.FlipY = flips[i][1];
        }
        Image frame; RenderSprites(s, fbo, frame);
        WriteEvidence("c01-flips", frame);
        const glm::u8vec4 red{ 255, 0, 0, 255 }, green{ 0, 255, 0, 255 }, blue{ 0, 0, 255, 255 }, yellow{ 255, 255, 0, 255 };
        // Sample the four quadrant centres of each sprite (GL coords, bottom-left origin).
        auto q = [&](int i, int dx, int dy) { return PixelAtGl(frame, (uint32_t)(25 + 50 * i + dx), (uint32_t)(30 + dy)); };
        // none: TL red, TR green, BL blue, BR yellow
        CHECK(Near(q(0, -10, +10), red));  CHECK(Near(q(0, +10, +10), green));  CHECK(Near(q(0, -10, -10), blue));  CHECK(Near(q(0, +10, -10), yellow));
        // FlipX: TL green, TR red, BL yellow, BR blue
        CHECK(Near(q(1, -10, +10), green)); CHECK(Near(q(1, +10, +10), red));   CHECK(Near(q(1, -10, -10), yellow)); CHECK(Near(q(1, +10, -10), blue));
        // FlipY: TL blue, TR yellow, BL red, BR green
        CHECK(Near(q(2, -10, +10), blue));  CHECK(Near(q(2, +10, +10), yellow)); CHECK(Near(q(2, -10, -10), red));   CHECK(Near(q(2, +10, -10), green));
        // both: TL yellow, TR blue, BL green, BR red
        CHECK(Near(q(3, -10, +10), yellow)); CHECK(Near(q(3, +10, +10), blue)); CHECK(Near(q(3, -10, -10), green)); CHECK(Near(q(3, +10, -10), red));

        // Negative scale == FlipX (the documented legacy convention), zero scale == nothing.
        Ref<Scene> scene2 = Scene::Create();
        Entity neg = AddSprite(*scene2, "neg", { 25.0f, 30.0f, 0.0f }, { -1.0f, 1.0f }, { 1, 1, 1, 1 }, 0);
        neg.GetComponent<SpriteRendererComponent>().Resolved = quad; neg.GetComponent<SpriteRendererComponent>().PixelsPerUnit = 0.05f;
        Entity zero = AddSprite(*scene2, "zero", { 75.0f, 30.0f, 0.0f }, { 0.0f, 0.0f }, { 1, 0, 1, 1 }, 0);
        Entity zeroT = AddSprite(*scene2, "zeroT", { 125.0f, 30.0f, 0.0f }, { 0.0f, 1.0f }, { 1, 1, 1, 1 }, 0);
        zeroT.GetComponent<SpriteRendererComponent>().Resolved = quad; zeroT.GetComponent<SpriteRendererComponent>().PixelsPerUnit = 0.05f;
        Image f2; RenderSprites(*scene2, fbo, f2);
        CHECK(Near(PixelAtGl(f2, 15, 40), green)); CHECK(Near(PixelAtGl(f2, 35, 40), red));   // mirrored like FlipX
        CHECK(CountInk(f2, 50, 0, 50, H, kClearU8) == 0);                                        // zero scale: no ink
        CHECK(CountInk(f2, 100, 0, 50, H, kClearU8) == 0);                                       // zero width texture sprite: no ink
    }

    TEST_CASE("C01 GPU: NaN / inf position or scale draws nothing anywhere and leaves the finite sprites untouched")
    {
        const uint32_t W = 320, H = 180;
        Ref<FrameBuffer> fbo = MakeRgba8Target(W, H);
        REQUIRE(fbo != nullptr);
        Ref<Scene> scene = Scene::Create();
        Scene& s = *scene;
        // Two finite sentinels at ZOrder 0 and 2, four poisoned sprites at ZOrder 1
        // (between them in the painter order) each with its own index colour.
        AddSprite(s, "a", { 60.0f, 90.0f, 0.0f }, { 80.0f, 80.0f }, EncodeIndex(1), 0);
        AddSprite(s, "b", { 260.0f, 90.0f, 0.0f }, { 80.0f, 80.0f }, EncodeIndex(2), 2);
        Entity p1 = AddSprite(s, "nanPos", { kNaN, 90.0f, 0.0f }, { 400.0f, 400.0f }, EncodeIndex(11), 1);
        Entity p2 = AddSprite(s, "infPos", { kInf, -kInf, 0.0f }, { 400.0f, 400.0f }, EncodeIndex(12), 1);
        Entity p3 = AddSprite(s, "nanScale", { 160.0f, 90.0f, 0.0f }, { kNaN, 50.0f }, EncodeIndex(13), 1);
        Entity p4 = AddSprite(s, "infScale", { 160.0f, 90.0f, 0.0f }, { kInf, kInf }, EncodeIndex(14), 1);
        Entity p5 = AddSprite(s, "nanRot", { 160.0f, 90.0f, 0.0f }, { 50.0f, 50.0f }, EncodeIndex(15), 1);
        p5.GetComponent<TransformComponent>().Rotation.z = kNaN;
        (void)p1; (void)p2; (void)p3; (void)p4;
        const auto list = s.BuildSpriteDrawList();
        CHECK(list.size() == 7);                                    // all seven are LISTED (finite keys: Position.z = 0)
        Image frame; RenderSprites(s, fbo, frame);
        WriteEvidence("c01-nonfinite-transforms", frame);
        // Pinned policy: a non-finite transform produces NO fragments (the vertices
        // fail clipping) — no ink of any poisoned colour anywhere in the frame ...
        int poisoned = 0;
        for (uint32_t y = 0; y < H; ++y)
            for (uint32_t x = 0; x < W; ++x)
            {
                const int64_t idx = DecodeIndex(PixelAtGl(frame, x, y));
                if (idx >= 11 && idx <= 15) ++poisoned;
            }
        CHECK(poisoned == 0);
        // ... and the finite sentinels are exactly where they belong, fully painted.
        CHECK(CountColor(frame, 20, 50, 80, 80, EncodeIndexU8(1), 0) == 80 * 80);
        CHECK(CountColor(frame, 220, 50, 80, 80, EncodeIndexU8(2), 0) == 80 * 80);
        CHECK(DecodeIndex(PixelAtGl(frame, 160, 90)) == -1);       // the middle stays clear
    }
}
