// Built-in automation commands: engine introspection, logs, screenshots and lifetime.
//
// Naming follows "<area>.<verb>" so the help output groups naturally.

#include "Core/Logging/LogManager.h"
#include "Core/Logging/LogRingBuffer.h"
#include "Platform/PlatformPaths.h"
#include "RHI/DeviceManager.h"
#include "Renderer/Renderer.h"

#include "Automation/AutomationCommandRegistry.h"
#include "Automation/ScreenshotService.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>

namespace Lime
{
	namespace
	{
		bool MatchesMinimumLevel(ELogLevel Level, ELogLevel Minimum)
		{
			return static_cast<uint8>(Level) >= static_cast<uint8>(Minimum);
		}

		bool TryParseLogLevel(std::string_view Text, ELogLevel& OutLevel)
		{
			if (Text == "trace")
			{
				OutLevel = ELogLevel::Trace;
				return true;
			}
			if (Text == "debug")
			{
				OutLevel = ELogLevel::Debug;
				return true;
			}
			if (Text == "info")
			{
				OutLevel = ELogLevel::Info;
				return true;
			}
			if (Text == "warning" || Text == "warn")
			{
				OutLevel = ELogLevel::Warning;
				return true;
			}
			if (Text == "error")
			{
				OutLevel = ELogLevel::Error;
				return true;
			}
			if (Text == "critical")
			{
				OutLevel = ELogLevel::Critical;
				return true;
			}
			return false;
		}
	} // namespace

	void RegisterCoreAutomationCommands()
	{
		FAutomationCommandRegistry& Registry = FAutomationCommandRegistry::Get();

		Registry.Register("help", "Lists every available command",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  FJson Commands = FJson::array();
			                  for (const FAutomationCommand* Command : FAutomationCommandRegistry::Get().GetAll())
			                  {
				                  FJson Entry = FJson::object();
				                  Entry["name"] = Command->Name;
				                  Entry["description"] = Command->Description;
				                  Commands.push_back(std::move(Entry));
			                  }
			                  Invocation.GetResult()["commands"] = std::move(Commands);
		                  });

