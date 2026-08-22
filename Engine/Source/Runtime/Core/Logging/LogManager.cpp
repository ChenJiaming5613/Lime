#include "Core/Logging/LogManager.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <chrono>
#include <filesystem>

namespace Lime
{
	namespace
	{
		spdlog::level::level_enum ToSpdlogLevel(ELogLevel Level)
		{
			switch (Level)
			{
				case ELogLevel::Trace:
					return spdlog::level::trace;
				case ELogLevel::Debug:
					return spdlog::level::debug;
				case ELogLevel::Info:
					return spdlog::level::info;
				case ELogLevel::Warning:
					return spdlog::level::warn;
				case ELogLevel::Error:
					return spdlog::level::err;
				case ELogLevel::Critical:
					return spdlog::level::critical;
				default:
					return spdlog::level::info;
			}
		}

		double GetElapsedSeconds()
		{
			using FClock = std::chrono::steady_clock;
			static const FClock::time_point StartTime = FClock::now();
			return std::chrono::duration<double>(FClock::now() - StartTime).count();
		}

		// spdlog::filename_t is std::wstring when built with SPDLOG_WCHAR_FILENAMES.
		spdlog::filename_t ToSinkPath(const std::filesystem::path& Path)
		{
#if defined(SPDLOG_WCHAR_FILENAMES)
			return Path.wstring();
#else
			return Path.string();
#endif
		}
	} // namespace

	FLogManager& FLogManager::Get()
	{
		static FLogManager Instance;
		return Instance;
	}

	FLogManager::~FLogManager()
	{
		Shutdown();
	}

	bool FLogManager::Initialize(const FLogConfig& Config)
	{
		if (bInitialized)
		{
			return true;
		}

		RingBuffer.Clear();
		MinimumLevel = std::min(Config.ConsoleLevel, Config.FileLevel);

		std::vector<spdlog::sink_ptr> Sinks;

		auto ConsoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		ConsoleSink->set_level(ToSpdlogLevel(Config.ConsoleLevel));
		ConsoleSink->set_pattern("[%T.%e] [%^%l%$] %v");
		Sinks.push_back(std::move(ConsoleSink));

		if (Config.bLogToFile)
		{
			try
			{
				if (Config.FileName.has_parent_path())
				{
					std::filesystem::create_directories(Config.FileName.parent_path());
				}

				auto FileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(ToSinkPath(Config.FileName), true);
				FileSink->set_level(ToSpdlogLevel(Config.FileLevel));
				FileSink->set_pattern("[%Y-%m-%d %T.%e] [%l] %v");
				Sinks.push_back(std::move(FileSink));
			}
			catch (const std::exception&)
			{
				// A missing log directory must never prevent the engine from starting.
			}
		}

		Logger = std::make_shared<spdlog::logger>("Lime", Sinks.begin(), Sinks.end());
		Logger->set_level(ToSpdlogLevel(MinimumLevel));
		Logger->flush_on(spdlog::level::warn);
		spdlog::set_default_logger(Logger);

		bInitialized = true;
		return true;
	}

	void FLogManager::Shutdown()
	{
		if (!bInitialized)
		{
			return;
		}

		Flush();
		spdlog::drop_all();
		Logger.reset();
		bInitialized = false;
	}

	void FLogManager::Flush()
	{
		if (Logger)
		{
			Logger->flush();
		}
	}

	void FLogManager::SetLevel(ELogLevel Level)
	{
		MinimumLevel = Level;
		if (Logger)
		{
			Logger->set_level(ToSpdlogLevel(Level));
		}
	}

	void FLogManager::Log(std::string_view Category, ELogLevel Level, std::string Message)
	{
		// The ring buffer keeps category and message separate so the console panel can colour and
		// filter them without re-parsing formatted text.
		FLogEntry Entry;
		Entry.TimeSeconds = GetElapsedSeconds();
		Entry.Level = Level;
		Entry.Category.assign(Category);
		Entry.Message = Message;
		RingBuffer.Push(std::move(Entry));

		if (Logger)
		{
			Logger->log(ToSpdlogLevel(Level), "[{}] {}", Category, Message);
		}
	}
} // namespace Lime
