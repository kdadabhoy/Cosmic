// test_exe_resources.cpp — icon embedding into a PE exe (Phase 16 / S5).
//
// Exercises the real BeginUpdateResource/UpdateResource path on a COPY of the
// built CosmicApp.exe (never the live one). Skips cleanly when that exe isn't next
// to the test runner, so it never breaks a partial build.

#include <cstdint>
#include <filesystem>
#include <vector>

#include "doctest.h"
#include "utils/ExeResources.h"
#include "utils/ImageIO.h"

namespace fs = std::filesystem;

TEST_SUITE("ExeResources (S5)")
{
    TEST_CASE("embeds a multi-size icon into a copied exe")
    {
#ifdef _WIN32
        const fs::path src = "CosmicApp.exe";   // next to CosmicTests in build/Runtime/<cfg>
        if (!fs::exists(src))
        {
            WARN("CosmicApp.exe not next to the test runner — skipping icon-embed test.");
            return;
        }

        std::error_code ec;
        const fs::path tmpExe = fs::temp_directory_path() / "cosmic_icontest.exe";
        fs::copy_file(src, tmpExe, fs::copy_options::overwrite_existing, ec);
        REQUIRE_FALSE(ec);

        // A small opaque magenta PNG source.
        const int w = 16, h = 16;
        std::vector<uint8_t> px((size_t)w * h * 4);
        for (size_t i = 0; i < px.size(); i += 4) { px[i] = 255; px[i + 1] = 0; px[i + 2] = 255; px[i + 3] = 255; }
        const fs::path png = fs::temp_directory_path() / "cosmic_icontest.png";
        REQUIRE(Cosmic::ImageIO::WritePNG(png.string(), w, h, 4, px.data()));

        CHECK(Cosmic::ExeResources::SetIcon(tmpExe.string(), png.string()));

        fs::remove(tmpExe, ec);
        fs::remove(png, ec);
#endif
    }
}
