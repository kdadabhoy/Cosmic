#pragma once

// GamepadCodes.h
// Last Modified 7/1/2026

/**
 * General Description:
 * Engine-internal gamepad/joystick constants (E7). Values mirror GLFW's
 * gamepad API so they can be passed straight through, while keeping client
 * code free of GLFW includes — same pattern as KeyCodes.h.
 *
 * Axes are in [-1, 1] (triggers report -1 released .. +1 fully pressed).
 * An RC transmitter in USB-joystick mode works through the same axis API —
 * see Input::GetGamepadAxis, which falls back to raw joystick axes when the
 * device has no gamepad mapping.
 */

namespace Cosmic
{
	// --- Gamepad slots (from GLFW_JOYSTICK_*) ---
	inline constexpr int CS_GAMEPAD_1 = 0;
	inline constexpr int CS_GAMEPAD_2 = 1;
	inline constexpr int CS_GAMEPAD_3 = 2;
	inline constexpr int CS_GAMEPAD_4 = 3;
	inline constexpr int CS_GAMEPAD_LAST = 15;

	// --- Axes (from GLFW_GAMEPAD_AXIS_*) ---
	inline constexpr int CS_GAMEPAD_AXIS_LEFT_X        = 0;
	inline constexpr int CS_GAMEPAD_AXIS_LEFT_Y        = 1;
	inline constexpr int CS_GAMEPAD_AXIS_RIGHT_X       = 2;
	inline constexpr int CS_GAMEPAD_AXIS_RIGHT_Y       = 3;
	inline constexpr int CS_GAMEPAD_AXIS_LEFT_TRIGGER  = 4;
	inline constexpr int CS_GAMEPAD_AXIS_RIGHT_TRIGGER = 5;
	inline constexpr int CS_GAMEPAD_AXIS_LAST          = 5;

	// --- Buttons (from GLFW_GAMEPAD_BUTTON_*) ---
	inline constexpr int CS_GAMEPAD_BUTTON_A            = 0;   // cross
	inline constexpr int CS_GAMEPAD_BUTTON_B            = 1;   // circle
	inline constexpr int CS_GAMEPAD_BUTTON_X            = 2;   // square
	inline constexpr int CS_GAMEPAD_BUTTON_Y            = 3;   // triangle
	inline constexpr int CS_GAMEPAD_BUTTON_LEFT_BUMPER  = 4;
	inline constexpr int CS_GAMEPAD_BUTTON_RIGHT_BUMPER = 5;
	inline constexpr int CS_GAMEPAD_BUTTON_BACK         = 6;
	inline constexpr int CS_GAMEPAD_BUTTON_START        = 7;
	inline constexpr int CS_GAMEPAD_BUTTON_GUIDE        = 8;
	inline constexpr int CS_GAMEPAD_BUTTON_LEFT_THUMB   = 9;
	inline constexpr int CS_GAMEPAD_BUTTON_RIGHT_THUMB  = 10;
	inline constexpr int CS_GAMEPAD_BUTTON_DPAD_UP      = 11;
	inline constexpr int CS_GAMEPAD_BUTTON_DPAD_RIGHT   = 12;
	inline constexpr int CS_GAMEPAD_BUTTON_DPAD_DOWN    = 13;
	inline constexpr int CS_GAMEPAD_BUTTON_DPAD_LEFT    = 14;
	inline constexpr int CS_GAMEPAD_BUTTON_LAST         = 14;
}
