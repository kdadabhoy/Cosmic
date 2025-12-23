#include "core/Input.h"
#include "core/Application.h"
#include <GLFW/glfw3.h>


namespace Cosmic {
    bool Input::IsKeyPressed(int keycode)
    {
        // Get the window handle from our Application singleton
        auto* window = Application::Get().GetWindow().getHandle();
        auto state = glfwGetKey(window, keycode);

        // Return true if the key is currently being held down
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }



    bool Input::IsMouseButtonPressed(int button)
    {
        auto* window = Application::Get().GetWindow().getHandle();
        auto state = glfwGetMouseButton(window, button);
        return state == GLFW_PRESS;
    }



    glm::vec2 Input::GetMousePosition()
    {
        auto* window = Application::Get().GetWindow().getHandle();
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        return { (float)xpos, (float)ypos };
    }

}