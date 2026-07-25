#pragma once

// GoldenImage.h — golden-image capture + compare (Phase 29 W2 / plan doc 28 §9.5).
//
// ============================================================================
// The regression net the engine split is measured against
// ============================================================================
//
// A golden test renders a fixed scene into an offscreen FrameBuffer, reads the
// pixels back, and compares them to a PNG committed under tests/render/goldens/.
// Every later phase of the split has to reproduce those frames exactly, which is
// what makes "the 3D build is pixel-identical" an objective claim instead of an
// assertion.
//
// ORIGIN CONVENTION. Both halves are top-left-origin RGBA8 and compose directly:
//   * FrameBuffer::ReadPixels returns row-major TOP-LEFT origin (it flips GL's
//     bottom-left rows for you) and converts/clamps HDR attachments to 8-bit.
//     The FBO MUST BE BOUND when it is called.
//   * ImageIO::ReadPixels decodes to RGBA8 top-left origin (it explicitly forces
//     stb's flip-on-load off).
// There is no flip anywhere in this file, and there must never be one.
//
// TOLERANCE. GPUs are not bit-exact across drivers and the post chain does real
// floating-point work, so a plain memcmp would be a false-alarm generator.
// A frame passes when every differing pixel is within kChannelTolerance on each
// channel, OR the pixels that exceed it are at most kPixelBudget of the frame.
// Byte-exact comparison is reserved for IN-PROCESS A/B pairs (BytesEqual), where
// both frames come off the same GPU in the same run and any difference is a real
// engine difference.
//
// ON FAILURE the actual frame is written next to the golden as <name>.actual.png
// and a diff mask as <name>.diff.png, so the eye can find the change immediately.
// ============================================================================

#include "core/Core.h"
#include "graphics/FrameBuffer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace CosmicRender
{
    // The engine's smart-pointer alias, pulled in so the signatures below read
    // like engine code instead of Cosmic::Ref<Cosmic::FrameBuffer>.
    using Cosmic::Ref;

    // Per-channel absolute difference a pixel may show and still count as equal.
    inline constexpr int kChannelTolerance = 2;

    // Fraction of the frame allowed to exceed that tolerance (0.1%).
    inline constexpr double kPixelBudget = 0.001;

    // The golden frame size. Small on purpose: the PNGs are committed, the
    // comparison is per-pixel, and 320x180 is still enough to see structure.
    inline constexpr uint32_t kGoldenWidth  = 320;
    inline constexpr uint32_t kGoldenHeight = 180;

    // ------------------------------------------------------------------------
    // Image — a top-left-origin RGBA8 frame.
    // ------------------------------------------------------------------------
    struct Image
    {
        uint32_t             Width  = 0;
        uint32_t             Height = 0;
        std::vector<uint8_t> Rgba;   // Width * Height * 4

        bool Valid() const { return Width > 0 && Height > 0 && Rgba.size() == (size_t)Width * Height * 4; }
    };

    // ------------------------------------------------------------------------
    // Harness mode
    // ------------------------------------------------------------------------

    // --update-goldens on the command line, or COSMIC_UPDATE_GOLDENS=1 in the
    // environment. In update mode CheckGolden WRITES the golden instead of
    // comparing, and every check reports as passing.
    void SetUpdateGoldens(bool update);
    bool UpdateGoldens();

    // Absolute path of tests/render/goldens (baked in by CMake as
    // COSMIC_GOLDEN_DIR). Created on demand when generating.
    const std::string& GoldenDir();

    // ------------------------------------------------------------------------
    // Capture
    // ------------------------------------------------------------------------

    // Create the standard offscreen target: kGoldenWidth x kGoldenHeight,
    // {RGBA8, DEPTH24STENCIL8}. Callers that need a different attachment set
    // build their own FrameBuffer and still capture through Capture().
    Ref<Cosmic::FrameBuffer> MakeTarget(uint32_t width = kGoldenWidth,
                                        uint32_t height = kGoldenHeight);

    // Read colour attachment `attachment` of `fbo` into `out`. Binds the FBO (the
    // ReadPixels precondition) and leaves it bound — the caller decides what to
    // bind next. Returns false and leaves `out` untouched on failure.
    bool Capture(const Ref<Cosmic::FrameBuffer>& fbo, Image& out, uint32_t attachment = 0);

    // ------------------------------------------------------------------------
    // Compare
    // ------------------------------------------------------------------------

    struct Diff
    {
        bool     SizeMismatch    = false;
        size_t   TotalPixels     = 0;
        size_t   DifferingPixels = 0;   // pixels exceeding kChannelTolerance
        int      MaxChannelDelta = 0;
        uint32_t FirstX = 0, FirstY = 0;   // first differing pixel (scan order)

        double DifferingFraction() const
        {
            return TotalPixels ? (double)DifferingPixels / (double)TotalPixels : 0.0;
        }
        bool Passes() const
        {
            return !SizeMismatch && DifferingFraction() <= kPixelBudget;
        }
    };

    Diff Compare(const Image& actual, const Image& expected, int channelTolerance = kChannelTolerance);

    // Exact byte equality — for in-process A/B pairs only (see the header note).
    bool BytesEqual(const Image& a, const Image& b);

    // ------------------------------------------------------------------------
    // The doctest-facing verb
    // ------------------------------------------------------------------------

    // Compare `actual` against goldens/<name>.png and report through doctest.
    //   * update mode      -> writes the golden, passes
    //   * golden missing   -> writes it, then FAILS with a message saying so (a
    //                         missing golden is not a silent pass)
    //   * mismatch         -> writes <name>.actual.png + <name>.diff.png, fails
    // Returns true when the frame matched (or was written in update mode).
    bool CheckGolden(const std::string& name, const Image& actual);

    // Write an image to <GoldenDir()>/<name>.png. Exposed for diagnostics.
    bool WriteImage(const std::string& name, const Image& image);
}
