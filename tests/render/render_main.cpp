// render_main.cpp — CosmicRenderTests entry point (Phase 29 W2 / plan doc 28 §9.5).
//
// Unlike CosmicTests this binary needs a real GPU: it stands up a Window (which
// creates the GL context), calls Renderer::Init, and hands control to doctest.
// Every suite then renders into an offscreen FrameBuffer and compares the frame
// against a committed PNG.
//
// The window is created at 640x360 and never shown — the engine's Window is
// constructed hidden and only Show()n explicitly, so this runs without stealing
// focus. The goldens themselves are 320x180 offscreen targets; the window size
// only has to be large enough for a valid default framebuffer.
//
// WORKING DIRECTORY. The engine loads shaders CWD-relative
// ("assets/shaders/PBR.glsl"), and the engine's POST_BUILD syncs assets/ next to
// Cosmic.dll — which is also where this exe lands. So main() chdirs to the
// executable's own directory before initializing the renderer, and the binary
// runs correctly from anywhere.
//
// Flags:
//   --update-goldens          regenerate every golden instead of comparing
//   COSMIC_UPDATE_GOLDENS=1   the same, via the environment
// plus every standard doctest flag (--test-suite=..., --reporters=..., ...).

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include "GoldenImage.h"

#include "core/Log.h"
#include "core/Window.h"
#include "renderer/Light2DRenderer.h"
#include "renderer/RenderCommand.h"
#include "renderer/Renderer2D.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    bool WantsUpdate(int argc, char** argv)
    {
        for (int i = 1; i < argc; ++i)
            if (std::strcmp(argv[i], "--update-goldens") == 0)
                return true;

        if (const char* env = std::getenv("COSMIC_UPDATE_GOLDENS"))
            return env[0] != '\0' && std::strcmp(env, "0") != 0;

        return false;
    }

    // Everything except our own flags, so doctest never sees --update-goldens
    // and reports it as unrecognized.
    std::vector<char*> DoctestArgs(int argc, char** argv)
    {
        std::vector<char*> out;
        out.reserve((size_t)argc);
        for (int i = 0; i < argc; ++i)
            if (i == 0 || std::strcmp(argv[i], "--update-goldens") != 0)
                out.push_back(argv[i]);
        return out;
    }

    void ChdirToExecutable(char** argv)
    {
        std::error_code ec;
        const fs::path exe = fs::absolute(fs::path(argv[0]), ec);
        if (ec || !exe.has_parent_path())
            return;
        fs::current_path(exe.parent_path(), ec);
    }
}

int main(int argc, char** argv)
{
    ChdirToExecutable(argv);
    Cosmic::Log::Init("logs");

    CosmicRender::SetUpdateGoldens(WantsUpdate(argc, argv));

    // The context. Constructed hidden (the engine creates every window that way
    // and Show()s it later), so nothing pops up over the user's desktop.
    auto window = std::make_unique<Cosmic::Window>(640, 360, "CosmicRenderTests");

    // No frame is ever presented here (every suite renders offscreen), so vsync
    // has nothing to throttle; it is switched off anyway so the R07 performance
    // record (WO-08) can state "vsync off" as a fact of the harness, not an
    // assumption.
    window->SetVSync(false);

    // Renderer::Init()'s body, spelled out. The Renderer facade is the one
    // renderer class without COSMIC_API, so it is not callable across the DLL
    // boundary; its two subsystem calls both are.
    Cosmic::RenderCommand::Init();
    Cosmic::Renderer2D::Init();

    // Deterministic starting state: the engine defaults the goldens were
    // captured under. Every suite sets its own pass state on top of this.
    Cosmic::RenderCommand::SetDepthTest(true);
    Cosmic::RenderCommand::SetDepthWrite(true);
    Cosmic::RenderCommand::SetBlendMode(Cosmic::RendererAPI::BlendMode::Alpha);

    std::vector<char*> args = DoctestArgs(argc, argv);

    doctest::Context context;
    context.applyCommandLine((int)args.size(), args.data());
    const int result = context.run();

    // Shut down while the context is still current — GL resource destructors
    // check for it, but the renderer's own subsystems do not. Same order
    // Renderer::Shutdown() uses.
    Cosmic::Renderer2D::Shutdown();
    Cosmic::Light2DRenderer::Shutdown();
    window.reset();

    return result;
}
