#pragma once

// Log.h
// Last Modified 5/14/2026

/**
 * General Description:
 * 
 * The Log class is the primary diagnostic subsystem of the Cosmic Engine. It provides
 * a centralized, thread-safe logging interface built upon the spdlog library.
 * By separating concerns between "Core" (engine-level) and "Client" (game-level)
 * loggers, it allows developers to easily distinguish between internal engine
 * status updates and application-specific logic.
 * 
 * This system utilizes preprocessor macros to simplify logging calls and
 * provides color-coded console output to highlight different severity levels
 * from Trace to Critical.
 * 
 * 
 * Public Function Prototypes (Pre and Post Conditions):
 * 
 * 1. static void Init()
 * Pre:  None.
 * Post: The spdlog sinks are initialized, patterns are set, and both Core
 * and Client loggers are instantiated and ready for use.
 * 
 * 2. static Ref<spdlog::logger>& GetCoreLogger()
 * Pre:  Init() must have been called.
 * Post: Returns a reference to the internal engine logger ("COSMIC").
 * 
 * 3. static Ref<spdlog::logger>& GetClientLogger()
 * Pre:  Init() must have been called.
 * Post: Returns a reference to the application-facing logger ("APP").
 */

#include "core/Core.h"

 // This ignores warning from external headers when compiling with high warning levels
#pragma warning(push, 0)
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#pragma warning(pop)

namespace Cosmic
{
	class Log
	{
	public:
		////////////////////////////////
		// System Lifecycle
		///////////////////////////////

		static void Init();

		////////////////////////////////
		// Logger Accessors
		///////////////////////////////

		static COSMIC_API Ref<spdlog::logger>& GetCoreLogger();
		static COSMIC_API Ref<spdlog::logger>& GetClientLogger();

	private:
		static Ref<spdlog::logger> s_CoreLogger;
		static Ref<spdlog::logger> s_ClientLogger;
	};
}

/////////////////////////////////////////////////////////////////////////////////
// Logging Macros
/////////////////////////////////////////////////////////////////////////////////

// Core log macros
#define CS_CORE_TRACE(...)    ::Cosmic::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define CS_CORE_INFO(...)     ::Cosmic::Log::GetCoreLogger()->info(__VA_ARGS__)
#define CS_CORE_WARN(...)     ::Cosmic::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define CS_CORE_ERROR(...)    ::Cosmic::Log::GetCoreLogger()->error(__VA_ARGS__)
#define CS_CORE_CRITICAL(...) ::Cosmic::Log::GetCoreLogger()->critical(__VA_ARGS__)

// Client log macros
#define CS_TRACE(...)         ::Cosmic::Log::GetClientLogger()->trace(__VA_ARGS__)
#define CS_INFO(...)          ::Cosmic::Log::GetClientLogger()->info(__VA_ARGS__)
#define CS_WARN(...)          ::Cosmic::Log::GetClientLogger()->warn(__VA_ARGS__)
#define CS_ERROR(...)         ::Cosmic::Log::GetClientLogger()->error(__VA_ARGS__)
#define CS_CRITICAL(...)      ::Cosmic::Log::GetClientLogger()->critical(__VA_ARGS__)