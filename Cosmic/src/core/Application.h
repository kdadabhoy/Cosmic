#pragma once
// Application.h
// Last Modified 5/24/2026

/**
 * @brief Master Controller and Resource Host for the Cosmic Engine.
 *
 * The Application class serves as the root singleton execution context of the engine.
 * It drives the structural heartbeat loop (Run), intercepting global hardware signals
 * and multiplexing engine updates into asynchronous variable and deterministic fixed timesteps.
 * 
 * 
 * =================================================================================
 * CRITICAL MEMORY OWNERSHIP ARCHITECTURE & POLICY:
 * =================================================================================
 * The Application layer holds ABSOLUTE OWNERSHIP over unmanaged raw pointers (`Layer*`)
 * injected into the engine runtime. While the `LayerStack` manages execution priorities and
 * loop structures via temporary borrow mechanics, this class assumes complete, structural
 * responsibility for managing the lifecycles, safely deferred transitions, and
 * destruction of layers.
 * 
 * Dynamic runtime DLL plugins (guest workspace environments) are completely unmounted,
 * explicitly destroyed, and isolated safely inside execution loop "Safe Zones" to prevent
 * memory corruption, dangling pointer exceptions, or OpenGL state failures.
 */

#include "core/Core.h"
#include "core/Window.h"
#include "core/LayerStack.h"
#include "events/Event.h"
#include "events/ApplicationEvent.h"
#include "layers/ImGuiLayer.h"
#include "graphics/FrameBuffer.h"
#include "core/Timestep.h"
#include "core/IFrameClock.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>

// Forward-declare HMODULE to avoid pulling Windows.h into every file that includes Application.h.
// HMODULE is defined as DECLARE_HANDLE(HMODULE) which expands to: struct HMODULE__*
#if defined(_WIN32) && !defined(_WINDOWS_)
struct HINSTANCE__;
typedef struct HINSTANCE__* HMODULE;
#endif

namespace Cosmic
{
	// Forward Declarations (Prevent Compiler Issues):
	class WorkspaceLayer;

	class COSMIC_API Application
	{
	public:
		/////////////////////////////////////////////////////////////////////////////////
		// Main Life Cycle & Execution 
		/////////////////////////////////////////////////////////////////////////////////

		// startupProjectDll: optional project to boot directly into, skipping the
		// Launcher — Runtime/Main.cpp wires this to the `--project` command-line
		// flag. Accepts "Name", "Name.dll", or an absolute path. Must be a
		// constructor argument (not a post-construction setter) because
		// Initialize() runs inside the constructor and decides Launcher-vs-project
		// there. If the DLL cannot be found the engine logs an error and falls
		// back to the Launcher.
		Application(const std::string& startupProjectDll = "");

		// Test-only overload (WO-10 clock seam): the same construction with an
		// injected frame time source. Every scheduling decision (accumulator,
		// clamp, pause, scale, dispatch) runs unchanged over it; only the value
		// RenderSingleFrame samples once per frame comes from `clock`. The
		// shipping constructor above passes GlfwFrameClock. Never pass null.
		Application(const std::string& startupProjectDll, std::unique_ptr<IFrameClock> clock);
		virtual ~Application();

		void		Run();
		void		Shutdown();
		void		OnEvent(Event& e);
		void		PushLayer(Layer* inLayer);
		void		PushOverlay(Layer* inOverlay);

		void		TransitionFromLauncherToWorkspace(const std::string& projectDllFilename);
		void		TransitionToLauncher();


		/////////////////////////////////////////////////////////////////////////////////
		// Static Accessors (Singleton)
		/////////////////////////////////////////////////////////////////////////////////

		static Application& Get();


		/////////////////////////////////////////////////////////////////////////////////
		// Subsystem Accessors 
		/////////////////////////////////////////////////////////////////////////////////

		inline Window&					GetWindow()							{ return *m_Window; }
		inline Ref<FrameBuffer>         GetFrameBuffer()					{ return m_Framebuffer; }
		inline WorkspaceLayer*			GetWorkspaceLayer()					{ return m_WorkspaceLayer; }

		// Viewport bounds in GLFW window-space pixels (top-left of rendered image content).
		// Delegates to WorkspaceLayer; returns zero vectors when no workspace is active.
		glm::vec2			GetViewportPos()  const;
		glm::vec2			GetViewportSize() const;


		/////////////////////////////////////////////////////////////////////////////////
		// Time & Step Control (Mutators) 
		/////////////////////////////////////////////////////////////////////////////////

		void			UseFixedTimeStep(bool useFixedTimeStep)		{ m_UseFixedTimestep = useFixedTimeStep; }

