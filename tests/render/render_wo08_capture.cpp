// render_wo08_capture.cpp — WO-08 R06 (GPU half): framebuffer read-back.
//
// FrameBuffer::ReadPixels on a 641x359 RGBA8 target must hand back a
// 641x359x4 top-left-origin buffer: the marker drawn at the world top-left is
// at index 0, alpha is whatever the blend equation left in the target, and the
// dimensions come from the FBO, never from the caller. On an RGBA16F target the
// documented HDR-to-byte behaviour is "convert + clamp": 2.0 -> 255, -1.0 -> 0,
// 0.25 -> 64 (+-1 for the driver's rounding). Every expectation here is
// derived from the scene, not from a second call into the code under test, and
// the PNG written from the capture is re-read through the CPU path so the two
// halves of R06 meet on the same bytes.

#include "wo08_common.h"

using namespace Wo08;

TEST_SUITE("WO-08 R06")
{
    TEST_CASE("R06 GPU read-back — 641x359 RGBA8: dimensions, top-left origin, alpha, and out-of-range attachments")
    {
        const uint32_t w = 641, h = 359;
        Ref<FrameBuffer> fbo = MakeRgba8Target(w, h);
        REQUIRE(fbo != nullptr);

        const glm::u8vec4 tl{ 255, 0, 0, 255 }, tr{ 0, 255, 0, 255 }, bl{ 0, 0, 255, 255 }, br{ 255, 255, 0, 255 };
        BeginFrame(fbo, { 0.0f, 0.0f, 0.0f, 0.0f });   // ALPHA-ZERO clear
        Renderer2D::PushRenderPass(PixelOrtho(w, h), { 0.0f, 0.0f, (float)w, (float)h });
        auto q = [&](float cx, float cy, float s, const glm::u8vec4& c) { Renderer2D::DrawQuad(glm::vec2{ cx, cy }, { s, s }, glm::vec4(c) / 255.0f); };
        q(4.0f, h - 4.0f, 8.0f, tl);          // GL top-left corner => image (0..7, 0..7)
        q(w - 4.0f, h - 4.0f, 8.0f, tr);
        q(4.0f, 4.0f, 8.0f, bl);
        q(w - 4.0f, 4.0f, 8.0f, br);
        // A half-alpha white quad over the alpha-zero clear: RGB = 0.5, alpha =
        // 0.5*0.5 + 0*(1-0.5) = 0.25 under src-alpha / one-minus-src-alpha.
        q(320.5f, 179.5f, 21.0f, { 255, 255, 255, 128 });
        Renderer2D::PopRenderPass();

        // Read back straight through the engine verb (the FBO is bound).
        fbo->Bind();
        std::vector<uint8_t> rgba;
        uint32_t rw = 0, rh = 0;
        REQUIRE(fbo->ReadPixels(0, rgba, rw, rh));
        CHECK(rw == w);
        CHECK(rh == h);
        REQUIRE(rgba.size() == (size_t)w * h * 4);

        auto at = [&](uint32_t x, uint32_t y) { const uint8_t* p = &rgba[((size_t)y * w + x) * 4]; return glm::u8vec4{ p[0], p[1], p[2], p[3] }; };
        CHECK_MESSAGE(Near(at(0, 0), tl), "index 0 is the TOP-left marker: ", Describe(at(0, 0)));
        CHECK(Near(at(7, 7), tl));
        CHECK(Near(at(w - 1, 0), tr));
        CHECK(Near(at(0, h - 1), bl));
        CHECK(Near(at(w - 1, h - 1), br));
        CHECK_MESSAGE(Near(at(320, 179), glm::u8vec4{ 128, 128, 128, 64 }, 2), "half-alpha over alpha-zero: ", Describe(at(320, 179)));
        CHECK_MESSAGE(Near(at(100, 100), glm::u8vec4{ 0, 0, 0, 0 }), "cleared alpha-zero pixel: ", Describe(at(100, 100)));
        CHECK(Near(at(8, 8), glm::u8vec4{ 0, 0, 0, 0 }));   // just past the marker

        // The row stride is exactly w*4: the last pixel of row 0 and the first of
        // row 1 are distinct known values (marker vs clear).
        CHECK(Near(at(w - 1, 0), tr));
        CHECK(Near(at(0, 1), tl));
        CHECK(Near(at(8, 1), glm::u8vec4{ 0, 0, 0, 0 }));

        // An out-of-range attachment fails and leaves the outputs untouched.
        std::vector<uint8_t> keep = { 1, 2, 3 };
        uint32_t kw = 77, kh = 88;
        CHECK_FALSE(fbo->ReadPixels(1, keep, kw, kh));
        CHECK(keep == std::vector<uint8_t>{ 1, 2, 3 });
        CHECK(kw == 77);
        CHECK(kh == 88);

        // The capture round-trips through the CPU PNG path unchanged (alpha too).
        {
            const std::string dir = EvidenceDir();
            const std::string path = ((dir.empty() ? std::filesystem::temp_directory_path() : std::filesystem::path(dir)) / "r06-capture-641x359.png").string();
            REQUIRE(ImageIO::WritePNG(path, (int)w, (int)h, 4, rgba.data()));
            int pw = 0, ph = 0;
            std::vector<uint8_t> back;
            REQUIRE(ImageIO::ReadPixels(path, pw, ph, back));
            CHECK(pw == (int)w);
            CHECK(ph == (int)h);
            CHECK(back == rgba);
        }

        // And the harness Capture() (which the goldens use) agrees byte for byte.
        Image img;
        REQUIRE(Capture(fbo, img));
        CHECK(img.Width == w);
        CHECK(img.Height == h);
        CHECK(img.Rgba == rgba);
    }

    TEST_CASE("R06 HDR-to-byte — an RGBA16F target reads back converted and clamped: 2.0 -> 255, 0.25 -> 64, -1.0 -> 0")
    {
        const uint32_t w = 64, h = 32;
        Ref<FrameBuffer> hdr = MakeHdrTarget(w, h);
        REQUIRE(hdr != nullptr);

        BeginFrame(hdr, { 0.0f, 0.0f, 0.0f, 1.0f });
        // Blending OFF so the HDR value is written verbatim (no alpha weighting),
        // then restored to the engine default afterwards.
        RenderCommand::SetBlendMode(RendererAPI::BlendMode::Off);
        Renderer2D::PushRenderPass(PixelOrtho(w, h), { 0.0f, 0.0f, (float)w, (float)h });
        Renderer2D::DrawQuad(glm::vec2{ 16.0f, 16.0f }, { 24.0f, 24.0f }, { 2.0f, 0.25f, -1.0f, 1.0f });
        Renderer2D::DrawQuad(glm::vec2{ 48.0f, 16.0f }, { 24.0f, 24.0f }, { 0.5f, 4.0f, 1.0f, 0.75f });   // alpha 0.75 -> 191
        Renderer2D::PopRenderPass();
        RenderCommand::SetBlendMode(RendererAPI::BlendMode::Alpha);

        Image img;
        REQUIRE(Capture(hdr, img));
        CHECK(img.Width == w);
        CHECK(img.Height == h);
        const glm::u8vec4 a = PixelAt(img, 16, 15), b = PixelAt(img, 48, 15), c = PixelAt(img, 2, 2);
        CHECK_MESSAGE(a.r == 255, "2.0 clamps to 255: ", Describe(a));
        CHECK_MESSAGE((a.g >= 63 && a.g <= 64), "0.25 -> 64 (+-1): ", Describe(a));
        CHECK_MESSAGE(a.b == 0, "-1.0 clamps to 0: ", Describe(a));
        CHECK(a.a == 255);
        CHECK_MESSAGE((b.r >= 127 && b.r <= 128), "0.5 -> 128 (+-1): ", Describe(b));
        CHECK_MESSAGE(b.g == 255, "4.0 clamps to 255: ", Describe(b));
        CHECK(b.b == 255);
        CHECK_MESSAGE((b.a >= 191 && b.a <= 192), "alpha 0.75 -> 191 (+-1): ", Describe(b));
        CHECK(Near(c, glm::u8vec4{ 0, 0, 0, 255 }));
        WriteEvidence("r06-hdr-to-byte", img);
    }
}
