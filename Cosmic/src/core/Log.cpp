// Windows Specific to ensure thread safety

#include "Log.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <filesystem> 
#include <chrono>
#include <sstream>
#include <iomanip>

namespace Cosmic
{
	Ref<spdlog::logger> Log::s_CoreLogger;
	Ref<spdlog::logger> Log::s_ClientLogger;
	std::shared_mutex Log::s_LoggerMutex;

	/////////////////////////////////////////////////////////////////////////////////

	/**
	 * Init
	 * * THE DIAGNOSTIC HEARTBEAT: Initializes the logging infrastructure.
	 * 1. Checks and generates runtime subfolders for tracking safety.
	 * 2. Assembles multi-sink pipelines combining stdout terminal buffers and file vectors.
	 * 3. Applies strict bracket patterns uniformly across both storage outputs.
	 */
	void Log::Init(const std::string& logDirectory)
	{
		if (!logDirectory.empty() && !std::filesystem::exists(logDirectory))
		{
			std::filesystem::create_directories(logDirectory);
		}

		// 1. Thread-safe Windows timestamp isolation
		auto now = std::chrono::system_clock::now();
		auto in_time_t = std::chrono::system_clock::to_time_t(now);

		struct tm buf;
		// Windows secure thread-safe variant (Takes pointer to destination storage first)
		localtime_s(&buf, &in_time_t);

		std::stringstream ss;
		ss << std::put_time(&buf, "%Y-%m-%d_%H-%M-%S");
		std::string timeStr = ss.str();

		// 2. Append the timestamp to the filename
		std::string coreLogPath = logDirectory + "/Cosmic_" + timeStr + ".log";
		std::string clientLogPath = logDirectory + "/App_" + timeStr + ".log";

		// Note: spdlog's "_mt" suffix guarantees the logging macros themselves are 100% thread safe
		std::vector<spdlog::sink_ptr> coreSinks;
		coreSinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
		coreSinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(coreLogPath, true));

		std::vector<spdlog::sink_ptr> clientSinks;
		clientSinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
		clientSinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(clientLogPath, true));

		std::string loggingPattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v";
		for (auto& sink : coreSinks)   sink->set_pattern(loggingPattern);
		for (auto& sink : clientSinks) sink->set_pattern(loggingPattern);

		// Allocate temporary pointers first so we don't hold a lock during long sink setups
		auto coreTmp = std::make_shared<spdlog::logger>("COSMIC", coreSinks.begin(), coreSinks.end());
		coreTmp->set_level(spdlog::level::trace);
		coreTmp->flush_on(spdlog::level::trace);

		auto clientTmp = std::make_shared<spdlog::logger>("APP", clientSinks.begin(), clientSinks.end());
		clientTmp->set_level(spdlog::level::trace);
		clientTmp->flush_on(spdlog::level::trace);

		// 3. EXCLUSIVE WRITE LOCK: Block all background log macros for a fraction of a millisecond
		// to safely swap the master logger addresses out from underneath them.
		{
			std::unique_lock<std::shared_mutex> lock(s_LoggerMutex);
			s_CoreLogger = coreTmp;
			s_ClientLogger = clientTmp;
		}
	}

	/////////////////////////////////////////////////////////////////////////////////

	Ref<spdlog::logger>& Log::GetCoreLogger()
	{
		// SHARED READ LOCK: Allows hundreds of concurrent simulation threads to fetch 
		// the logger pointer completely simultaneously without any performance penalty.
		std::shared_lock<std::shared_mutex> lock(s_LoggerMutex);
		return s_CoreLogger;
	}

	/////////////////////////////////////////////////////////////////////////////////

	Ref<spdlog::logger>& Log::GetClientLogger()
	{
		std::shared_lock<std::shared_mutex> lock(s_LoggerMutex);
		return s_ClientLogger;
	}

	/////////////////////////////////////////////////////////////////////////////////

	void Log::SetLogDirectory(const std::string& logDirectory)
	{
		CS_CORE_WARN("Redirecting active logging channels to destination target: '{0}'", logDirectory);

		// Safely builds the new files and updates pointers via the unique lock inside Init
		Init(logDirectory);

		CS_CORE_INFO("Log streams successfully re-routed.");
	}
	/////////////////////////////////////////////////////////////////////////////////
}