		// Global simulation speed (default 1). POLICY (WO-10, KI-52/KI-54): the
		// scale must be FINITE and >= 0 — 0 freezes (prefer Pause()), 0.25 is slow
		// motion, 4 is fast-forward. NaN, ±inf and NEGATIVE values are rejected with
		// a warning and the previous scale is kept: a NaN would poison the fixed
		// accumulator for the rest of the process, +inf never leaves the drain loop,
		// and a negative scale is not a rewind (no fixed update could ever fire and
		// the negative accumulator became a silent no-tick debt once the scale was
		// positive again). Reverse playback belongs to a layer's LOCAL timeline
		// (Layer::SetTimeScale accepts negative rates), DataPlayer::SetSpeed and
		// TimelineState::Speed. See contracts.md §7 / docs/guide/time-and-ticks.md.
		void			SetTimeScale(float timescale);
		float			GetTimeScale() const						{ return m_TimeScale; }

		// Fixed-step rate control (default 60 Hz, clamped to [1, 1000] — ±inf included;
		// NaN is rejected and the previous rate kept — KI-53). The new rate is picked
		// up at the start of the next frame. For very high control-loop rates prefer
		// app-side substepping inside OnFixedUpdate — raising this rate ticks EVERY
		// layer's OnFixedUpdate faster, not just yours.
		void			SetFixedTimestepHz(float hz);
		float			GetFixedTimestepHz() const					{ return m_FixedTimestepHz; }

		// Unscaled, never-paused process uptime in seconds. Accumulated in double
		// (KI-51) and returned as the correctly rounded float: no cumulative drift,
		// but the RETURNED value's resolution is a float's (0.49 ms at 2 h, 7.8 ms
		// at 24 h) — a phase/session-length source, not a timestamp.
		inline float	GetAbsoluteTime() const						{ return (float)m_AbsoluteTime; } // seconds

		// First-class pause (docs/design/responsive-rendering-and-pause.md,
		// Feature B). Orthogonal to TimeScale — Resume() never touches the
		// user's scale. While paused: OnFixedUpdate is skipped, OnUpdate runs
		// with dt = 0 (the scene keeps DRAWING, frozen), ImGui stays fully
		// interactive, GetAbsoluteTime() keeps advancing, GetLocalTime()-driven
		// animation/shaders freeze. No engine hotkey — clients bind their own.
		void			Pause()										{ m_Paused = true; }
		void			Resume()									{ m_Paused = false; }
		void			TogglePause()								{ m_Paused = !m_Paused; }
		bool			IsPaused() const							{ return m_Paused; }

		// Responsive rendering during OS window drag/resize (Feature A of the
		// same design doc). Default ON; forwards to Window's modal frame pump.
		void			SetRenderWhileDragging(bool enabled);
		bool			IsRenderWhileDragging() const;


		/////////////////////////////////////////////////////////////////////////////////
		// UI & Application State
		/////////////////////////////////////////////////////////////////////////////////

		inline ImGuiLayer*			GetImGuiLayer()			{ return m_ImGuiLayer.get(); }
		void						Close()					{ m_Running = false; }

		// When true, all update and render passes are skipped while the window is
		// minimized. Default is FALSE — the engine keeps ticking while minimized, which
		// suits simulations, telemetry tools, and servers that must run regardless of
		// window state. Set true for a game that should fully pause when minimized.
		void						SetPauseOnMinimize(bool pause)	{ m_PauseOnMinimize = pause; }
		bool						GetPauseOnMinimize() const		{ return m_PauseOnMinimize; }

		// Start-up failure (UX-V0 / KI-83). When a subsystem the engine cannot run
		// without fails inside the constructor — today: Renderer2D's batch shader
		// (a driver that rejects it) — Initialize() stops there, logs one CRITICAL
		// line naming the cause, and Run() returns at once. The host (Runtime/
		// Main.cpp) then tells the user, deletes the app (a normal, symmetric
		// Shutdown) and exits with GetExitCode() — never an access violation.
		static constexpr int		StartupFailureExitCode = 2;
		bool						StartedSuccessfully() const		{ return m_StartupError.empty(); }
		const std::string&			GetStartupError() const			{ return m_StartupError; }
		int							GetExitCode() const				{ return m_StartupError.empty() ? 0 : StartupFailureExitCode; }


	private:
		/////////////////////////////////////////////////////////////////////////////////
		// Internal Event Handlers & Initialization
		/////////////////////////////////////////////////////////////////////////////////

		void		Initialize();
		bool		OnWindowClose(WindowCloseEvent& e);
		bool		OnWindowResize(WindowResizeEvent& e);

