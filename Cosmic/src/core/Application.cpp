#include "Cosmic.h"
#include "core/Application.h"
#include "renderer/Renderer.h"
#include "renderer/RenderCommand.h"
#include "core/Timestep.h"
#include "graphics/FrameBuffer.h"
#include "core/Log.h"
#include "layers/WorkspaceLayer.h"
#include "layers/LauncherLayer.h"
#include "imgui_internal.h"


// Note: glfw3.h is kept only for glfwGetTime() in the Run() loop.
#include <GLFW/glfw3.h>

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
	 * Constructor
	 * Sets up the engine singleton, initializes the core Logger, and triggers
	 * the internal subsystem initialization sequence.
	 */
	Application::Application()
		: m_Running(true), m_Minimized(false), m_UseFixedTimestep(true), m_TimeScale(1.0f), m_ImGuiLayer(nullptr)
	{
		// This generates the logs/ directory and mounts console + file streams
		Log::Init("logs");

		CS_CORE_INFO("=================================================");
		CS_CORE_INFO("  Cosmic Engine Framework: Subsystems Initialized ");
		CS_CORE_INFO("=================================================");

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
		float lastFrameTime = 0.0f;
		float accumulator = 0.0f;
		const float fixedDeltaTime = 1.0f / 60.0f;

		while (m_Running && !m_Window->ShouldClose())
		{
			m_Window->PollEvents();

			float time = (float)glfwGetTime();
			Timestep rawTimestep = time - lastFrameTime;
			lastFrameTime = time;
			m_AbsoluteTime += rawTimestep.GetSeconds();

			// Skip execution passes while minimized (default). Disabled via SetPauseOnMinimize(false).
			if (m_Minimized && m_PauseOnMinimize)
			{
				continue;
			}


			// -----------------------------------------------------------------
			// PASS 1A: Fixed Timestep Updates (Deterministic Logic / Physics)
			// -----------------------------------------------------------------
			if (m_UseFixedTimestep)
			{
				float frameTime = rawTimestep.GetSeconds();

				// Spiral-of-death panic protection clamping
				if (frameTime > 0.25f)
				{
					frameTime = 0.25f;
				}

				accumulator += (frameTime * m_TimeScale);

				// Signed so layers receive a negative dt during rewind (TimeScale < 0)
				const float signedFixedDelta = m_TimeScale >= 0.f ? fixedDeltaTime : -fixedDeltaTime;

				while (accumulator >= fixedDeltaTime)
				{
					for (Layer* layer : m_LayerStack)
					{
						layer->OnFixedUpdate(signedFixedDelta);
					}
					accumulator -= fixedDeltaTime;
				}
			}


			// -----------------------------------------------------------------
			// PASS 1B: Variable Timestep Updates (Animations & Visual States)
			// -----------------------------------------------------------------
			Timestep scaledTimestep = rawTimestep.GetSeconds() * m_TimeScale;
			for (Layer* layer : m_LayerStack)
			{
				// 1. Core Engine updates the layer's local timeline automatically
				layer->UpdateLayerTime(scaledTimestep.GetSeconds());

				// 2. Client code runs its standard frame updates
				layer->OnUpdate(scaledTimestep.GetSeconds());
			}


			// -----------------------------------------------------------------
			// PASS 2: Main UI Rendering and Dockspace Composition
			// -----------------------------------------------------------------
			m_ImGuiLayer->Begin();
			for (Layer* layer : m_LayerStack)
			{
				layer->OnImGuiRender();
			}
			m_ImGuiLayer->End();

			m_Window->SwapBuffers();


			// =================================================================
			// THE SAFE ZONE: Guaranteed zero-iteration window on m_LayerStack
			// =================================================================

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
				LoadProjectDLL(m_PendingProjectDLL);

				// 4. Invalidate request string to wait for subsequent transition inputs
				m_PendingProjectDLL = "";
			}
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

		// 7. Flush static engine rendering layers and hardware structures
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
		for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
		{
			if (e.Handled)
			{
				break;
			}

			(*it)->OnEvent(e);
		}
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

		// 5. Boot exclusively into the Launcher state
		PushLayer(new LauncherLayer());
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
		//    dev builds — try projects/ first, then the exe dir, then the raw path
		//    (covers an absolute path being passed in). Cosmic.dll resolves from the
		//    exe dir for either location via the default loader search order.
		namespace fs = std::filesystem;
		std::string resolved = filepath;
		if (!fs::path(filepath).is_absolute())
		{
			fs::path cwd = fs::current_path();
			fs::path candidates[] = { cwd / "projects" / filepath, cwd / filepath };
			for (const fs::path& c : candidates)
			{
				if (fs::exists(c)) { resolved = c.string(); break; }
			}
		}

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

		if (m_WorkspaceLayer)
		{
			m_WorkspaceLayer->SetViewportLayer(m_ActivePluginLayer);
			std::string displayName = std::filesystem::path(filepath).stem().string();
			m_WorkspaceLayer->SetProjectName(displayName);

			CS_CORE_INFO("WorkspaceLayer project name set to: '{0}'", displayName);
			CS_CORE_INFO("Successfully loaded and mounted project DLL Layer!");
		}
		else
		{
			CS_CORE_WARN("Plugin Layer created but no active Workspace target was found to bind it to.");
		}
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

		// 3. Flush the library handle out of the operating system process memory space
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