#pragma once

// Log.h
// Last Modified 5/26/2026 - Exporting Log stuff

#pragma once

#include "core/Core.h"
#include <mutex>
#include <shared_mutex>

// This ignores warnings from external headers when compiling with high warning levels
#pragma warning(push, 0)
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/sinks/basic_file_sink.h> // Persistent basic file sink support
#pragma warning(pop)

namespace Cosmic
{
	class COSMIC_API Log
	{
	public:
		////////////////////////////////
		// System Lifecycle
		///////////////////////////////

		// Accepts an optional log file layout target path string
		static void Init(const std::string& logDirectory = "logs");

		// Allows the client runtime to change the log location on the fly
		static void SetLogDirectory(const std::string& logDirectory);

		////////////////////////////////
		// Logger Accessors
		///////////////////////////////

		static Ref<spdlog::logger>& GetCoreLogger();
		static Ref<spdlog::logger>& GetClientLogger();

	private:
		static Ref<spdlog::logger> s_CoreLogger;
		static Ref<spdlog::logger> s_ClientLogger;

		// Guard mutex to guarantee safe thread swaps during hot-reloads
		static std::shared_mutex s_LoggerMutex;
	};
}

///////////////////////////////////////////////////////////////////////////////////
// Logging Macros
///////////////////////////////////////////////////////////////////////////////////

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