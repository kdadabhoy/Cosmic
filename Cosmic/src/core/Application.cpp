// Application.cpp
#include "Cosmic.h"
#include "core/Application.h"
#include "renderer/Renderer.h"
#include "renderer/RenderCommand.h"
#include "core/Timestep.h"
#include "graphics/FrameBuffer.h"
#include "core/Log.h"
#include "layers/WorkspaceLayer.h"

// Note: glfw3.h is kept only for glfwGetTime() in the Run() loop.
#include <GLFW/glfw3.h>

// CRITICAL FIX: Wrap Windows.h to isolate polluting win32 macro definitions
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

	Application& Application::Get()
	{
		return *s_Instance;
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Constructor
	 * Sets up the engine singleton, initializes the core Logger, and triggers
	 * the internal subsystem initialization sequence.
	 */
	Application::Application()
		: m_Running(true), m_Minimized(false), m_UseFixedTimestep(true), m_TimeScale(1.0f), m_ImGuiLayer(nullptr)
	{
		Log::Init();
		CS_CORE_INFO("Cosmic Engine Logging Initialized");

		s_Instance = this;

		if (!Initialize())
		{
			CS_CORE_CRITICAL("Cosmic: Failed to initialize application!");
		}
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

	/**
	 * Internal Initialization
	 * Orchestrates the creation of the Window, Renderer, Framebuffer, and ImGui.
	 * Returns: true if all subsystems started successfully.
	 */
	bool Application::Initialize()
	{
		// 0. Log statement
		CS_CORE_TRACE("Initializing Application Subsystems...");

		// 1. Create the window 
		m_Window = CreateScope<Window>(DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_WINDOW_TITLE);

		// 2. Bind events to the OnEvent function
		m_Window->SetEventCallback(GLCORE_BIND_EVENT_FN(Application::OnEvent));

		// 3. Initialize the Renderer (Dispatcher and API)
		Renderer::Init();

		// 4. Framebuffer Setup 
		FramebufferSpecification fbSpec;
		fbSpec.Width = DEFAULT_WIDTH;
		fbSpec.Height = DEFAULT_HEIGHT;
		m_Framebuffer = FrameBuffer::Create(fbSpec);

		// 5. Initialize ImGui
		m_ImGuiLayer = CreateScope<ImGuiLayer>();
		PushOverlay(m_ImGuiLayer.get());

		// 6. Mount the Engine Editor Shell Workspace Out-Of-The-Box
		// Keep a member variable tracking reference (e.g., m_WorkspaceLayer) if needed globally
		m_WorkspaceLayer = new Workspace::WorkspaceLayer();
		PushLayer(m_WorkspaceLayer);

		return true;
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
		dispatcher.Dispatch<WindowCloseEvent>(GLCORE_BIND_EVENT_FN(Application::OnWindowClose));
		dispatcher.Dispatch<WindowResizeEvent>(GLCORE_BIND_EVENT_FN(Application::OnWindowResize));

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

	/**
	 * THE CORE APPLICATION LOOP (The Engine's Heartbeat)
	 */
	void Application::Run()
	{
		float lastFrameTime = 0.0f;
		float accumulator = 0.0f;
		float fixedDeltaTime = 1.0f / 60.0f;

		while (m_Running && !m_Window->ShouldClose())
		{
			m_Window->PollEvents();

			float time = (float)glfwGetTime();
			Timestep rawTimestep = time - lastFrameTime;
			lastFrameTime = time;

			if (m_Minimized)
			{
				continue;
			}

			// 1A. Fixed Timestep Case (Physics/Deterministic Logic)
			if (m_UseFixedTimestep)
			{
				float frameTime = rawTimestep.GetSeconds();
				if (frameTime > 0.25f) frameTime = 0.25f;

				accumulator += (frameTime * m_TimeScale);
				while (accumulator >= fixedDeltaTime)
				{
					// Notify engine layers (WorkspaceLayer picks this up and forwards it to the viewport)
					for (Layer* layer : m_LayerStack)
						layer->OnFixedUpdate(Timestep(fixedDeltaTime));

					accumulator -= fixedDeltaTime;
				}
			}

			// 1B. Variable Timestep (Animations/Smooth Visuals)
			Timestep scaledTimestep = rawTimestep.GetSeconds() * m_TimeScale;
			for (Layer* layer : m_LayerStack)
			{
				layer->OnUpdate(scaledTimestep);
			}

			// 2. UI Rendering Pass (The layout layers draw themselves automatically)
			m_ImGuiLayer->Begin();

			for (Layer* layer : m_LayerStack)
			{
				layer->OnImGuiRender();
			}

			// --- Unified Dynamic Project Launcher Manager Overlay ---
			ImGui::Begin("Cosmic Project Launcher");
			ImGui::Text("Enter filename or absolute system download path location:");
			ImGui::InputText("DLL Target Path", m_DLLPathBuffer, sizeof(m_DLLPathBuffer));

			ImGui::Separator();

			if (ImGui::Button("Load / Hot-Swap Module", ImVec2(200, 0)))
			{
				LoadProjectDLL(m_DLLPathBuffer);
			}

			ImGui::SameLine();

			if (ImGui::Button("Unload Module", ImVec2(150, 0)))
			{
				if (m_PluginHandle)
				{
					UnloadProjectDLL();
				}
			}

			if (m_ActivePluginLayer)
			{
				ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Status: Project Layer active and running inside Viewport.");
			}
			else
			{
				ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Status: No guest project layer loaded.");
			}
			ImGui::End();
			// --------------------------------------------------------

			m_ImGuiLayer->End();

			m_Window->SwapBuffers();
		}
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

	/**
	 * Engine Shutdown
	 *
	 * This method orchestrates a "soft-landing" for the engine. By explicitly clearing
	 * the LayerStack, we ensure that every layer has a chance to run its OnDetach()
	 * logic (cleaning up textures, shaders, etc.) while the Renderer and Window are
	 * still valid.
	 *
	 * Because we removed the 'delete' call from the LayerStack destructor, this clear
	 * only removes raw pointers. The actual memory is then safely freed by the
	 * Scope<> (unique_ptr) members of the Application class during its final
	 * destruction phase. This prevents double-frees and dangling pointer crashes.
	 */
	void Application::Shutdown()
	{
		CS_CORE_TRACE("Shutting down Application Subsystems...");

		// 1. Notify and remove all layers/overlays from the heartbeat
		m_LayerStack.Clear();

		// 2. Additional cleanup for core static subsystems
		Renderer::Shutdown();
	}

	/////////////////////////////////////////////////////////////////////////////////




	void Application::LoadProjectDLL(const std::string& filepath)
	{
		if (m_PluginHandle) UnloadProjectDLL();

		// 1. Load the DLL into Cosmic's memory space
		HMODULE handle = LoadLibraryA(filepath.c_str());
		if (!handle)
		{
			CS_CORE_ERROR("Failed to load plugin: {0}", filepath);
			return;
		}

		// 2. Find the function pointers inside the DLL
		auto initContexts = (void(*)(HostContext))GetProcAddress(handle, "InitializePluginContexts");

		// Change: The plugin export signature now drops custom abstractions 
		// and simply creates a standard engine Layer pointer.
		auto createPluginLayer = (Cosmic::Layer * (*)())GetProcAddress(handle, "CreatePluginLayer");

		if (!initContexts || !createPluginLayer)
		{
			CS_CORE_ERROR("Plugin is missing engine export signatures!");
			FreeLibrary(handle);
			return;
		}

		m_PluginHandle = handle;

		// 3. Share the exact memory address of ImGui/ImPlot contexts across boundaries
		HostContext ctx;
		ctx.ImGuiCtx = ImGui::GetCurrentContext();
		ctx.ImPlotCtx = ImPlot::GetCurrentContext();
		initContexts(ctx);

		// 4. Instantiate the plugin layer and assign it as the center layout viewport focus
		m_ActivePluginLayer = createPluginLayer();

		if (m_WorkspaceLayer)
		{
			m_WorkspaceLayer->SetViewportLayer(m_ActivePluginLayer);
			CS_CORE_INFO("Successfully loaded and mounted project DLL Layer!");
		}
		else
		{
			CS_CORE_WARN("Plugin Layer created but no active Workspace target was found to bind it to.");
		}
	}

	void Application::UnloadProjectDLL()
	{
		if (!m_PluginHandle) return;

		// 1. Decouple the canvas display safely before purging memory allocations
		if (m_WorkspaceLayer)
		{
			m_WorkspaceLayer->ClearViewportLayer();
		}

		// 2. Delete the dynamic active client layer instances safely
		if (m_ActivePluginLayer)
		{
			delete m_ActivePluginLayer;
			m_ActivePluginLayer = nullptr;
		}

		// 3. Drop library handles out of standard environment address structures
		FreeLibrary((HMODULE)m_PluginHandle);
		m_PluginHandle = nullptr;
		CS_CORE_INFO("Project DLL safely unmounted and unloaded.");
	}

}