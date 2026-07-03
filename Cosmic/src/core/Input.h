#pragma once

// Input.h
// Last Modified 5/14/2026

/**
 * General Description:
 * The Input class is a static utility that provides global access to the current
 * state of input devices. It abstracts the polling mechanisms of GLFW, allowing
 * any subsystem to check the status of keys, mouse buttons, and cursor positions
 * without maintaining a direct reference to the Window handle.
 *
 * Unlike the Event system which is reactive, the Input class is proactive; it
 * is intended for use within update loops where the current state of a device
 * is required for continuous logic (e.g., character movement or camera rotation).
 *
 * Public Function Prototypes (Pre and Post Conditions):
 *
 * 1. static bool IsKeyPressed(int keycode)
 *    Pre:  The Application singleton and its Window must be initialized.
 *    Post: Returns true if the key corresponding to the keycode is held down.
 *
 * 2. static bool IsMouseButtonPressed(int button)
 *    Post: Returns true if the specified mouse button is currently depressed.
 *
 * 3. static glm::vec2 GetMousePosition()
 *    Post: Returns the (x, y) screen coordinates of the cursor as a GLM vector.
 *
 * 4. static float GetMouseX() / GetMouseY()
 *    Post: Returns the specific individual component of the mouse position.
 */

#include "core/Core.h"
#include <glm/glm.hpp>
#include <string>


namespace Cosmic
{
	class COSMIC_API Input
	{
	public:
		////////////////////////////////
		// Keyboard Queries
		///////////////////////////////

		static bool			IsKeyPressed(int keycode);


		////////////////////////////////
		// Mouse Queries
		///////////////////////////////

		static bool			IsMouseButtonPressed(int button);
		static glm::vec2	GetMousePosition();
		static float		GetMouseX();
		static float		GetMouseY();

		// Cursor position in SCREEN pixels (OS virtual-desktop coordinates) — the
		// space ImGui reports positions in while multi-viewport is enabled, and the
		// space WorkspaceLayer::GetViewportPos() lives in. Use this whenever mouse
		// math involves an ImGui rect (viewport picking, gizmos, zoom-to-cursor);
		// GetMousePosition() is window-client-relative and only matches by luck
		// when the window sits at the desktop origin (e.g. borderless maximized).
		static glm::vec2	GetMouseScreenPosition();


		////////////////////////////////
		// Gamepad / Joystick Queries (E7)
		//
		// gamepad: slot index CS_GAMEPAD_1..CS_GAMEPAD_LAST (default: first).
		// Devices WITH a gamepad mapping (Xbox/PS pads) use the standardized
		// CS_GAMEPAD_AXIS_* / CS_GAMEPAD_BUTTON_* layout. Devices WITHOUT one
		// (RC transmitters in USB-joystick mode, sim yokes) fall back to raw
		// joystick axes/buttons with the same call — axis indices are then
		// device-specific; discover them with GetGamepadAxisCount + a live
		// readout (see the template project's telemetry layer).
		//
		// No window/GL dependency beyond an initialized Application.
		///////////////////////////////

		// True when a joystick/gamepad is present in the slot.
		static bool			IsGamepadConnected(int gamepad = 0);

		// Axis value in [-1, 1] (0 on disconnected/out-of-range). Triggers on
		// mapped pads: -1 released .. +1 pressed.
		static float		GetGamepadAxis(int axis, int gamepad = 0);

		// Button held? (mapped layout when available, raw button index otherwise)
		static bool			IsGamepadButtonPressed(int button, int gamepad = 0);

		// Raw capability queries (useful for RC transmitters / unmapped sticks)
		static int			GetGamepadAxisCount(int gamepad = 0);
		static int			GetGamepadButtonCount(int gamepad = 0);

		// Human-readable device name ("" when disconnected).
		static std::string	GetGamepadName(int gamepad = 0);
	};
}