// spdlog wrapper. Fans out to console, rotating file and an in-memory ring buffer.

#pragma once

#include "Core/CoreTypes.h"
#include "Core/Logging/LogRingBuffer.h"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <memory>
#include <string_view>

namespace Lime
{
	struct FLogConfig
	{
		std::filesystem::path FileName = "Logs/LimeEngine.log";
		ELogLevel ConsoleLevel = ELogLevel::Trace;
		ELogLevel FileLevel = ELogLevel::Trace;
		SizeType RingBufferCapacity = FLogRingBuffer::DefaultCapacity;
		bool bLogToFile = true;
	};

	class FLogManager
	{
	public:
		static FLogManager& Get();

		bool Initialize(const FLogConfig& Config = {});
		void Shutdown();
		void Flush();

		bool IsInitialized() const { return bInitialized; }
		FLogRingBuffer& GetRingBuffer() { return RingBuffer; }
		const FLogRingBuffer& GetRingBuffer() const { return RingBuffer; }

		void SetLevel(ELogLevel Level);
		bool ShouldLog(ELogLevel Level) const { return Level >= MinimumLevel; }

		// Category is a short static string such as "RHI"; it is prepended to the message.
		void Log(std::string_view Category, ELogLevel Level, std::string Message);

	private:
		FLogManager() = default;
		~FLogManager();

		LIME_NON_COPYABLE(FLogManager);
		LIME_NON_MOVABLE(FLogManager);

		std::shared_ptr<spdlog::logger> Logger;
		FLogRingBuffer RingBuffer;
		ELogLevel MinimumLevel = ELogLevel::Trace;
		bool bInitialized = false;
	};
} // namespace Lime

// Log categories are plain string literals so no registration step is needed.
#define LIME_LOG_CATEGORY_CORE "Core"
#define LIME_LOG_CATEGORY_PLATFORM "Platform"
#define LIME_LOG_CATEGORY_RHI "RHI"
#define LIME_LOG_CATEGORY_RENDERER "Renderer"
#define LIME_LOG_CATEGORY_EDITOR "Editor"
#define LIME_LOG_CATEGORY_APP "App"

// Formatting only happens when the level passes, so disabled logs cost a single comparison.
#define LIME_LOG(Category, Level, ...)                                                                                                     \
	do                                                                                                                                     \
	{                                                                                                                                      \
		auto& LimeLogManager = ::Lime::FLogManager::Get();                                                                                 \
		if (LimeLogManager.ShouldLog(Level))                                                                                               \
		{                                                                                                                                  \
			LimeLogManager.Log(Category, Level, ::fmt::format(__VA_ARGS__));                                                               \
		}                                                                                                                                  \
	} while (false)

#if LIME_DEBUG
#define LIME_LOG_TRACE(Category, ...) LIME_LOG(Category, ::Lime::ELogLevel::Trace, __VA_ARGS__)
#define LIME_LOG_DEBUG(Category, ...) LIME_LOG(Category, ::Lime::ELogLevel::Debug, __VA_ARGS__)
#else
#define LIME_LOG_TRACE(Category, ...) LIME_UNUSED(0)
#define LIME_LOG_DEBUG(Category, ...) LIME_UNUSED(0)
#endif

#define LIME_LOG_INFO(Category, ...) LIME_LOG(Category, ::Lime::ELogLevel::Info, __VA_ARGS__)
#define LIME_LOG_WARNING(Category, ...) LIME_LOG(Category, ::Lime::ELogLevel::Warning, __VA_ARGS__)
#define LIME_LOG_ERROR(Category, ...) LIME_LOG(Category, ::Lime::ELogLevel::Error, __VA_ARGS__)
#define LIME_LOG_CRITICAL(Category, ...) LIME_LOG(Category, ::Lime::ELogLevel::Critical, __VA_ARGS__)
