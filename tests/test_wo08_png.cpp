// test_wo08_png.cpp — WO-08 R06 (headless half): CPU PNG encode/decode.
//
// ImageIO::WritePNG / ReadPixels are the capture path behind screenshots,
// thumbnails and the golden harness. This pins:
//   * an exact 4-corner RGBA fixture survives a write/read round trip byte for
//     byte (including alpha 0 pixels with non-zero RGB — no premultiply);
//   * the file's IHDR is checked INDEPENDENTLY (signature, width, height, bit
//     depth 8, colour type 6) by parsing the bytes, not by asking ImageIO;
//   * decode orientation is checked against a PNG this test BUILDS BY HAND
//     (stored-deflate zlib stream + adler32 + crc32), so "row 0 is the top row"
//     is proven against an encoder that is not the code under test;
//   * invalid arguments and invalid paths fail cleanly and leave outputs alone;
//   * ResizeRgba's box average / copy / degenerate-size behaviour.

#include <doctest.h>

#include "utils/ImageIO.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Cosmic;
namespace fs = std::filesystem;

namespace
{
    struct Rgba { uint8_t r, g, b, a; };

    std::string TempPath(const char* name)
    {
        std::error_code ec;
        fs::path dir = fs::temp_directory_path(ec) / "cosmic-wo08-r06";
        fs::create_directories(dir, ec);
        return (dir / name).string();
    }

    // The 4-corner fixture: 7x5, distinct corners (with alpha 128 / 64 / 0), and
    // an interior gradient that makes every pixel unique.
    std::vector<uint8_t> MakeFixture(int w, int h)
    {
        std::vector<uint8_t> px((size_t)w * h * 4);
        auto set = [&](int x, int y, Rgba c) { uint8_t* p = &px[((size_t)y * w + x) * 4]; p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a; };
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                set(x, y, { (uint8_t)(20 + x * 30), (uint8_t)(40 + y * 40), (uint8_t)(200 - x * 10 - y * 5), (uint8_t)(255 - y * 3) });
        set(0, 0,         { 255, 0, 0, 255 });     // TL opaque red
        set(w - 1, 0,     { 0, 255, 0, 128 });     // TR half-alpha green
        set(0, h - 1,     { 0, 0, 255, 64 });      // BL quarter-alpha blue
        set(w - 1, h - 1, { 255, 255, 0, 0 });     // BR ALPHA ZERO with non-zero RGB
        return px;
    }

    uint32_t BE32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

    std::vector<uint8_t> ReadFile(const std::string& path)
    {
        std::ifstream f(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }

    // --- An independent minimal PNG encoder (RGBA8, one stored deflate block) ---
    uint32_t Crc32(const uint8_t* data, size_t n, uint32_t crc = 0)
    {
        static uint32_t table[256];
        static bool init = false;
        if (!init)
        {
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                table[i] = c;
            }
            init = true;
        }
        crc = ~crc;
        for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
        return ~crc;
    }

    void PutBE32(std::vector<uint8_t>& v, uint32_t x) { v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16)); v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x); }

    void PutChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data)
    {
        PutBE32(out, (uint32_t)data.size());
        std::vector<uint8_t> td(type, type + 4);
        td.insert(td.end(), data.begin(), data.end());
        out.insert(out.end(), td.begin(), td.end());
        PutBE32(out, Crc32(td.data(), td.size()));
    }

    // rows are TOP-FIRST (PNG's own convention).
    std::vector<uint8_t> EncodePngRgba(int w, int h, const std::vector<uint8_t>& rgba)
    {
        std::vector<uint8_t> out = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        std::vector<uint8_t> ihdr;
        PutBE32(ihdr, (uint32_t)w); PutBE32(ihdr, (uint32_t)h);
        ihdr.push_back(8);   // bit depth
        ihdr.push_back(6);   // colour type RGBA
        ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
        PutChunk(out, "IHDR", ihdr);

        // Raw scanlines: filter byte 0 + w*4 bytes each.
        std::vector<uint8_t> raw;
        for (int y = 0; y < h; ++y)
        {
            raw.push_back(0);
            raw.insert(raw.end(), rgba.begin() + (size_t)y * w * 4, rgba.begin() + (size_t)(y + 1) * w * 4);
        }
        // zlib: CMF/FLG, one stored block (BFINAL=1, BTYPE=00), LEN, NLEN, data, adler32.
        std::vector<uint8_t> z = { 0x78, 0x01 };
        REQUIRE(raw.size() < 65535);
        z.push_back(0x01);
        z.push_back((uint8_t)(raw.size() & 0xFF)); z.push_back((uint8_t)(raw.size() >> 8));
        z.push_back((uint8_t)(~raw.size() & 0xFF)); z.push_back((uint8_t)((~raw.size() >> 8) & 0xFF));
        z.insert(z.end(), raw.begin(), raw.end());
        uint32_t a = 1, b = 0;
        for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
        PutBE32(z, (b << 16) | a);
        PutChunk(out, "IDAT", z);
        PutChunk(out, "IEND", {});
        return out;
    }
}

