#include "layers/ImGuiLayer.h"
#include "imgui.h"
#include "implot.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "core/Application.h"
#include "ui/Fonts.h"
#include "ui/ThemeManager.h"
#include "utils/FileSystem.h"
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

		// Dock-layout persistence must live in the WRITABLE user-data root: ImGui's
		// default writes "imgui.ini" into the working directory, which is read-only
		// for an installed app (Program Files). In portable/dev mode this resolves to
		// "./imgui.ini" — identical to the old behavior. ImGui borrows the pointer,
		// so the string must outlive the context: keep it in a static.
		static const std::string s_IniPath = FileSystem::Resolve("user://imgui.ini");
		io.IniFilename = s_IniPath.c_str();

		// Register built-in themes + any user themes, then apply the default.
		ThemeManager::Init();
		SetTheme("Sleek Pro");

		Application& app = Application::Get();
		GLFWwindow* window = app.GetWindow().GetHandle();

		ImGui_ImplGlfw_InitForOpenGL(window, true);
		ImGui_ImplOpenGL3_Init("#version 410");

		// Register custom UI fonts from engine://fonts (and project://fonts) into the
		// shared atlas. Must happen before the first frame — the OpenGL backend bakes
		// the atlas texture lazily on the first NewFrame.
		UI::Fonts::Init();
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

		// NOTE: do NOT override io.DisplaySize here. The GLFW backend already sets both
		// io.DisplaySize and io.DisplayFramebufferScale from the live window/framebuffer
		// size in Begin() (ImGui_ImplGlfw_NewFrame), before layout. Re-assigning it here
		// — after the frame was laid out and hit-tested — made ImGui render in a different
		// coordinate space than it laid out in whenever the cached window size was stale,
		// which clipped the custom title bar off-screen and offset every mouse click until
		// a resize (F11) refreshed the cache.

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

			EventDispatcher dispatcher(event);
			dispatcher.Dispatch<MouseButtonPressedEvent>([this](MouseButtonPressedEvent& e) { return OnMouseButtonPressed(e); });
		}
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
		// Legacy enum path → resolve to a theme name and apply via the registry.
		SetTheme(std::string(NameForTheme(theme)));
	}

	void ImGuiLayer::SetTheme(const std::string& name)
	{
		// Idempotent: makes the theme API robust regardless of call order
		// (a client can call SetTheme before our OnAttach has run).
		ThemeManager::Init();
		ThemeManager::Apply(name);
	}
}