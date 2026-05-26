#include "layers/ImGuiLayer.h"
#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "core/Application.h"
#include <GLFW/glfw3.h>

namespace Cosmic
{
	/////////////////////////////////////////////////////////////////////////////////

	ImGuiLayer::ImGuiLayer() : Layer("ImGuiLayer")
	{
	}

	/////////////////////////////////////////////////////////////////////////////////

	ImGuiLayer::~ImGuiLayer()
	{
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OnAttach
	 * * THE UI BOOTSTRAP: Sets up the ImGui environment.
	 * 1. Creates the primary ImGui and ImPlot contexts.
	 * 2. Enables "Modern" UI features: Docking (for workspace layouts) and
	 * Viewports (allowing UI windows to float outside the main app window).
	 * 3. Links ImGui to the engine's GLFW window handle and OpenGL 3.3+ renderer.
	 */
	void ImGuiLayer::OnAttach()
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImPlot::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

		// Set a default polished theme right out of the box on start
		SetTheme(ImGuiTheme::CosmicEmerald);

		Application& app = Application::Get();
		GLFWwindow* window = app.GetWindow().GetHandle();

		ImGui_ImplGlfw_InitForOpenGL(window, true);
		ImGui_ImplOpenGL3_Init("#version 410");
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OnDetach
	 * * CLEANUP: Ensures all backend resources and global UI contexts are
	 * released when the layer is removed.
	 */
	void ImGuiLayer::OnDetach()
	{
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();

		ImPlot::DestroyContext();
		ImGui::DestroyContext();
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Begin
	 * * FRAME START: Prepares the GPU and Platform backends for a new batch
	 * of UI draw commands.
	 */
	void ImGuiLayer::Begin()
	{
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * End
	 * * FRAME RENDER: Collects all ImGui commands issued during the update,
	 * flushes them to the GPU, and handles OS-level window management
	 * for detached viewports.
	 */
	void ImGuiLayer::End()
	{
		ImGuiIO& io = ImGui::GetIO();
		Application& app = Application::Get();
		io.DisplaySize = ImVec2((float)app.GetWindow().GetWidth(), (float)app.GetWindow().GetHeight());

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		// Viewport logic: Allows ImGui windows to have their own OS windows
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			GLFWwindow* backup_current_context = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backup_current_context);
		}
	}

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * OnEvent
	 * * EVENT CAPTURE LOGIC: This is critical for engine stability.
	 * If the mouse is hovering over an ImGui window (io.WantCaptureMouse),
	 * the event is marked as "Handled" so that the game/simulation layer
	 * below doesn't react (e.g., clicking a UI button shouldn't make the
	 * player shoot).
	 */
	void ImGuiLayer::OnEvent(Event& event)
	{
		if (m_BlockEvents)
		{
			ImGuiIO& io = ImGui::GetIO();
			event.Handled |= event.IsInCategory(EventCategoryMouse) & io.WantCaptureMouse;
			event.Handled |= event.IsInCategory(EventCategoryKeyboard) & io.WantCaptureKeyboard;
		}

		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<MouseButtonPressedEvent>(GLCORE_BIND_EVENT_FN(ImGuiLayer::OnMouseButtonPressed));
	}

	/////////////////////////////////////////////////////////////////////////////////

	bool ImGuiLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e)
	{
		ImGuiIO& io = ImGui::GetIO();
		return io.WantCaptureMouse;
	}

	/////////////////////////////////////////////////////////////////////////////////

	void ImGuiLayer::SetTheme(ImGuiTheme theme)
	{
		ImGuiStyle& style = ImGui::GetStyle();

		// 1. Apply global structural configurations automatically
		style.WindowRounding = 5.0f;
		style.FrameRounding = 4.0f;
		style.PopupRounding = 4.0f;
		style.GrabRounding = 3.0f;
		style.TabRounding = 4.0f;
		style.WindowBorderSize = 1.0f;
		style.FrameBorderSize = 0.0f;
		style.WindowPadding = ImVec2(8.0f, 8.0f);
		style.ItemSpacing = ImVec2(6.0f, 4.0f);

		// 2. Fetch the automated lookup map
		const auto& registry = GetThemeRegistry();
		auto it = registry.find(theme);

		if (it != registry.end())
		{
			// Execute the registered function directly!
			it->second(style);
		}
		else
		{
			// Automated Fallback if an enum item wasn't added to the map yet
			ImGui::StyleColorsDark();
		}
	}
}