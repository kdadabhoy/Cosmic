// Windows Specific to ensure thread safety

#include "Log.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace Cosmic
{
	Ref<spdlog::logger> Log::s_CoreLogger;
	Ref<spdlog::logger> Log::s_ClientLogger;
	std::shared_ptr<spdlog::sinks::dist_sink_mt> Log::s_CoreDist;
	std::shared_ptr<spdlog::sinks::dist_sink_mt> Log::s_ClientDist;
	std::vector<spdlog::sink_ptr> Log::s_ExtraSinks;
	std::shared_mutex Log::s_LoggerMutex;

	namespace
	{
		// Console pattern colors the level token (%^…%$) — spdlog's wincolor sink
		// paints the Windows console per severity. The FILE pattern stays marker-free
		// so no escape junk lands in the log files (H7).
		constexpr const char* kConsolePattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v";
		constexpr const char* kFilePattern    = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v";
	}

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

		// Fresh console (colored level) + file (marker-free) children for this
		// destination. The "_mt" suffix keeps the sinks themselves thread-safe.
		auto coreConsole = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		coreConsole->set_pattern(kConsolePattern);
		auto coreFile = std::make_shared<spdlog::sinks::basic_file_sink_mt>(coreLogPath, true);
		coreFile->set_pattern(kFilePattern);

		auto clientConsole = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		clientConsole->set_pattern(kConsolePattern);
		auto clientFile = std::make_shared<spdlog::sinks::basic_file_sink_mt>(clientLogPath, true);
		clientFile->set_pattern(kFilePattern);

		// 3. EXCLUSIVE WRITE LOCK: swap the file/console children of the ONE owning
		// dist-sink per logger. The dist-sinks + loggers are created once and never
		// swapped, so any extra sink registered via AddSink survives a redirect.
		{
			std::unique_lock<std::shared_mutex> lock(s_LoggerMutex);

			const bool first = !s_CoreDist;
			if (first)
			{
				s_CoreDist   = std::make_shared<spdlog::sinks::dist_sink_mt>();
				s_ClientDist = std::make_shared<spdlog::sinks::dist_sink_mt>();

				s_CoreLogger = std::make_shared<spdlog::logger>("COSMIC", s_CoreDist);
				s_CoreLogger->set_level(spdlog::level::trace);
				s_CoreLogger->flush_on(spdlog::level::trace);   // crash-safety (see header)

				s_ClientLogger = std::make_shared<spdlog::logger>("APP", s_ClientDist);
				s_ClientLogger->set_level(spdlog::level::trace);
				s_ClientLogger->flush_on(spdlog::level::trace);
			}

			// Rebuild children: fresh console + file, plus every persistent extra sink.
			std::vector<spdlog::sink_ptr> coreChildren   { coreConsole,   coreFile   };
			std::vector<spdlog::sink_ptr> clientChildren { clientConsole, clientFile };
			for (const auto& s : s_ExtraSinks) { coreChildren.push_back(s); clientChildren.push_back(s); }

			s_CoreDist->set_sinks(std::move(coreChildren));
			s_ClientDist->set_sinks(std::move(clientChildren));
		}
	}

	void Log::AddSink(const spdlog::sink_ptr& sink)
	{
		if (!sink)
			return;
		std::unique_lock<std::shared_mutex> lock(s_LoggerMutex);
		if (std::find(s_ExtraSinks.begin(), s_ExtraSinks.end(), sink) != s_ExtraSinks.end())
			return;
		s_ExtraSinks.push_back(sink);
		if (s_CoreDist)   s_CoreDist->add_sink(sink);     // dist_sink add/remove is thread-safe
		if (s_ClientDist) s_ClientDist->add_sink(sink);
	}

	void Log::RemoveSink(const spdlog::sink_ptr& sink)
	{
		std::unique_lock<std::shared_mutex> lock(s_LoggerMutex);
		s_ExtraSinks.erase(std::remove(s_ExtraSinks.begin(), s_ExtraSinks.end(), sink), s_ExtraSinks.end());
		if (s_CoreDist)   s_CoreDist->remove_sink(sink);
		if (s_ClientDist) s_ClientDist->remove_sink(sink);
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