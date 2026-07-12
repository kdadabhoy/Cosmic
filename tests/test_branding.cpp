// test_branding.cpp — drop-a-file branding resolution order (Phase 22 / K1)
// plus the ImageIO decode/resize verbs it rides on. Headless (no GL): the
// resolver probes explicit roots in a real temp directory, and the image
// round-trip goes through stb's encoder/decoder on CPU buffers only.

#include <doctest.h>

#include "utils/Branding.h"
#include "utils/ImageIO.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace Cosmic;

namespace
{
    struct TempTree
    {
        fs::path Root;
        TempTree()
        {
            Root = fs::temp_directory_path() / "cosmic_branding_test";
            std::error_code ec;
            fs::remove_all(Root, ec);
            fs::create_directories(Root, ec);
        }
        ~TempTree()
        {
            std::error_code ec;
            fs::remove_all(Root, ec);
        }
        // Create a file (contents irrelevant — resolution probes existence only).
        std::string Touch(const fs::path& rel)
        {
            const fs::path p = Root / rel;
            std::error_code ec;
            fs::create_directories(p.parent_path(), ec);
            std::ofstream(p) << "x";
            return p.generic_string();
        }
        std::string Dir(const fs::path& rel)
        {
            const fs::path p = Root / rel;
            std::error_code ec;
            fs::create_directories(p, ec);
            return p.generic_string();
        }
    };
}

TEST_CASE("Branding::ResolveIcon honours the documented candidate order")
{
    TempTree t;

    Branding::IconQuery q;
    q.ExeDir       = t.Dir("exe");
    q.UserRoot     = t.Dir("user");
    q.ManifestIcon = t.Root.generic_string() + "/proj/art/mark.png";
    q.ProjectRoot  = t.Dir("proj");

    // Nothing exists yet -> engine default ("").
    CHECK(Branding::ResolveIcon(q).empty());

    // Populate lowest-priority first and watch each higher candidate win.
    const std::string projIcon = t.Touch("proj/icon.png");
    CHECK(Branding::ResolveIcon(q) == projIcon);

    const std::string manifestIcon = t.Touch("proj/art/mark.png");
    CHECK(Branding::ResolveIcon(q) == manifestIcon);

    const std::string userIcon = t.Touch("user/branding/icon.png");
    CHECK(Branding::ResolveIcon(q) == userIcon);

    const std::string exeIcon = t.Touch("exe/branding/icon.png");
    CHECK(Branding::ResolveIcon(q) == exeIcon);

    // Deleting the top override falls back cleanly, one candidate at a time.
    fs::remove(exeIcon);
    CHECK(Branding::ResolveIcon(q) == userIcon);
    fs::remove(userIcon);
    CHECK(Branding::ResolveIcon(q) == manifestIcon);
    fs::remove(manifestIcon);
    CHECK(Branding::ResolveIcon(q) == projIcon);
    fs::remove(projIcon);
    CHECK(Branding::ResolveIcon(q).empty());
}

TEST_CASE("Branding::ResolveIcon skips empty roots and missing directories")
{
    TempTree t;

    // Empty query -> "".
    CHECK(Branding::ResolveIcon({}).empty());

    // Roots pointing at directories that do not exist are skipped, not errors.
    Branding::IconQuery q;
    q.ExeDir      = (t.Root / "no_such_dir").generic_string();
    q.UserRoot    = (t.Root / "also_missing").generic_string();
    q.ProjectRoot = t.Dir("proj");
    const std::string projIcon = t.Touch("proj/icon.png");
    CHECK(Branding::ResolveIcon(q) == projIcon);

    // A manifest entry naming a DIRECTORY (not a file) must not resolve.
    Branding::IconQuery qd;
    qd.ManifestIcon = t.Dir("some_dir");
    CHECK(Branding::ResolveIcon(qd).empty());
}

TEST_CASE("ImageIO PNG round-trip preserves pixels (top-left origin)")
{
    TempTree t;
    const std::string png = (t.Root / "roundtrip.png").generic_string();

    // 2x2 RGBA: distinct corners so origin flips would be caught.
    const uint8_t src[2 * 2 * 4] = {
        255, 0, 0, 255,    0, 255, 0, 255,     // top row:    red, green
        0, 0, 255, 255,    255, 255, 0, 255,   // bottom row: blue, yellow
    };
    REQUIRE(ImageIO::WritePNG(png, 2, 2, 4, src));

    int w = 0, h = 0;
    std::vector<uint8_t> back;
    REQUIRE(ImageIO::ReadPixels(png, w, h, back));
    CHECK(w == 2);
    CHECK(h == 2);
    REQUIRE(back.size() == sizeof(src));
    for (size_t i = 0; i < sizeof(src); ++i)
        CHECK(back[i] == src[i]);

    // Unreadable path -> false, no crash.
    int dw = 0, dh = 0;
    std::vector<uint8_t> none;
    CHECK_FALSE(ImageIO::ReadPixels((t.Root / "missing.png").generic_string(), dw, dh, none));
}

TEST_CASE("ImageIO::ResizeRgba box-averages on shrink and copies at same size")
{
    // 4x4 checkerboard of black/white -> 2x2 box average = mid grey everywhere.
    std::vector<uint8_t> src(4 * 4 * 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
        {
            const uint8_t v = ((x + y) % 2 == 0) ? 255 : 0;
            uint8_t* p = src.data() + (static_cast<size_t>(y) * 4 + x) * 4;
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }

    std::vector<uint8_t> half(2 * 2 * 4);
    ImageIO::ResizeRgba(src.data(), 4, 4, half.data(), 2, 2);
    for (int i = 0; i < 4; ++i)
    {
        const uint8_t* p = half.data() + static_cast<size_t>(i) * 4;
        CHECK((int)p[0] == 127);   // (255+0+0+255)/4 = 127 (integer)
        CHECK((int)p[3] == 255);
    }

    // Same-size resample is an exact copy.
    std::vector<uint8_t> same(src.size());
    ImageIO::ResizeRgba(src.data(), 4, 4, same.data(), 4, 4);
    CHECK(same == src);

    // Enlarging a solid color stays that color (bilinear of a constant field).
    std::vector<uint8_t> solid(2 * 2 * 4, 200);
    std::vector<uint8_t> big(8 * 8 * 4);
    ImageIO::ResizeRgba(solid.data(), 2, 2, big.data(), 8, 8);
    for (uint8_t b : big)
        CHECK((int)b == 200);
}