		// Cheap and side effect free, so a client can use it to wait for the engine to come up.
		Registry.Register("ping", "Returns immediately; used to detect readiness",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  Invocation.GetResult()["frame"] = Invocation.GetContext().FrameCount;
			                  Invocation.GetResult()["pong"] = true;
		                  });

		Registry.Register("engine.info", "Reports the backend, adapter, window size and editor state",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  const FAutomationContext& Context = Invocation.GetContext();
			                  FJson& Result = Invocation.GetResult();

			                  Result["project"] = Context.ProjectName;
			                  Result["editorEnabled"] = Context.Editor != nullptr;

			                  if (Context.DeviceManager != nullptr)
			                  {
				                  Result["backend"] = ToString(Context.DeviceManager->GetBackend());
				                  Result["adapter"] = Context.DeviceManager->GetAdapterName();
				                  Result["backBufferWidth"] = Context.DeviceManager->GetBackBufferWidth();
				                  Result["backBufferHeight"] = Context.DeviceManager->GetBackBufferHeight();
			                  }

			                  if (Context.Renderer != nullptr)
			                  {
				                  Result["offscreenRendering"] = Context.Renderer->IsOffscreenRenderingEnabled();
				                  if (Context.Renderer->IsOffscreenRenderingEnabled())
				                  {
					                  Result["viewportWidth"] = Context.Renderer->GetViewportTarget().GetWidth();
					                  Result["viewportHeight"] = Context.Renderer->GetViewportTarget().GetHeight();
				                  }
			                  }
		                  });

		Registry.Register("engine.stats", "Reports frame timing",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  const FAutomationContext& Context = Invocation.GetContext();
			                  FJson& Result = Invocation.GetResult();

			                  Result["frame"] = Context.FrameCount;
			                  Result["deltaSeconds"] = Context.DeltaSeconds;
			                  Result["fps"] = Context.FramesPerSecond;
			                  Result["totalSeconds"] = Context.TotalSeconds;
		                  });

		Registry.Register("engine.quit", "Asks the engine to exit its main loop",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  if (Invocation.GetContext().RequestExit == nullptr)
			                  {
				                  Invocation.Fail("Exit is not available");
				                  return;
			                  }
			                  Invocation.GetContext().RequestExit();
			                  Invocation.GetResult()["exiting"] = true;
		                  });

		Registry.Register("log.tail", "Returns recent log entries. Params: count, level, category",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  FLogRingBuffer* Buffer = Invocation.GetContext().LogBuffer;
			                  if (Buffer == nullptr)
			                  {
				                  Invocation.Fail("No log buffer");
				                  return;
			                  }

			                  uint32 Count = 50;
			                  std::string LevelText;
			                  std::string Category;
			                  std::string Error;

			                  if (!Invocation.TryGetUInt("count", Count, Error) || !Invocation.TryGetString("level", LevelText, Error) ||
							      !Invocation.TryGetString("category", Category, Error))
			                  {
				                  Invocation.Fail(std::move(Error));
				                  return;
			                  }

			                  ELogLevel Minimum = ELogLevel::Trace;
			                  if (!LevelText.empty() && !TryParseLogLevel(LevelText, Minimum))
			                  {
				                  Invocation.Fail(fmt::format("Unknown log level '{}'", LevelText));
				                  return;
			                  }

			                  std::vector<FLogEntry> Entries;
			                  Buffer->CopyTo(Entries);

			                  // Filtered first, then trimmed, so "the last N warnings" behaves as expected
			                  // rather than filtering an already truncated tail.
			                  FJson Matching = FJson::array();
			                  for (const FLogEntry& Entry : Entries)
			                  {
				                  if (!MatchesMinimumLevel(Entry.Level, Minimum))
				                  {
					                  continue;
				                  }
				                  if (!Category.empty() && Entry.Category != Category)
				                  {
					                  continue;
				                  }

				                  FJson Item = FJson::object();
				                  Item["time"] = Entry.TimeSeconds;
				                  Item["level"] = ToString(Entry.Level);
				                  Item["category"] = Entry.Category;
				                  Item["message"] = Entry.Message;
				                  Matching.push_back(std::move(Item));
			                  }

			                  const SizeType Total = Matching.size();
			                  if (Count > 0 && Total > Count)
			                  {
				                  FJson Trimmed = FJson::array();
				                  for (SizeType Index = Total - Count; Index < Total; ++Index)
				                  {
					                  Trimmed.push_back(Matching[Index]);
				                  }
				                  Matching = std::move(Trimmed);
			                  }

			                  Invocation.GetResult()["matched"] = Total;
			                  Invocation.GetResult()["entries"] = std::move(Matching);
		                  });

		Registry.Register("log.clear", "Empties the in-memory log buffer",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  FLogRingBuffer* Buffer = Invocation.GetContext().LogBuffer;
			                  if (Buffer == nullptr)
			                  {
				                  Invocation.Fail("No log buffer");
				                  return;
			                  }
			                  Buffer->Clear();
			                  Invocation.GetResult()["cleared"] = true;
		                  });

		Registry.Register("screenshot.capture", "Captures a PNG. Params: path, source (backBuffer|viewport)",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  if (Invocation.GetContext().Screenshots == nullptr)
			                  {
				                  Invocation.Fail("The screenshot service is unavailable");
				                  return;
			                  }

			                  std::string Name;
			                  std::string SourceText;
			                  std::string Error;
			                  if (!Invocation.TryGetString("path", Name, Error) || !Invocation.TryGetString("source", SourceText, Error))
			                  {
				                  Invocation.Fail(std::move(Error));
				                  return;
			                  }

			                  EScreenshotSource Source = EScreenshotSource::BackBuffer;
			                  if (!SourceText.empty() && !TryParseScreenshotSource(SourceText, Source))
			                  {
				                  Invocation.Fail(fmt::format("Unknown source '{}'; expected backBuffer or viewport", SourceText));
				                  return;
			                  }

			                  const std::filesystem::path Path = FScreenshotService::ResolveOutputPath(Name);

			                  // Deferred on purpose: at this point the frame has not been rendered yet, so capturing
			                  // now would return the previous one. The completion runs after submission.
			                  Invocation.Defer(
			                      [Source, Path](FAutomationContext& Context, FJson& Result, std::string& OutError)
			                      {
				                      if (!Context.Screenshots->Capture(Source, Path, OutError))
				                      {
					                      return;
				                      }
				                      Result["path"] = FPlatformPaths::ToUtf8(Path);
				                      Result["source"] = ToString(Source);
			                      });
		                  });

		// Backs GET /screenshot. Encodes into memory so the transport can return the image itself,
		// which is what lets a client capture without sharing a file system with the engine. Not
		// useful over a JSON reply, so it is not part of the documented command set.
		Registry.RegisterHidden("screenshot.encode", "Captures a PNG into memory. Params: source (backBuffer|viewport)",
		                        [](FAutomationInvocation& Invocation)
		                        {
			                        if (Invocation.GetContext().Screenshots == nullptr)
			                        {
				                        Invocation.Fail("The screenshot service is unavailable");
				                        return;
			                        }

			                        std::string SourceText;
			                        std::string Error;
			                        if (!Invocation.TryGetString("source", SourceText, Error))
			                        {
				                        Invocation.Fail(std::move(Error));
				                        return;
			                        }

			                        EScreenshotSource Source = EScreenshotSource::BackBuffer;
			                        if (!SourceText.empty() && !TryParseScreenshotSource(SourceText, Source))
			                        {
				                        Invocation.Fail(fmt::format("Unknown source '{}'; expected backBuffer or viewport", SourceText));
				                        return;
			                        }

			                        Invocation.Defer(
			                            [Source](FAutomationContext& Context, FJson& Result, std::string& OutError)
			                            {
				                            std::vector<uint8> Png;
				                            uint32 Width = 0;
				                            uint32 Height = 0;
				                            if (!Context.Screenshots->CaptureToPng(Source, Png, Width, Height, OutError))
				                            {
					                            return;
				                            }

				                            Result["source"] = ToString(Source);
				                            Result["width"] = Width;
				                            Result["height"] = Height;
				                            // A binary node, so the HTTP layer can hand the bytes over without a
				                            // base64 round trip. Never serialized as JSON on this path.
				                            Result["bytes"] = FJson::binary(std::move(Png));
			                            });
		                        });
	}
} // namespace Lime
