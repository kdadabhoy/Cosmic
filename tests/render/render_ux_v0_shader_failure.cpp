// render_ux_v0_shader_failure.cpp — UX-V0 acceptance VM02 (KI-83): a failed
// engine shader never crashes the process.
//
// Before UX-V0 a shader the driver refused made Shader::Create return nullptr and
// the engine dereferenced it (Renderer2D's batch shader at start-up: exit
// 0xC0000005 on Mesa/llvmpipe; Line.glsl / Circle.glsl at the first flush), and
// in Debug an uninitialised program id made a failed shader look valid. These
// cases feed a REAL broken shader file (fixtures/ux_v0/ux_v0_broken.glsl — an
// undeclared identifier every GLSL compiler rejects) through the production load
// path and nothing else (rule 5 — no private state is touched):
//   1. Shader::Create(<fixture>) directly: nullptr, and ONE error line naming the
//      path, the GL renderer/version and the compiler's first error line.
//   2. Renderer2D::Init with COSMIC_SHADER_OVERRIDE (the documented override in
//      Shader::Create) mapping Texture.glsl to the fixture: Init returns false with
//      the reason, and a full frame of quads/lines/circles runs without a crash
//      (the quad batch is skipped). With Line.glsl + Circle.glsl mapped instead,
//      Init succeeds, quads still draw, lines and circles are skipped. The
//      harness's normal renderer is rebuilt afterwards and checked.
//   3. The real host, CosmicApp.exe, started with Texture.glsl mapped to the
//      fixture: exit code 2 (Application::StartupFailureExitCode) and a readable
//      "could not start" message on stderr — not an access violation.
//
// Engine verbs only (no gl* / GL_* tokens: tests/check_gl_conformance.ps1 scans
// tests/). Win32 is used only to start and watch the child process in case 3.

#include "wo08_common.h"

#include "core/Application.h"   // StartupFailureExitCode
#include "core/Log.h"
#include "graphics/Shader.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using namespace Cosmic;
    using namespace CosmicRender;
    namespace fs = std::filesystem;

    // Absolute path of tests/render/fixtures/ux_v0, baked by CMake.
    std::string UxV0Fixture(const char* name)
    {
#ifdef COSMIC_UX_V0_FIXTURE_DIR
        return (fs::path(COSMIC_UX_V0_FIXTURE_DIR) / name).string();
#else
        return name;
#endif
    }

    bool Contains(const std::string& haystack, const std::string& needle)
    {
        return haystack.find(needle) != std::string::npos;
    }

    // The value inside `label'...'` in `text` ("" when absent).
    std::string QuotedAfter(const std::string& text, const std::string& label)
    {
        const size_t at = text.find(label);
        if (at == std::string::npos)
            return {};
        const size_t start = at + label.size();
        const size_t end = text.find('\'', start);
        return end == std::string::npos ? std::string() : text.substr(start, end - start);
    }

    // Every engine log line at error level or above, while in scope.
    class ErrorCapture
    {
    public:
        ErrorCapture()
        {
            m_Sink = std::make_shared<CallbackSink>(
                [this](spdlog::level::level_enum level, const std::string& line)
                {
                    if (level < spdlog::level::err)
                        return;
                    std::lock_guard<std::mutex> lock(m_Mutex);
                    m_Lines.push_back(line);
                });
            Log::AddSink(m_Sink);
        }
        ~ErrorCapture() { Log::RemoveSink(m_Sink); }

        std::vector<std::string> Matching(const std::string& a, const std::string& b = {}) const
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            std::vector<std::string> out;
            for (const std::string& line : m_Lines)
                if (Contains(line, a) && (b.empty() || Contains(line, b)))
                    out.push_back(line);
            return out;
        }

    private:
        spdlog::sink_ptr         m_Sink;
        mutable std::mutex       m_Mutex;
        std::vector<std::string> m_Lines;
    };

    // Sets an environment variable for the scope and restores the old value.
    class EnvScope
    {
    public:
        EnvScope(const char* name, const std::string& value) : m_Name(name)
        {
            char* old = nullptr;
            size_t len = 0;
            if (_dupenv_s(&old, &len, name) == 0 && old)
            {
                m_Old = std::string(old);
                free(old);
            }
            _putenv_s(name, value.c_str());
        }
        ~EnvScope() { _putenv_s(m_Name.c_str(), m_Old ? m_Old->c_str() : ""); }

    private:
        std::string                m_Name;
        std::optional<std::string> m_Old;
    };

    // Rebuild Renderer2D under the CURRENT environment; returns Init's answer.
    bool ReinitRenderer2D()
    {
        Renderer2D::Shutdown();
        return Renderer2D::Init();
    }

    // The harness's starting state (render_main.cpp) — restored after each case
    // that rebuilt the renderer, so the golden suites that follow see no change.
    void RestoreHarnessRenderer()
    {
        Renderer2D::Shutdown();
        const bool ok = Renderer2D::Init();
        REQUIRE_MESSAGE(ok, "the harness renderer did not come back: " << Renderer2D::GetInitError());
        CHECK(Renderer2D::GetInitError().empty());
        RenderCommand::SetDepthTest(true);
        RenderCommand::SetDepthWrite(true);
        RenderCommand::SetBlendMode(RendererAPI::BlendMode::Alpha);
    }

    constexpr uint32_t kW = 64, kH = 32;
    const glm::vec4 kQuadColor{ 0.90f, 0.15f, 0.15f, 1.0f };
    const glm::vec4 kLineColor{ 0.20f, 0.95f, 0.95f, 1.0f };
    const glm::vec4 kCircleColor{ 0.95f, 0.85f, 0.10f, 1.0f };

    // One frame with a quad (left), a vertical line (middle) and a disc (right)
    // in pixel space; returns the capture.
    Image DrawProbeFrame()
    {
        Ref<FrameBuffer> fbo = Wo08::MakeRgba8Target(kW, kH);
        REQUIRE(fbo != nullptr);
        Wo08::BeginFrame(fbo);
        Renderer2D::PushRenderPass(Wo08::PixelOrtho(kW, kH), { 0.0f, 0.0f, (float)kW, (float)kH });
        Renderer2D::DrawQuad(glm::vec2{ 12.0f, 16.0f }, { 12.0f, 12.0f }, kQuadColor);
        Renderer2D::DrawLine({ 32.5f, 2.0f, 0.0f }, { 32.5f, 30.0f, 0.0f }, kLineColor);
        Renderer2D::DrawCircle(glm::vec2{ 52.0f, 16.0f }, { 14.0f, 14.0f }, kCircleColor, 1.0f, 0.005f);
        Renderer2D::PopRenderPass();

        Image img;
        REQUIRE(Capture(fbo, img));
        fbo->Unbind();
        return img;
    }
}

