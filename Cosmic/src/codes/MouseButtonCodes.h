#pragma once

// MouseButtonCodes.h
// Last Modified 5/14/2026

/**
 * General Description:
 * MouseButtonCodes.h defines the engine's internal representation of mouse
 * hardware buttons. These definitions decouple the engine logic from
 * platform-specific libraries like GLFW.
 *
 * Usage:
 * Use these constants when checking for mouse state via Cosmic::Input or
 * when identifying buttons in Cosmic::MouseButtonEvent subclasses.
 */

namespace Cosmic
{

// --- Standard Mouse Buttons ---
#define CS_MOUSE_BUTTON_1         0
#define CS_MOUSE_BUTTON_2         1
#define CS_MOUSE_BUTTON_3         2
#define CS_MOUSE_BUTTON_4         3
#define CS_MOUSE_BUTTON_5         4
#define CS_MOUSE_BUTTON_6         5
#define CS_MOUSE_BUTTON_7         6
#define CS_MOUSE_BUTTON_8         7

// --- Logical Aliases ---
#define CS_MOUSE_BUTTON_LAST      CS_MOUSE_BUTTON_8
#define CS_MOUSE_BUTTON_LEFT      CS_MOUSE_BUTTON_1
#define CS_MOUSE_BUTTON_RIGHT     CS_MOUSE_BUTTON_2
#define CS_MOUSE_BUTTON_MIDDLE    CS_MOUSE_BUTTON_3

}