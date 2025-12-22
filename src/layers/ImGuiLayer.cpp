#include "layers/ImGuiLayer.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "core/Application.h"
#include <GLFW/glfw3.h>





ImGuiLayer::ImGuiLayer() : Layer("ImGuiLayer") {}




void ImGuiLayer::OnAttach() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    Application& app = Application::Get();
    GLFWwindow* window = app.GetWindow().getHandle();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 410");
}







void ImGuiLayer::OnDetach() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}





void ImGuiLayer::Begin() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}






void ImGuiLayer::End() {
    ImGuiIO& io = ImGui::GetIO();
    Application& app = Application::Get();
    io.DisplaySize = ImVec2((float)app.GetWindow().GetWidth(), (float)app.GetWindow().GetHeight());

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup_current_context = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup_current_context);
    }
}








void ImGuiLayer::OnEvent(Event& event) {
    EventDispatcher dispatcher(event);
    // Use the macro from Core.h for binding
    dispatcher.Dispatch<MouseButtonPressedEvent>(GLCORE_BIND_EVENT_FN(ImGuiLayer::OnMouseButtonPressed));
}






bool ImGuiLayer::OnMouseButtonPressed(MouseButtonPressedEvent& e) {
    ImGuiIO& io = ImGui::GetIO();
    // If ImGui wants the mouse, we stop the event from reaching other layers
    return io.WantCaptureMouse;
}