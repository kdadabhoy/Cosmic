#pragma once

// Log.h
// Last Modified 5/26/2026 - Exporting Log stuff

#pragma once

#include "core/Core.h"
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <functional>
#include <string>

// This ignores warnings from external headers when compiling with high warning levels
#pragma warning(push, 0)
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/sinks/basic_file_sink.h> // Persistent basic file sink support
#include <spdlog/sinks/dist_sink.h>       // one owning fan-out sink (H7: survives redirects)
#include <spdlog/sinks/base_sink.h>       // CallbackSink base (H7: route logs to a panel)
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

		// Allows the client runtime to change the log location on the fly. As of H7
		// this rotates only the FILE child of each logger's owning dist-sink — extra
		// sinks (a Console panel, see AddSink) and the colored console survive.
		static void SetLogDirectory(const std::string& logDirectory);

		////////////////////////////////
		// Extra sinks (H7)
		///////////////////////////////

		// Attach/detach an extra sink to BOTH loggers. The sink is held by each
		// logger's owning dist-sink, so it persists across SetLogDirectory redirects
		// (removed only by RemoveSink). Thread-safe. An editor registers a
		// CallbackSink here to mirror the engine log into its Console panel; the null
		// default (no extra sinks) leaves every shipped app's output unchanged.
		static void AddSink(const spdlog::sink_ptr& sink);
		static void RemoveSink(const spdlog::sink_ptr& sink);

		////////////////////////////////
		// Logger Accessors
		///////////////////////////////

		static Ref<spdlog::logger>& GetCoreLogger();
		static Ref<spdlog::logger>& GetClientLogger();

	private:
		static Ref<spdlog::logger> s_CoreLogger;
		static Ref<spdlog::logger> s_ClientLogger;

		// One owning fan-out sink per logger — its children (console + file + extras)
		// change on redirect while the sink object (and the logger) is never swapped,
		// so registered extra sinks are never dropped (H7).
		static std::shared_ptr<spdlog::sinks::dist_sink_mt> s_CoreDist;
		static std::shared_ptr<spdlog::sinks::dist_sink_mt> s_ClientDist;
		static std::vector<spdlog::sink_ptr>                s_ExtraSinks;

		// Guard mutex to guarantee safe thread swaps during hot-reloads
		static std::shared_mutex s_LoggerMutex;
	};

	/**
	 * @brief Generic spdlog sink that hands each formatted line to a std::function
	 * (severity + text). Thread-safe (base_sink<std::mutex>); the callback may fire
	 * from ANY logging thread, so it must be cheap + reentrant — an editor enqueues
	 * the line for its UI thread to drain (like FileWatcher). Header-only so a client
	 * DLL can instantiate it and register via Log::AddSink.
	 */
	class CallbackSink : public spdlog::sinks::base_sink<std::mutex>
	{
	public:
		using Callback = std::function<void(spdlog::level::level_enum, const std::string&)>;
		explicit CallbackSink(Callback cb) : m_Callback(std::move(cb)) {}

	protected:
		void sink_it_(const spdlog::details::log_msg& msg) override
		{
			if (!m_Callback)
				return;
			spdlog::memory_buf_t formatted;
			this->formatter_->format(msg, formatted);   // inherited protected member
			m_Callback(msg.level, fmt::to_string(formatted));
		}
		void flush_() override {}

	private:
		Callback m_Callback;
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