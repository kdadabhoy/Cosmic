#include "Log.h"
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Cosmic
{

	Ref<spdlog::logger> Log::s_CoreLogger;
	Ref<spdlog::logger> Log::s_ClientLogger;

	void Log::Init()
	{
		// Pattern: [Timestamp] [LoggerName] [Level] Message
		// %^ begins color range, %$ ends it.
		spdlog::set_pattern("%^[%T] %n: %v%$");

		s_CoreLogger = spdlog::stdout_color_mt("COSMIC");
		s_CoreLogger->set_level(spdlog::level::trace);

		s_ClientLogger = spdlog::stdout_color_mt("APP");
		s_ClientLogger->set_level(spdlog::level::trace);
	}

}