		/**
		 * @brief One full frame: timing, fixed-step pass, variable pass, ImGui, swap.
		 *
		 * The per-frame body of Run(), factored out so it can ALSO be pumped from
		 * the Win32 modal move/size loop (Window's WM_TIMER frame pump) and fired
		 * once immediately after a fullscreen toggle (paint-through-transition).
		 * Excludes PollEvents() and the Safe Zone — no DLL load/unload or layer
		 * push/pop can run mid-drag. Returns false on the minimized early-out.
		 */
		bool		RenderSingleFrame();


	private:
		/////////////////////////////////////////////////////////////////////////////////
		// Core Engine Subsystems
		/////////////////////////////////////////////////////////////////////////////////

		Scope<Window>					m_Window;
		Scope<ImGuiLayer>				m_ImGuiLayer;
		LayerStack						m_LayerStack;
		Ref<FrameBuffer>                m_Framebuffer;


		/////////////////////////////////////////////////////////////////////////////////
		// Application State Flags
		/////////////////////////////////////////////////////////////////////////////////

		bool							m_Running = true;
		bool							m_UseFixedTimestep = true;
		bool							m_Minimized = false;
		bool							m_PauseOnMinimize = false;


		/////////////////////////////////////////////////////////////////////////////////
		// Singleton Pointer
		/////////////////////////////////////////////////////////////////////////////////

		static Application* s_Instance;


		/////////////////////////////////////////////////////////////////////////////////
		// Dynamic Workspace / Project State Strings
		/////////////////////////////////////////////////////////////////////////////////

		std::string                     m_PendingProjectDLL = "";


		/////////////////////////////////////////////////////////////////////////////////
		// Default Configuration Constants
		/////////////////////////////////////////////////////////////////////////////////

		const static int				DEFAULT_WIDTH		 = 1280;
		const static int				DEFAULT_HEIGHT		 = 720;
		const std::string				DEFAULT_WINDOW_TITLE = "Cosmic Engine";

		float			m_TimeScale			= 1.0f;
		double			m_AbsoluteTime		= 0.0;		// double: a float accumulator ran 3 % slow after 24 h (KI-51)
		float			m_FixedTimestepHz	= 60.0f;

		// The frame time source (WO-10 seam): GlfwFrameClock in shipping, a
		// scripted clock under test. Sampled once per frame; see IFrameClock.h.
		std::unique_ptr<IFrameClock> m_Clock;

		// Frame-loop clock state. Members (not Run() locals) so the main loop
		// and the modal-loop frame pump share one coherent clock/accumulator.
		// The clock SAMPLE is kept in double (KI-51): only the frame DELTA is
		// narrowed to the float Timestep, so a sub-frame delta at 24 h of uptime
		// is still exact instead of quantised to the float ulp of 86,400 (7.8 ms).
		double			m_LastFrameTime		= 0.0;
		float			m_Accumulator		= 0.0f;
		bool			m_InFrameTick		= false;	// RenderSingleFrame re-entrancy guard
		bool			m_Paused			= false;	// first-class pause (orthogonal to TimeScale)

		// Set via the constructor argument; consumed by Initialize() to skip the
		// Launcher and route straight into the pending-project Safe Zone path.
		std::string		m_StartupProjectDLL = "";


	private:
		/////////////////////////////////////////////////////////////////////////////////
		// Dynamic Project DLL Assembly Linking & Unlinking Subsystems
		/////////////////////////////////////////////////////////////////////////////////

		/**
		 * @brief Maps a client compiled module into the host memory space and hooks engine endpoints.
		 */
		void LoadProjectDLL(const std::string& filepath);

		/**
		 * @brief Deletes dynamic client layers and safely clears dynamic window linkages.
		 */
		void UnloadProjectDLL();

		/**
		 * @brief Dispatches synchronous WindowResize signals to re-dock and align UI layout dimensions.
		 */
		void SynchronizeRenderingState();

		/**
		 * @brief THE SAFE ZONE body — applies deferred layer/DLL transitions.
		 * Called from Run() only while no LayerStack iteration is active (including
		 * while minimized, so queued transitions never stall).
		 */
		void ProcessDeferredTransitions();


	private:
		/////////////////////////////////////////////////////////////////////////////////
		// Dynamic Guest Module Allocation Handlers
		/////////////////////////////////////////////////////////////////////////////////

		WorkspaceLayer*		m_WorkspaceLayer = nullptr;
		HMODULE				m_PluginHandle = nullptr;  // typed as HMODULE — communicates "loaded DLL handle" clearly
		Layer*				m_ActivePluginLayer = nullptr;
		bool				m_PendingReturnToLauncher = false;

		// UX-V0 / KI-83 — see StartedSuccessfully(). Last member on purpose: the
		// offsets of every older member are unchanged.
		std::string			m_StartupError;

	};

	/////////////////////////////////////////////////////////////////////////////////
	// Client Application Entry-Point Hook
	/////////////////////////////////////////////////////////////////////////////////
	Application* CreateApplication();

}