TEST_CASE("WO-08 R06: a 4-corner RGBA fixture survives WritePNG -> ReadPixels byte for byte, alpha included")
{
    const int w = 7, h = 5;
    const std::vector<uint8_t> fixture = MakeFixture(w, h);
    const std::string path = TempPath("r06-fixture.png");
    std::error_code ec;
    fs::remove(path, ec);

    REQUIRE(ImageIO::WritePNG(path, w, h, 4, fixture.data()));
    REQUIRE(fs::exists(path, ec));

    int rw = -1, rh = -1;
    std::vector<uint8_t> back;
    REQUIRE(ImageIO::ReadPixels(path, rw, rh, back));
    CHECK(rw == w);
    CHECK(rh == h);
    REQUIRE(back.size() == fixture.size());
    CHECK(back == fixture);

    // Corners, by index (top-left origin): TL, TR, BL, BR.
    auto px = [&](int x, int y) { const uint8_t* p = &back[((size_t)y * w + x) * 4]; return Rgba{ p[0], p[1], p[2], p[3] }; };
    CHECK(px(0, 0).r == 255);         CHECK(px(0, 0).a == 255);
    CHECK(px(w - 1, 0).g == 255);     CHECK(px(w - 1, 0).a == 128);
    CHECK(px(0, h - 1).b == 255);     CHECK(px(0, h - 1).a == 64);
    CHECK(px(w - 1, h - 1).r == 255); CHECK(px(w - 1, h - 1).g == 255); CHECK(px(w - 1, h - 1).a == 0);   // RGB kept under alpha 0

    // Independent header check on the bytes ImageIO wrote.
    const std::vector<uint8_t> bytes = ReadFile(path);
    REQUIRE(bytes.size() > 33);
    const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    for (int i = 0; i < 8; ++i) CHECK(bytes[i] == sig[i]);
    CHECK(BE32(&bytes[8]) == 13);                 // IHDR length
    CHECK(std::string(bytes.begin() + 12, bytes.begin() + 16) == "IHDR");
    CHECK(BE32(&bytes[16]) == (uint32_t)w);
    CHECK(BE32(&bytes[20]) == (uint32_t)h);
    CHECK(bytes[24] == 8);                        // bit depth
    CHECK(bytes[25] == 6);                        // colour type 6 = RGBA
    // IHDR CRC verifies (type + data).
    CHECK(BE32(&bytes[29]) == Crc32(&bytes[12], 17));

    // RGB (3-channel) writes decode back with alpha 255.
    std::vector<uint8_t> rgb;
    for (int i = 0; i < w * h; ++i) { rgb.push_back(fixture[i * 4]); rgb.push_back(fixture[i * 4 + 1]); rgb.push_back(fixture[i * 4 + 2]); }
    const std::string path3 = TempPath("r06-fixture-rgb.png");
    REQUIRE(ImageIO::WritePNG(path3, w, h, 3, rgb.data()));
    REQUIRE(ImageIO::ReadPixels(path3, rw, rh, back));
    CHECK(rw == w); CHECK(rh == h);
    for (int i = 0; i < w * h; ++i)
    {
        CHECK(back[i * 4 + 0] == rgb[i * 3 + 0]);
        CHECK(back[i * 4 + 1] == rgb[i * 3 + 1]);
        CHECK(back[i * 4 + 2] == rgb[i * 3 + 2]);
        CHECK(back[i * 4 + 3] == 255);
    }
}

TEST_CASE("WO-08 R06: decode orientation is top-left, proven against a hand-built PNG")
{
    // 2x2: TL red, TR green, BL blue, BR white — rows written top-first as the
    // PNG spec says. If ReadPixels flipped rows, (0,0) would come back blue.
    const std::vector<uint8_t> rgba = { 255, 0, 0, 255,   0, 255, 0, 255,
                                        0, 0, 255, 255,   255, 255, 255, 255 };
    const std::vector<uint8_t> png = EncodePngRgba(2, 2, rgba);
    const std::string path = TempPath("r06-handbuilt.png");
    {
        std::ofstream f(path, std::ios::binary);
        REQUIRE(f.good());
        f.write((const char*)png.data(), (std::streamsize)png.size());
    }
    int w = 0, h = 0;
    std::vector<uint8_t> back;
    REQUIRE(ImageIO::ReadPixels(path, w, h, back));
    CHECK(w == 2);
    CHECK(h == 2);
    REQUIRE(back.size() == 16);
    CHECK(back == rgba);
    CHECK(back[0] == 255);  CHECK(back[1] == 0);    // (0,0) red  == top-left
    CHECK(back[8] == 0);    CHECK(back[10] == 255); // (0,1) blue == bottom-left

    // And the same hand-built bytes are what ImageIO produces for that image,
    // modulo compression: write it, decode both, identical pixels.
    const std::string path2 = TempPath("r06-handbuilt-roundtrip.png");
    REQUIRE(ImageIO::WritePNG(path2, 2, 2, 4, rgba.data()));
    int w2 = 0, h2 = 0;
    std::vector<uint8_t> back2;
    REQUIRE(ImageIO::ReadPixels(path2, w2, h2, back2));
    CHECK(back2 == back);
}

