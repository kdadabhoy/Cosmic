// GoldenImage.cpp — see GoldenImage.h.

#include "GoldenImage.h"

#include "utils/ImageIO.h"

#include <doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace Cosmic;

namespace CosmicRender
{
    namespace
    {
        bool s_Update = false;

        std::string MakeGoldenDir()
        {
            // COSMIC_GOLDEN_DIR is baked in by tests/render/CMakeLists.txt and
            // points into the SOURCE tree, so a golden written in update mode
            // lands where git can see it.
            std::string dir = COSMIC_GOLDEN_DIR;
            std::error_code ec;
            fs::create_directories(fs::path(dir), ec);
            return dir;
        }

        std::string PathFor(const std::string& name, const char* suffix)
        {
            return (fs::path(GoldenDir()) / (name + suffix)).string();
        }
    }

    void SetUpdateGoldens(bool update) { s_Update = update; }
    bool UpdateGoldens()               { return s_Update; }

    const std::string& GoldenDir()
    {
        static const std::string dir = MakeGoldenDir();
        return dir;
    }

    // ------------------------------------------------------------------------
    // Capture
    // ------------------------------------------------------------------------

    Ref<FrameBuffer> MakeTarget(uint32_t width, uint32_t height)
    {
        FramebufferSpecification spec;
        spec.Width       = width;
        spec.Height      = height;
        spec.Attachments = { FramebufferTextureFormat::RGBA8,
                             FramebufferTextureFormat::DEPTH24STENCIL8 };
        return FrameBuffer::Create(spec);
    }

    bool Capture(const Ref<FrameBuffer>& fbo, Image& out, uint32_t attachment)
    {
        if (!fbo)
            return false;

        // ReadPixels requires the FBO to be bound, and hands back row-major
        // TOP-LEFT-origin RGBA8 — the same convention ImageIO uses, so nothing
        // is flipped on either side of the comparison.
        fbo->Bind();

        Image img;
        if (!fbo->ReadPixels(attachment, img.Rgba, img.Width, img.Height))
            return false;
        if (!img.Valid())
            return false;

        out = std::move(img);
        return true;
    }

    // ------------------------------------------------------------------------
    // Compare
    // ------------------------------------------------------------------------

    Diff Compare(const Image& actual, const Image& expected, int channelTolerance)
    {
        Diff d;
        if (actual.Width != expected.Width || actual.Height != expected.Height)
        {
            d.SizeMismatch = true;
            return d;
        }

        d.TotalPixels = (size_t)actual.Width * actual.Height;
        bool haveFirst = false;

        for (size_t p = 0; p < d.TotalPixels; ++p)
        {
            int worst = 0;
            for (int c = 0; c < 4; ++c)
            {
                const int delta = std::abs((int)actual.Rgba[p * 4 + c] - (int)expected.Rgba[p * 4 + c]);
                worst = std::max(worst, delta);
            }
            d.MaxChannelDelta = std::max(d.MaxChannelDelta, worst);

            if (worst > channelTolerance)
            {
                ++d.DifferingPixels;
                if (!haveFirst)
                {
                    d.FirstX  = (uint32_t)(p % actual.Width);
                    d.FirstY  = (uint32_t)(p / actual.Width);
                    haveFirst = true;
                }
            }
        }
        return d;
    }

    bool BytesEqual(const Image& a, const Image& b)
    {
        return a.Width == b.Width && a.Height == b.Height && a.Rgba == b.Rgba;
    }

    // ------------------------------------------------------------------------
    // I/O
    // ------------------------------------------------------------------------

    bool WriteImage(const std::string& name, const Image& image)
    {
        if (!image.Valid())
            return false;
        return ImageIO::WritePNG(PathFor(name, ".png"), (int)image.Width, (int)image.Height,
                                 4, image.Rgba.data());
    }

    namespace
    {
        bool ReadGolden(const std::string& name, Image& out)
        {
            const std::string path = PathFor(name, ".png");
            std::error_code ec;
            if (!fs::exists(fs::path(path), ec))
                return false;

            int w = 0, h = 0;
            std::vector<uint8_t> rgba;
            if (!ImageIO::ReadPixels(path, w, h, rgba))
                return false;
            if (w <= 0 || h <= 0)
                return false;

            out.Width  = (uint32_t)w;
            out.Height = (uint32_t)h;
            out.Rgba   = std::move(rgba);
            return out.Valid();
        }