TEST_SUITE("UX-V0 VM02")
{
    TEST_CASE("VM02 Shader::Create on a broken shader file: nullptr and ONE error line naming the path, the GL renderer/version and the first compiler error")
    {
        const std::string broken = UxV0Fixture("ux_v0_broken.glsl");
        REQUIRE(fs::exists(broken));

        ErrorCapture capture;
        Ref<Shader> shader = Shader::Create(broken);
        CHECK(shader == nullptr);

        const std::string err = Shader::GetLastCreateError();
        INFO("GetLastCreateError: " << err);
        CHECK(Contains(err, broken));
        CHECK(Contains(err, "ux_v0_undeclared"));           // the compiler's first error line
        CHECK(Contains(err, "FRAGMENT stage: "));
        const std::string renderer = QuotedAfter(err, "renderer '");
        const std::string version  = QuotedAfter(err, "OpenGL '");
        CHECK_MESSAGE(!renderer.empty(), "no renderer string in: " << err);
        CHECK_MESSAGE(!version.empty(), "no version string in: " << err);
        CHECK(renderer != "unknown");
        CHECK(version != "unknown");

        const std::vector<std::string> lines = capture.Matching("Shader::Create:", broken);
        CHECK(lines.size() == 1);
        if (!lines.empty())
        {
            CHECK(Contains(lines[0], "ux_v0_undeclared"));
            CHECK(Contains(lines[0], renderer));
            CHECK(Contains(lines[0], version));
        }

        // A later successful Create clears the reason.
        Ref<Shader> good = Shader::Create("assets/shaders/Line.glsl");
        CHECK(good != nullptr);
        CHECK(Shader::GetLastCreateError().empty());

        // A missing file says so (and still no crash, no program).
        Ref<Shader> missing = Shader::Create(UxV0Fixture("ux_v0_does_not_exist.glsl"));
        CHECK(missing == nullptr);
        CHECK(Contains(Shader::GetLastCreateError(), "could not read the file"));
    }

    TEST_CASE("VM02 Renderer2D with a broken batch shader: Init returns false with the reason and a full frame runs without a crash; broken line/circle shaders leave quads drawing")
    {
        const std::string broken = UxV0Fixture("ux_v0_broken.glsl");
        REQUIRE(fs::exists(broken));

        SUBCASE("batch quad shader (Texture.glsl) broken: fatal for Init, never a null dereference")
        {
            {
                EnvScope env("COSMIC_SHADER_OVERRIDE", "Texture.glsl=" + broken);
                ErrorCapture capture;
                CHECK_FALSE(ReinitRenderer2D());
                const std::string why = Renderer2D::GetInitError();
                INFO("GetInitError: " << why);
                CHECK(Contains(why, "assets/shaders/Texture.glsl"));
                CHECK(Contains(why, "ux_v0_broken.glsl"));
                CHECK(Contains(why, "ux_v0_undeclared"));
                CHECK(capture.Matching("Shader::Create:", "Texture.glsl").size() == 1);

                // Every batch verb on the failed renderer: no crash; the quad
                // batch is skipped (the quad's pixels keep the clear colour).
                const Image img = DrawProbeFrame();
                CHECK(Wo08::Near(Wo08::PixelAtGl(img, 12, 16), Wo08::kClearU8));
            }
            RestoreHarnessRenderer();
        }

        SUBCASE("line + circle shaders broken: Init succeeds, quads draw, lines and circles are skipped")
        {
            {
                EnvScope env("COSMIC_SHADER_OVERRIDE", "Line.glsl=" + broken + ";Circle.glsl=" + broken);
                CHECK(ReinitRenderer2D());
                CHECK(Renderer2D::GetInitError().empty());

                const Image img = DrawProbeFrame();
                const glm::u8vec4 quad = Wo08::PixelAtGl(img, 12, 16);
                CHECK_MESSAGE(Wo08::Near(quad, Wo08::ToU8(kQuadColor)), "quad pixel " << Wo08::Describe(quad));
                CHECK(Wo08::Near(Wo08::PixelAtGl(img, 32, 16), Wo08::kClearU8));   // line skipped
                CHECK(Wo08::Near(Wo08::PixelAtGl(img, 52, 16), Wo08::kClearU8));   // circle skipped
            }
            RestoreHarnessRenderer();

            // Control: the same frame on the restored renderer draws all three,
            // so the probes above really look at the line and the disc.
            const Image img = DrawProbeFrame();
            CHECK(Wo08::Near(Wo08::PixelAtGl(img, 12, 16), Wo08::ToU8(kQuadColor)));
            CHECK_FALSE(Wo08::Near(Wo08::PixelAtGl(img, 32, 16), Wo08::kClearU8));
            CHECK_FALSE(Wo08::Near(Wo08::PixelAtGl(img, 52, 16), Wo08::kClearU8));
        }
    }

    TEST_CASE("VM02 start-up: CosmicApp.exe with a broken batch shader exits cleanly with code 2 and says why (no access violation)")
    {
        const std::string broken = UxV0Fixture("ux_v0_broken.glsl");
        REQUIRE(fs::exists(broken));

        wchar_t self[MAX_PATH]{};
        REQUIRE(GetModuleFileNameW(nullptr, self, MAX_PATH) != 0);
        const fs::path runtimeDir = fs::path(self).parent_path();
        const fs::path host = runtimeDir / L"CosmicApp.exe";
        REQUIRE_MESSAGE(fs::exists(host), "build the CosmicApp target: " << host.string());

        // The child's stdout + stderr go to one file (a pipe could fill and block it).
        std::error_code ec;
        fs::create_directories(runtimeDir / L"logs", ec);
        const fs::path outPath = runtimeDir / L"logs" / L"ux_v0_vm02_cosmicapp_output.txt";
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
        HANDLE out = CreateFileW(outPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        REQUIRE(out != INVALID_HANDLE_VALUE);

        DWORD exitCode = 0;
        bool finished = false;
        {
            // Inherited by the child: the override, and no dialog (stderr is a
            // file here, so none would show anyway — belt and braces).
            EnvScope overrideEnv("COSMIC_SHADER_OVERRIDE", "Texture.glsl=" + broken);
            EnvScope noDialog("COSMIC_NO_FATAL_DIALOG", "1");

            STARTUPINFOW si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdInput = nullptr;
            si.hStdOutput = out;
            si.hStdError = out;
            PROCESS_INFORMATION pi{};
            std::wstring cmd = L"\"" + host.wstring() + L"\"";
            const BOOL started = CreateProcessW(host.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                                CREATE_NO_WINDOW, nullptr, runtimeDir.c_str(), &si, &pi);
            CloseHandle(out);
            REQUIRE_MESSAGE(started, "CreateProcess failed: " << GetLastError());

            // llvmpipe start-up is slow (window + context ~2-4 s); a hang is a failure.
            finished = WaitForSingleObject(pi.hProcess, 120000) == WAIT_OBJECT_0;
            if (!finished)
                TerminateProcess(pi.hProcess, 0xDEAD);
            GetExitCodeProcess(pi.hProcess, &exitCode);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }

        std::ifstream in(outPath, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string output = ss.str();
        INFO("CosmicApp exit code " << exitCode << " (0x" << std::hex << exitCode << std::dec << "); output tail:\n"
             << (output.size() > 3000 ? output.substr(output.size() - 3000) : output));

        REQUIRE_MESSAGE(finished, "CosmicApp did not exit within 120 s");
        CHECK(exitCode != 0xC0000005u);                     // the KI-83 access violation
        CHECK(exitCode == static_cast<DWORD>(Application::StartupFailureExitCode));
        CHECK(Contains(output, "could not start"));
        CHECK(Contains(output, "Texture.glsl"));
        CHECK(Contains(output, "ux_v0_undeclared"));
        CHECK(Contains(output, "renderer '"));
    }
}
