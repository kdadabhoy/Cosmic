#include "Cosmic.h"
#include "core/Application.h"
#include "core/Version.h"
#include "utils/FileSystem.h"
#include "renderer/Renderer.h"
#include "renderer/RenderCommand.h"
#include "core/Timestep.h"
#include "graphics/FrameBuffer.h"
#include "core/Log.h"
#include "layers/WorkspaceLayer.h"
#include "layers/LauncherLayer.h"
#include "ui/ThemeManager.h"   // project://themes rescan on project mount
#include "ui/Fonts.h"          // project://fonts rescan (ImGui atlas) on project mount
#include "graphics/Font.h"     // project://fonts rescan (SDF library) on project mount
#include "imgui_internal.h"


// Note: glfw3.h is kept only for glfwGetTime() in the Run() loop.
#include <GLFW/glfw3.h>

#include <algorithm>

// Wrap Windows.h to isolate polluting win32 macro definitions
// Note: WIN32_LEAN_AND_MEAN removed here because it is already declared via the command line compiler flags
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	// Global pointer to the application instance allowing subsystems to access engine methods
	Application* Application::s_Instance = nullptr;

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * ResolveProjectDLLPath
	 * Resolves a project name ("SF_Telem"), DLL filename ("SF_Telem.dll"), or absolute
	 * path to an existing DLL file. Search order matches the Launcher's discovery:
	 * <exeDir>/projects/ first (packaged dist layout), then <exeDir> (dev build layout).
	 * Returns "" (and logs an error) when no candidate exists on disk.
	 */
	static std::string ResolveProjectDLLPath(const std::string& nameOrPath)
	{
		namespace fs = std::filesystem;

		fs::path request(nameOrPath);
		if (request.extension() != ".dll")
			request += ".dll";

		if (request.is_absolute())
		{
			if (fs::exists(request))
				return request.string();
			CS_CORE_ERROR("Project DLL not found: '{0}'", request.string());
			return "";
		}

		const fs::path cwd = fs::current_path();
		const fs::path candidates[] = { cwd / "projects" / request, cwd / request };
		for (const fs::path& c : candidates)
		{
			if (fs::exists(c))
				return c.string();
		}

		CS_CORE_ERROR("Project DLL not found: '{0}' (searched '{1}' and '{2}')",
			nameOrPath, candidates[0].string(), candidates[1].string());
		return "";
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Constructor
	 * Sets up the engine singleton, initializes the core Logger, and triggers
	 * the internal subsystem initialization sequence.
	 */
	Application::Application(const std::string& startupProjectDll)
		: m_Running(true), m_Minimized(false), m_UseFixedTimestep(true), m_TimeScale(1.0f), m_ImGuiLayer(nullptr)
	{
		// Must be assigned before Initialize() below — it decides Launcher vs project.
		m_StartupProjectDLL = startupProjectDll;

		// Logs go to the writable user-data root: "logs/" next to the exe in a dev
		// tree (portable mode), %LOCALAPPDATA%/Cosmic/logs when installed under a
		// read-only location like Program Files. See FileSystem::GetUserDataRoot().
		Log::Init(FileSystem::Resolve("user://logs"));

		CS_CORE_INFO("=================================================");
		CS_CORE_INFO("  Cosmic Engine v{0} — Subsystems Initialized", COSMIC_VERSION_STRING);
		CS_CORE_INFO("=================================================");
		CS_CORE_INFO("User data root: {0}", FileSystem::GetUserDataRoot());

		s_Instance = this;
		Initialize();
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Destructor
	 * Ensures that the shutdown sequence is called to release hardware resources.
	 */
	Application::~Application()
	{
		Shutdown();
	}

	/////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Run()
	 * 
	 * THE CORE APPLICATION LOOP (The Engine's Heartbeat)
	 *
	 * Controls the primary execution cycle of the engine. It handles window polling,
	 * orchestrates the semi-implicit fixed timestep accumulator for physics/logic updates,
	 * runs variable update passes for visuals, and executes the master ImGui render loop.
	 * 
	 * Note... THE SAFE ZONE: The bottom section of the loop guarantees that no iterations
	 * are currently operating over the LayerStack. This provides a safe, synchronization-free
	 * environment to safely push, pop, allocate, or delete unmanaged layers without invalidating
	 * active iterators or throwing context-teardown execution errors.
	 */
	void Application::Run()
	{
		// Seed the shared frame clock so the first frame's dt is ~0 rather than
		// the full boot duration. Shared with the modal-loop frame pump.
		m_LastFrameTime = (float)glfwGetTime();

		while (m_Running && !m_Window->ShouldClose())
		{
			m_Window->PollEvents();

			// The per-frame body lives in RenderSingleFrame() so the Win32 modal
			// move/size loop can pump it via WM_TIMER (responsive drag/resize)
			// and fullscreen toggles can present a correctly-sized frame within
			// the same transition. Returns false while minimized-and-paused.
			RenderSingleFrame();

			// =================================================================
			// THE SAFE ZONE: Guaranteed zero-iteration window on m_LayerStack.
			// Runs even while minimized, so a project transition queued just
			// before minimizing does not stall until the window is restored.
			// Deliberately NOT part of RenderSingleFrame(): no DLL load/unload
			// or layer push/pop may run from inside the modal-loop frame pump.
			// =================================================================
			ProcessDeferredTransitions();
		}
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * RenderSingleFrame — one full engine frame (see Application.h docs).
	 *
	 * Pause semantics (docs/design/responsive-rendering-and-pause.md):
	 *   - PASS 1A (fixed) is SKIPPED while paused — the accumulator does not
	 *     advance, so no catch-up burst on Resume().
	 *   - PASS 1B runs with dt = 0 — OnUpdate ISSUES DRAW CALLS in this engine,
	 *     so it must keep running or the scene goes black; dt = 0 freezes motion.
	 *   - PASS 2 (ImGui) + SwapBuffers run normally — pause menus stay live.
	 *   - m_AbsoluteTime keeps advancing (scale-independent uptime);
	 *     GetLocalTime()-driven animation freezes (advances by 0).
	 */
	bool Application::RenderSingleFrame()
	{
		// Re-entrancy guard: a fullscreen toggle inside a layer's OnUpdate fires
		// an immediate frame request; SendMessage edge cases in the modal pump
		// are ruled out for free.
		if (m_InFrameTick)
			return true;
		m_InFrameTick = true;

		float time = (float)glfwGetTime();
		Timestep rawTimestep = time - m_LastFrameTime;
		m_LastFrameTime = time;
		m_AbsoluteTime += rawTimestep.GetSeconds();

		// Skip execution passes while minimized (default). Disabled via
		// SetPauseOnMinimize(false). Run() still processes the Safe Zone.
		if (m_Minimized && m_PauseOnMinimize)
		{
			m_InFrameTick = false;
			return false;
		}


		// -----------------------------------------------------------------
		// PASS 1A: Fixed Timestep Updates (Deterministic Logic / Physics)
		// Skipped entirely while paused (pure logic — nothing to draw).
		// -----------------------------------------------------------------
		if (m_UseFixedTimestep && !m_Paused)
		{
			// The interval derives from the configurable rate (default 60 Hz —
			// see SetFixedTimestepHz). Sampled once per frame so a rate change
			// mid-frame cannot tear the accumulator loop.
			const float fixedDeltaTime = 1.0f / m_FixedTimestepHz;

			float frameTime = rawTimestep.GetSeconds();

			// Spiral-of-death panic protection clamping
			if (frameTime > 0.25f)
			{
				frameTime = 0.25f;
			}

			m_Accumulator += (frameTime * m_TimeScale);

			// Signed so layers receive a negative dt during rewind (TimeScale < 0)
			const float signedFixedDelta = m_TimeScale >= 0.f ? fixedDeltaTime : -fixedDeltaTime;

			m_LayerStack.SetIterating(true);
			while (m_Accumulator >= fixedDeltaTime)
			{
				for (Layer* layer : m_LayerStack)
				{
					layer->OnFixedUpdate(signedFixedDelta);
				}
				m_Accumulator -= fixedDeltaTime;
			}
			m_LayerStack.SetIterating(false);
		}


		// -----------------------------------------------------------------
		// PASS 1B: Variable Timestep Updates (Animations & Visual States)
		// While paused the pass still runs — with dt = 0 — because OnUpdate
		// is where world rendering happens; skipping it would blank the scene.
		// -----------------------------------------------------------------
		Timestep scaledTimestep = m_Paused ? 0.0f : rawTimestep.GetSeconds() * m_TimeScale;
		m_LayerStack.SetIterating(true);
		for (Layer* layer : m_LayerStack)
		{
			// 1. Core Engine updates the layer's local timeline automatically
			layer->UpdateLayerTime(scaledTimestep.GetSeconds());

			// 2. Client code runs its standard frame updates
			layer->OnUpdate(scaledTimestep.GetSeconds());
		}
		m_LayerStack.SetIterating(false);


		// -----------------------------------------------------------------
		// PASS 2: Main UI Rendering and Dockspace Composition
		// -----------------------------------------------------------------
		m_ImGuiLayer->Begin();
		m_LayerStack.SetIterating(true);
		for (Layer* layer : m_LayerStack)
		{
			layer->OnImGuiRender();
		}
		m_LayerStack.SetIterating(false);
		m_ImGuiLayer->End();

		m_Window->SwapBuffers();

		m_InFrameTick = false;
		return true;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * ProcessDeferredTransitions — THE SAFE ZONE body.
	 *
	 * Applies all deferred layer/DLL transitions. Only callable from points in the
	 * frame where no LayerStack iteration is active: the bottom of Run()'s loop, and
	 * the minimized early-out (so queued transitions don't stall while minimized).
	 */
	void Application::ProcessDeferredTransitions()
	{
		// --- Handle Return to Launcher Request ---
		if (m_PendingReturnToLauncher)
		{
			// Unlink guest library assemblies before cleaning host panels
			UnloadProjectDLL();

			if (m_WorkspaceLayer)
			{
				// Notify WorkspaceLayer to begin its multi-stage ImGui cleanup sequence.
				// Allocation destruction is deferred until it flags readiness.
				m_WorkspaceLayer->RequestLayoutReset();
			}

			m_PendingReturnToLauncher = false;
		}

		// Deferred destruction sequence for Workspace allocations
		if (m_WorkspaceLayer && m_WorkspaceLayer->IsReadyForDeletion())
		{
			m_LayerStack.PopLayer(m_WorkspaceLayer);
			delete m_WorkspaceLayer;
			m_WorkspaceLayer = nullptr;

			// Swap active display modes back to the Launcher Hub context
			PushLayer(new LauncherLayer());

			// Force state synchronization to eliminate ImGui dockspace caching artifacts
			SynchronizeRenderingState();
		}

		// --- Handle Project Workspace Redirection Requests (.dll loading) ---
		if (!m_PendingProjectDLL.empty())
		{
			// 0. Validate BEFORE tearing the Launcher down, so a bad --project flag
			//    or a missing DLL degrades to the Launcher instead of a dead workspace.
			const std::string resolved = ResolveProjectDLLPath(m_PendingProjectDLL);
			m_PendingProjectDLL = "";

			if (resolved.empty())
			{
				// Direct-boot (--project) never pushed a Launcher — make sure one
				// exists to land on. The launcher-click path always has one already.
				bool hasLauncher = false;
				for (Layer* layer : m_LayerStack)
				{
					if (dynamic_cast<LauncherLayer*>(layer)) { hasLauncher = true; break; }
				}
				if (!hasLauncher)
					PushLayer(new LauncherLayer());
				return;
			}

			// 1. Locate and strip out the legacy Launcher context layer
			LauncherLayer* launcherTarget = nullptr;
			for (Layer* layer : m_LayerStack)
			{
				if (auto* launcher = dynamic_cast<LauncherLayer*>(layer))
				{
					launcherTarget = launcher;
					break;
				}
			}

			if (launcherTarget)
			{
				m_LayerStack.PopLayer(launcherTarget);
				delete launcherTarget;
			}

			// 2. Initialize and push the master workspace platform
			m_WorkspaceLayer = new WorkspaceLayer();
			PushLayer(m_WorkspaceLayer);

			// 3. Mount guest assembly definitions directly onto the target workspace panel
			LoadProjectDLL(resolved);
		}
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Engine Shutdown
	 *
	 * Orchestrates a deterministic, staggered "soft-landing" cleanup sequence for all
	 * active hardware contexts, dynamic allocations, and modules.
	 *
	 * ENGINE MEMORY OWNERSHIP & LIFECYCLE POLICY:
	 * The Application class maintains ABSOLUTE OWNERSHIP of unmanaged heap-allocated
	 * layers (e.g., `LauncherLayer`, `WorkspaceLayer`). To guarantee that GPU assets
	 * (Textures, Framebuffers) delete themselves while an active OpenGL context exists,
	 * memory destruction is executed in a highly controlled, multi-stage sequence.
	 *
	 * PIPELINE CLEANUP FLOW:
	 * 1. Detach and unmount active dynamic Project DLL modules.
	 * 2. Pop scope-managed overlayers (ImGui) to prevent raw-pointer double deletions.
	 * 3. Snapshot remaining active layers into a temporary local sequence cache.
	 * 4. Evacuate LayerStack tracking arrays to invalidate update/event access loops.
	 * 5. Iteratively delete unmanaged heap layer memory instances.
	 * 6. Dissolve ImGui subsystems, close the UI window, and terminate the graphics context.
	 */
	void Application::Shutdown()
	{
		CS_CORE_TRACE("Shutting down Application Subsystems...");

		// 0. Unloading threads
		Cosmic::JobSystem::Get().Shutdown();

		// 1. Unload the project DLL runtime if it's still attached
		UnloadProjectDLL();

		// 2. Extract Scope-owned overlays (ImGui) from the LayerStack matrix
		// This shields the unique_ptr raw address from being processed in raw delete passes.
		if (m_ImGuiLayer)
		{
			m_LayerStack.PopOverlay(m_ImGuiLayer.get());
		}

		// 3. Cache references to remaining unmanaged app-level layers (e.g., LauncherLayer)
		std::vector<Layer*> layersToDelete;
		for (Layer* layer : m_LayerStack)
		{
			layersToDelete.push_back(layer);
		}

		// 4. Clear the active LayerStack immediately.
		// By emptying tracking vectors now, we guarantee that no stray events or threads 
		// can step through dangling pointer ranges during the upcoming deletion process.
		m_LayerStack.ForceCleanForShutdown();

		// 5. Execute explicit memory destruction on unmanaged layers.
		// This triggers layer destructors, releasing graphics assets (Textures, Shaders)
		// safely while the hardware OpenGL window context is completely alive.
		for (Layer* layer : layersToDelete)
		{
			delete layer;
		}
		layersToDelete.clear();

		// 6. Dissolve unique-scoped UI systems while graphics contexts are hot
		m_ImGuiLayer.reset();

		// 7. Flush static engine rendering layers and hardware structures.
		// Audio goes first: all layer-owned Sound Refs are gone by now (step 5),
		// and the device graph must close before the window/context teardown.
		AudioEngine::Shutdown();
		Renderer::Shutdown();

		// 8. Safely close physical window frames and dismantle the OpenGL core context
		m_Window.reset();

		CS_CORE_TRACE("Application Subsystems safely terminated.");
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Global Event Entry Point
	 * Handles top-level window events and propagates remaining events through the LayerStack.
	 */
	void Application::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);

		// Handling "global" application events
		dispatcher.Dispatch<WindowCloseEvent>([this](WindowCloseEvent& e) { return OnWindowClose(e); });
		dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& e) { return OnWindowResize(e); });

		// Propagate events down the layer stack (top to bottom)
		m_LayerStack.SetIterating(true);
		for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
		{
			if (e.Handled)
			{
				break;
			}

			(*it)->OnEvent(e);
		}
		m_LayerStack.SetIterating(false);
	}

	/////////////////////////////////////////////////////////////////////////////////

	glm::vec2 Application::GetViewportPos() const
	{
		return m_WorkspaceLayer ? m_WorkspaceLayer->GetViewportPos() : glm::vec2{ 0.0f, 0.0f };
	}

	glm::vec2 Application::GetViewportSize() const
	{
		return m_WorkspaceLayer ? m_WorkspaceLayer->GetViewportSize() : glm::vec2{ 0.0f, 0.0f };
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Logic Layer Registration
	 */
	void Application::PushLayer(Layer* inLayer)
	{
		m_LayerStack.PushLayer(inLayer);
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Overlay Registration
	 */
	void Application::PushOverlay(Layer* inOverlay)
	{
		m_LayerStack.PushOverlay(inOverlay);
	}

	/////////////////////////////////////////////////////////////////////////////////

	void Application::TransitionFromLauncherToWorkspace(const std::string& dllPath)
	{
		m_PendingProjectDLL = dllPath; // Just cache the request
	}

	/////////////////////////////////////////////////////////////////////////////////

	void Application::TransitionToLauncher()
	{
		m_PendingReturnToLauncher = true; // Set the flag to process in the Safe Zone
	}

	/////////////////////////////////////////////////////////////////////////////////

	void Application::SetFixedTimestepHz(float hz)
	{
		// Clamp to a sane band: below 1 Hz the accumulator starves; above 1000 Hz the
		// per-tick overhead of ticking every layer dominates (prefer app-side substepping).
		const float clamped = std::clamp(hz, 1.0f, 1000.0f);
		if (clamped != hz)
			CS_CORE_WARN("SetFixedTimestepHz({0}) clamped to {1} Hz.", hz, clamped);
		m_FixedTimestepHz = clamped;
	}

	Application& Application::Get()
	{
		return *s_Instance;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Internal Initialization
	 * Orchestrates the creation of the Window, Renderer, Framebuffer, and ImGui.
	 * Returns: true if all subsystems started successfully.
	 */
	void Application::Initialize()
	{
		CS_CORE_TRACE("Initializing Application Subsystems...");

		// =====================================================================
		// INITIALIZE THE JOB SYSTEM MULTITHREADING POOL FIRST
		// =====================================================================
		Cosmic::JobSystem::Get().Initialize();

		// Audio right after the JobSystem (doc 08 A1). Headless-safe: a failed
		// device init logs a warning and the subsystem becomes a no-op.
		Cosmic::AudioEngine::Init();

		// 1. Create the window 
		m_Window = CreateScope<Window>(DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_WINDOW_TITLE);
		m_Window->SetEventCallback([this](Event& e) { OnEvent(e); });

		// --- THE CRITICAL HAZEL FIX ---
		// Explicitly lock your frame present scheduling onto your primary monitor refresh rate on boot!
		m_Window->SetVSync(true);

		// 2. Initialize the Renderer
		Renderer::Init();

		// 3. Framebuffer Setup 
		FramebufferSpecification fbSpec;
		fbSpec.Width = DEFAULT_WIDTH;
		fbSpec.Height = DEFAULT_HEIGHT;
		m_Framebuffer = FrameBuffer::Create(fbSpec);

		// 4. Initialize ImGui Frame Layer Context Overlay
		m_ImGuiLayer = CreateScope<ImGuiLayer>();
		PushOverlay(m_ImGuiLayer.get());

		// 5. Boot into the Launcher — unless a startup project was requested (the
		//    --project flag). Direct boot routes through the same pending-DLL Safe
		//    Zone path the Launcher uses, so the first frame performs the load with
		//    the proven transition machinery; a missing DLL falls back to the Launcher.
		if (!m_StartupProjectDLL.empty())
		{
			CS_CORE_INFO("Startup project requested: '{0}' — skipping the Launcher.", m_StartupProjectDLL);
			m_PendingProjectDLL = m_StartupProjectDLL;
		}
		else
		{
			PushLayer(new LauncherLayer());
		}

		// 6. Sync the renderer to the TRUE framebuffer size now that the window,
		//    callbacks, framebuffer, and layers all exist. The window enables
		//    borderless custom chrome during construction, which enlarges the client
		//    area beyond the requested DEFAULT_WIDTH/HEIGHT — but that resize fires into
		//    a no-op callback (the real EventCallback isn't installed yet) and the GL
		//    viewport defaults to the size at context creation. Without this, the engine
		//    renders at a stale viewport until the first user resize / F11 toggle.
		//    SynchronizeRenderingState() queries glfwGetFramebufferSize and drives a
		//    WindowResizeEvent through OnWindowResize (FBO resize + glViewport).
		SynchronizeRenderingState();

		// 7. Frame pump hookup: lets the window request full engine frames from
		//    inside the Win32 modal move/size loop (responsive drag/resize —
		//    default on) and immediately after fullscreen transitions (paint-
		//    through-transition). Everything RenderSingleFrame touches exists by
		//    this point. The Window clears this callback in its destructor.
		m_Window->SetModalFrameCallback([this] { RenderSingleFrame(); });
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Responsive-drag toggle — forwards to the Window's modal frame pump.
	 * Default on; a client that wants the old freeze-while-dragging behavior
	 * (e.g. a minimal/low-power tool) calls SetRenderWhileDragging(false).
	 */
	void Application::SetRenderWhileDragging(bool enabled)
	{
		if (m_Window)
			m_Window->SetModalRenderingEnabled(enabled);
	}

	bool Application::IsRenderWhileDragging() const
	{
		return m_Window && m_Window->IsModalRenderingEnabled();
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Window Close Handler
	 */
	bool Application::OnWindowClose(WindowCloseEvent& e)
	{
		m_Running = false;
		return true;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Window Resize Handler
	 */
	bool Application::OnWindowResize(WindowResizeEvent& e)
	{
		if (e.GetWidth() == 0 || e.GetHeight() == 0)
		{
			m_Minimized = true;
			return false;
		}

		m_Minimized = false;
		m_Framebuffer->Resize(e.GetWidth(), e.GetHeight());
		Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());

		return false;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * LoadProjectDLL(const std::string& filepath)
	 *
	 * Dynamically Loads a Client Project Assembly Module (DLL)
	 * 
	 * This handles the runtime plugin linking process. It maps a client project DLL into
	 * the engine's virtual memory space, hooks into its export signatures, shares
	 * rendering state handles across compilation boundaries, and initializes the client logic.
	 *
	 * @param filepath The dynamic path or name of the `.dll` module to load.
	 * 
	 * @note LIFECYCLE MANAGEMENT: The returned dynamic `Layer*` from the plugin is completely
	 * unmanaged by smart pointers across the DLL boundary. Ultimate lifecycle management and
	 * deletion tracking are held explicitly by the host application's `m_ActivePluginLayer`.
	 */
	void Application::LoadProjectDLL(const std::string& filepath)
	{
		if (m_PluginHandle) UnloadProjectDLL();

		// 1. Resolve the actual DLL location. Project DLLs live in the "projects/"
		//    subfolder in the packaged dist layout, but land flat next to the exe in
		//    dev builds — ResolveProjectDLLPath tries projects/ first, then the exe
		//    dir, and accepts absolute paths. Cosmic.dll resolves from the exe dir
		//    for either location via the default loader search order.
		const std::string resolved = ResolveProjectDLLPath(filepath);
		if (resolved.empty())
			return;   // helper already logged the error

		// 2. Load the DLL into Cosmic's virtual address memory space
		HMODULE handle = LoadLibraryA(resolved.c_str());
		if (!handle)
		{
			CS_CORE_ERROR("Failed to load plugin: {0}", resolved);
			return;
		}

		// 2. Locate dynamic linkage hooks and engine export signatures
		auto initContexts = (void(*)(HostContext))GetProcAddress(handle, "InitializePluginContexts");
		auto createPluginLayer = (Cosmic::Layer * (*)())GetProcAddress(handle, "CreatePluginLayer");

		if (!initContexts || !createPluginLayer)
		{
			CS_CORE_ERROR("Plugin is missing required engine export signatures!");
			FreeLibrary(handle);
			return;
		}

		m_PluginHandle = handle;

		// 3. Context Sharing Architecture
		HostContext ctx;
		ctx.ImGuiCtx = ImGui::GetCurrentContext();
		ctx.ImPlotCtx = ImPlot::GetCurrentContext();
		initContexts(ctx);

		// 4. Instantiate the plugin layer and assign it as the workspace viewport focus
		m_ActivePluginLayer = createPluginLayer();

		if (!m_ActivePluginLayer)
		{
			CS_CORE_ERROR("Plugin's CreatePluginLayer() returned nullptr — aborting load.");
			FreeLibrary(handle);
			m_PluginHandle = nullptr;
			return;
		}

		// 5. Engine-side VFS binding. FileSystem is header-only with per-DLL
		//    static state, so the client calling SetActiveProject in its OnAttach
		//    only ever updates the CLIENT DLL's copy — engine-compiled code that
		//    resolves "project://" (theme registry, font libraries, Config::Load)
		//    used to resolve against an empty project name forever. Set the ENGINE
		//    copy from the DLL stem (== assets/projects/<stem> by the CMake asset-
		//    sync convention) BEFORE mounting, so engine-side resolution works even
		//    during the client's OnAttach.
		const std::string displayName = std::filesystem::path(filepath).stem().string();
		FileSystem::SetActiveProject(displayName);

		if (m_WorkspaceLayer)
		{
			m_WorkspaceLayer->SetViewportLayer(m_ActivePluginLayer);
			m_WorkspaceLayer->SetProjectName(displayName);

			CS_CORE_INFO("WorkspaceLayer project name set to: '{0}'", displayName);
			CS_CORE_INFO("Successfully loaded and mounted project DLL Layer!");
		}
		else
		{
			CS_CORE_WARN("Plugin Layer created but no active Workspace target was found to bind it to.");
		}

		// 6. Project-scoped theme/font rescan. ThemeManager::Init and Fonts::Init
		//    run at ImGuiLayer attach — BEFORE any project is mounted — so they can
		//    never see project://themes or project://fonts; this is the hook that
		//    actually loads them. Runs in the Safe Zone between frames, where adding
		//    ImGui fonts is safe: ImGui 1.92's dynamic atlas (RendererHasTextures)
		//    bakes new glyphs on demand, no upfront atlas rebuild required. All
		//    three rescans are idempotent (registries dedupe/replace by name).
		ThemeManager::LoadFolder(FileSystem::Resolve("project://themes"));
		UI::Fonts::LoadProjectFonts();
		Font::LoadProjectFonts();
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * UnloadProjectDLL()
	 * 
	 * Gracefully Unmounts and Purges the Client Project DLL Module
	 * * Safely down-steps the active plugin. It disengages viewport references, calls individual
	 * destructors to allow the DLL to release its independent graphics subsystem allocations,
	 * frees the module handle from memory, and scrubs active tracking pointers to prevent
	 * context destruction race conditions.
	 */
	void Application::UnloadProjectDLL()
	{
		if (!m_PluginHandle) return;

		// 1. Decouple the canvas frame buffers safely before wiping memory structures
		if (m_WorkspaceLayer)
		{
			m_WorkspaceLayer->ClearViewportLayer();
		}

		// 2. Free dynamic client layers explicitly while the library allocation is valid.
		if (m_ActivePluginLayer)
		{
			delete m_ActivePluginLayer;
			m_ActivePluginLayer = nullptr;
		}

		if (m_Window)
		{
			m_Window->ClearFullscreenHotkeyOverride();
		}

		// 3. Clear the ENGINE's active-project binding (set at load step 5). Project
		//    themes/fonts loaded at mount stay REGISTERED — the registries are
		//    additive by design: Register() replaces by name on the next mount, and
		//    dropping them here would dangle ImFont* / Ref<Font> handles that other
		//    engine systems may still hold for the current frame.
		FileSystem::SetActiveProject("");

		// 4. Flush the library handle out of the operating system process memory space
		FreeLibrary(m_PluginHandle);
		m_PluginHandle = nullptr;
		CS_CORE_INFO("Project DLL safely unmounted and unloaded.");
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * SynchronizeRenderingState()
	 * 
	 * Synchronizes the Rendering Engine State with Physical Canvas Dimensions
	 * * Manually queries OS window dimensions to synchronously fire a `WindowResizeEvent`.
	 * This is primarily used as a state synchronization bridge when swapping master layouts
	 * (e.g., flipping between Launcher and Workspace hubs) to prevent viewport trailing
	 * artifacts or ImGui dockspace state discrepancies.
	 */
	void Application::SynchronizeRenderingState()
	{
		// Query the hardware window context directly
		int width, height;
		m_Window->GetSize(&width, &height);

		// Manually route a window resize command directly down the rasterizer pipeline
		WindowResizeEvent e(width, height);
		OnWindowResize(e);
	}

	/////////////////////////////////////////////////////////////////////////////////

}