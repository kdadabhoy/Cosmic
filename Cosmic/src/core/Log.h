#pragma once

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
		static void Init();

		inline static Ref<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
		inline static Ref<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }

	private:
		static Ref<spdlog::logger> s_CoreLogger;
		static Ref<spdlog::logger> s_ClientLogger;
	};

}

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