        // A human-readable diff mask: matching pixels keep a dimmed greyscale of
        // the expected frame, differing ones go solid magenta. Same size as the
        // frame, so it drops straight into an image viewer next to the other two.
        void WriteDiffMask(const std::string& name, const Image& actual, const Image& expected)
        {
            if (actual.Width != expected.Width || actual.Height != expected.Height)
                return;

            Image mask;
            mask.Width  = actual.Width;
            mask.Height = actual.Height;
            mask.Rgba.resize(actual.Rgba.size());

            const size_t pixels = (size_t)actual.Width * actual.Height;
            for (size_t p = 0; p < pixels; ++p)
            {
                int worst = 0;
                for (int c = 0; c < 4; ++c)
                    worst = std::max(worst, std::abs((int)actual.Rgba[p * 4 + c] -
                                                     (int)expected.Rgba[p * 4 + c]));

                if (worst > kChannelTolerance)
                {
                    mask.Rgba[p * 4 + 0] = 255;
                    mask.Rgba[p * 4 + 1] = 0;
                    mask.Rgba[p * 4 + 2] = 255;
                    mask.Rgba[p * 4 + 3] = 255;
                }
                else
                {
                    const int grey = (expected.Rgba[p * 4 + 0] +
                                      expected.Rgba[p * 4 + 1] +
                                      expected.Rgba[p * 4 + 2]) / 3 / 3;   // dimmed
                    mask.Rgba[p * 4 + 0] = (uint8_t)grey;
                    mask.Rgba[p * 4 + 1] = (uint8_t)grey;
                    mask.Rgba[p * 4 + 2] = (uint8_t)grey;
                    mask.Rgba[p * 4 + 3] = 255;
                }
            }

            ImageIO::WritePNG(PathFor(name, ".diff.png"), (int)mask.Width, (int)mask.Height,
                              4, mask.Rgba.data());
        }
    }

    // ------------------------------------------------------------------------
    // The doctest-facing verb
    // ------------------------------------------------------------------------

    bool CheckGolden(const std::string& name, const Image& actual)
    {
        if (!actual.Valid())
        {
            FAIL_CHECK("golden '" << name << "': the captured frame is empty — "
                       "the render or the read-back failed");
            return false;
        }

        if (UpdateGoldens())
        {
            const bool written = WriteImage(name, actual);
            CHECK_MESSAGE(written, "golden '", name, "': failed to WRITE ", PathFor(name, ".png"));
            MESSAGE("golden '" << name << "': WROTE " << actual.Width << "x" << actual.Height);
            return written;
        }

        Image expected;
        if (!ReadGolden(name, expected))
        {
            // Write it so the reviewer has something to look at, but FAIL — a
            // missing golden must never read as a pass.
            WriteImage(name, actual);
            FAIL_CHECK("golden '" << name << "': no committed golden at "
                       << PathFor(name, ".png") << " — the captured frame was written there. "
                       "Review it, then commit it (or re-run with --update-goldens).");
            return false;
        }

        const Diff d = Compare(actual, expected);
        if (d.Passes())
        {
            // A pass within tolerance is not the same as a bit-exact reproduction.
            // The split's compat gate is "pixel-identical", so say so out loud
            // whenever the GPU did not reproduce the golden exactly — silence here
            // means every byte matched.
            if (d.MaxChannelDelta != 0)
                MESSAGE("golden '" << name << "': matched within tolerance but NOT byte-exact — "
                        << d.DifferingPixels << " pixel(s) over tolerance, max channel delta "
                        << d.MaxChannelDelta);
            return true;
        }

        WriteImage(name + ".actual", actual);
        WriteDiffMask(name, actual, expected);

        if (d.SizeMismatch)
        {
            FAIL_CHECK("golden '" << name << "': size changed — got "
                       << actual.Width << "x" << actual.Height << ", golden is "
                       << expected.Width << "x" << expected.Height);
        }
        else
        {
            FAIL_CHECK("golden '" << name << "': " << d.DifferingPixels << " / " << d.TotalPixels
                       << " pixels differ (" << (d.DifferingFraction() * 100.0)
                       << "%, budget " << (kPixelBudget * 100.0) << "%), max channel delta "
                       << d.MaxChannelDelta << " (tolerance " << kChannelTolerance
                       << "), first at (" << d.FirstX << ", " << d.FirstY << "). Wrote "
                       << name << ".actual.png and " << name << ".diff.png");
        }
        return false;
    }
}