TEST_CASE("WO-08 R06: invalid arguments and invalid paths fail and leave the outputs untouched")
{
    const int w = 4, h = 3;
    const std::vector<uint8_t> fixture = MakeFixture(w, h);
    const std::string ok = TempPath("r06-args.png");

    CHECK_FALSE(ImageIO::WritePNG(ok, w, h, 4, nullptr));
    CHECK_FALSE(ImageIO::WritePNG(ok, 0, h, 4, fixture.data()));
    CHECK_FALSE(ImageIO::WritePNG(ok, w, 0, 4, fixture.data()));
    CHECK_FALSE(ImageIO::WritePNG(ok, -1, h, 4, fixture.data()));
    CHECK_FALSE(ImageIO::WritePNG(ok, w, -5, 4, fixture.data()));
    CHECK_FALSE(ImageIO::WritePNG(ok, w, h, 0, fixture.data()));
    CHECK_FALSE(ImageIO::WritePNG(ok, w, h, 5, fixture.data()));
    std::error_code ec;
    CHECK_FALSE(fs::exists(ok, ec));   // none of those created a file

    // A path whose directory does not exist.
    const std::string missingDir = (fs::path(TempPath("")) / "no-such-dir-wo08" / "x.png").string();
    CHECK_FALSE(ImageIO::WritePNG(missingDir, w, h, 4, fixture.data()));
    // An empty path.
    CHECK_FALSE(ImageIO::WritePNG("", w, h, 4, fixture.data()));

    // Reading: missing file, a non-image file, a truncated PNG.
    int rw = 123, rh = 456;
    std::vector<uint8_t> out = { 9, 9, 9 };
    CHECK_FALSE(ImageIO::ReadPixels(TempPath("does-not-exist.png"), rw, rh, out));
    CHECK(rw == 123); CHECK(rh == 456);
    CHECK(out == std::vector<uint8_t>{ 9, 9, 9 });

    const std::string text = TempPath("r06-not-an-image.png");
    { std::ofstream f(text, std::ios::binary); f << "this is not a PNG at all\n"; }
    CHECK_FALSE(ImageIO::ReadPixels(text, rw, rh, out));
    CHECK(rw == 123); CHECK(rh == 456);

    const std::string good = TempPath("r06-good.png");
    REQUIRE(ImageIO::WritePNG(good, w, h, 4, fixture.data()));
    const std::vector<uint8_t> bytes = ReadFile(good);
    REQUIRE(bytes.size() > 40);
    const std::string truncated = TempPath("r06-truncated.png");
    { std::ofstream f(truncated, std::ios::binary); f.write((const char*)bytes.data(), 33); }   // signature + IHDR only
    CHECK_FALSE(ImageIO::ReadPixels(truncated, rw, rh, out));
    CHECK(rw == 123); CHECK(rh == 456);

    CHECK_FALSE(ImageIO::ReadPixels("", rw, rh, out));
}

TEST_CASE("WO-08 R06: ResizeRgba — copy, box average, bilinear, and degenerate sizes are no-ops")
{
    const std::vector<uint8_t> src = { 255, 0, 0, 255,   0, 255, 0, 255,
                                       0, 0, 255, 255,   255, 255, 255, 255 };
    // Same size: copy.
    std::vector<uint8_t> dst(16, 7);
    ImageIO::ResizeRgba(src.data(), 2, 2, dst.data(), 2, 2);
    CHECK(dst == src);
    // 2x2 -> 1x1: the box average of the four.
    std::vector<uint8_t> one(4, 0);
    ImageIO::ResizeRgba(src.data(), 2, 2, one.data(), 1, 1);
    CHECK(one[0] == (255 + 0 + 0 + 255) / 4);
    CHECK(one[1] == (0 + 255 + 0 + 255) / 4);
    CHECK(one[2] == (0 + 0 + 255 + 255) / 4);
    CHECK(one[3] == 255);
    // 2x2 -> 4x4 (enlarging): the corners keep the corner colours.
    std::vector<uint8_t> big(64, 0);
    ImageIO::ResizeRgba(src.data(), 2, 2, big.data(), 4, 4);
    CHECK(big[0] == 255); CHECK(big[1] == 0);            // TL red
    CHECK(big[(3 * 4 + 3) * 4 + 0] == 255); CHECK(big[(3 * 4 + 3) * 4 + 1] == 255);   // BR white
    // Degenerate: nothing written.
    std::vector<uint8_t> untouched(16, 42);
    ImageIO::ResizeRgba(src.data(), 2, 2, untouched.data(), 0, 2);
    ImageIO::ResizeRgba(src.data(), 2, 2, untouched.data(), 2, 0);
    ImageIO::ResizeRgba(src.data(), 0, 2, untouched.data(), 2, 2);
    ImageIO::ResizeRgba(nullptr, 2, 2, untouched.data(), 2, 2);
    ImageIO::ResizeRgba(src.data(), 2, 2, nullptr, 2, 2);
    CHECK(untouched == std::vector<uint8_t>(16, 42));
}
