#include "Editor/TestEngine/EditorTestEngine.h"

#include "Core/Logging/LogManager.h"
#include "Editor/TestEngine/UITestRegistry.h"

#include <imgui.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_ui.h>

#include <algorithm>

namespace Lime
{
	namespace
	{
		EUITestStatus ToUITestStatus(ImGuiTestStatus Status)
		{
			switch (Status)
			{
				case ImGuiTestStatus_Success:
					return EUITestStatus::Success;
				case ImGuiTestStatus_Queued:
					return EUITestStatus::Queued;
				case ImGuiTestStatus_Running:
					return EUITestStatus::Running;
				case ImGuiTestStatus_Error:
					return EUITestStatus::Error;
				case ImGuiTestStatus_Suspended:
					return EUITestStatus::Suspended;
				case ImGuiTestStatus_Unknown:
				case ImGuiTestStatus_COUNT:
					break;
			}
			return EUITestStatus::Unknown;
		}

		// Routes the test engine's serial log into the engine log, so a failure shows up in the same
		// place as everything else rather than only inside the test engine window.
		void ForwardLog(ImGuiTestEngine*, ImGuiTestContext*, ImGuiTestVerboseLevel Level, const char* Message, void*)
		{
			if (Message == nullptr)
			{
				return;
			}

			// Error and warning are worth surfacing on their own. The per action trace goes to trace
			// level: it is what explains a failure, but it is far too chatty for the default log.
			switch (Level)
			{
				case ImGuiTestVerboseLevel_Error:
					LIME_LOG_ERROR(LIME_LOG_CATEGORY_EDITOR, "UI test: {}", Message);
					break;
				case ImGuiTestVerboseLevel_Warning:
					LIME_LOG_WARNING(LIME_LOG_CATEGORY_EDITOR, "UI test: {}", Message);
					break;
				default:
					LIME_LOG_TRACE(LIME_LOG_CATEGORY_EDITOR, "UI test: {}", Message);
					break;
			}
		}
	} // namespace

	const char* ToString(EUITestStatus Status)
	{
		switch (Status)
		{
			case EUITestStatus::Unknown:
				return "unknown";
			case EUITestStatus::Success:
				return "success";
			case EUITestStatus::Queued:
				return "queued";
			case EUITestStatus::Running:
				return "running";
			case EUITestStatus::Error:
				return "error";
			case EUITestStatus::Suspended:
				return "suspended";
		}
		return "unknown";
	}

	FEditorTestEngine::~FEditorTestEngine()
	{
		// Shutdown ordering depends on the ImGui context, which this object does not own, so the
		// destructor can only catch the case where the owner forgot entirely.
		if (Engine != nullptr)
		{
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_EDITOR, "FEditorTestEngine was destroyed without Shutdown(); tests may not have stopped");
			ImGuiTestEngine_DestroyContext(Engine);
			Engine = nullptr;
		}
	}

	bool FEditorTestEngine::Initialize(ImGuiContext* Context)
	{
		if (Engine != nullptr)
		{
			return true;
		}
		if (Context == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_EDITOR, "The UI test engine needs an ImGui context");
			return false;
		}

		Engine = ImGuiTestEngine_CreateContext();
		if (Engine == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_EDITOR, "ImGuiTestEngine_CreateContext failed");
			return false;
		}

		ImGuiTestEngineIO& TestIO = ImGuiTestEngine_GetIO(Engine);
		// Fast teleports the mouse between targets instead of animating it. Interactive runs can be
		// switched to Normal from the test engine window when watching is the point.
		TestIO.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
		// The editor's own layout file is the authority on window placement; letting the test engine
		// write into it would make a test run change what the user sees next launch.
		TestIO.ConfigSavedSettings = false;
		// A failing check should mark the test failed and move on. Breaking suits a debugger session,
		// not an automated run.
		TestIO.ConfigBreakOnError = false;
		// One failure should not hide the rest of the results.
		TestIO.ConfigStopOnError = false;
		// Leaving a test's GUI up after it finishes would keep stray windows on screen.
		TestIO.ConfigKeepGuiFunc = false;
		// Info covers the per action trace, which is what makes a failure diagnosable from the log
		// alone. Debug adds per frame detail that drowns everything else without adding much.
		TestIO.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
		TestIO.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
		TestIO.ConfigLogToFunc = &ForwardLog;
		// Drawing the simulated cursor makes a manual run readable, and costs nothing headless.
		TestIO.ConfigMouseDrawCursor = true;

		// A test that stops yielding would otherwise hang the frame loop, and with it the automation
		// server, since the coroutine runs inside ImGui::NewFrame(). Failing the test first means the
		// client sees a real result instead of a command timeout. The kill time stays under the
		// automation command timeout for the same reason.
		TestIO.ConfigWatchdogWarning = 10.0f;
		TestIO.ConfigWatchdogKillTest = 20.0f;

		ImGuiTestEngine_Start(Engine, Context);
		RegisterTests();

		return true;
	}

	void FEditorTestEngine::RegisterTests()
	{
		// Built-in tests are added here rather than through a static initializer, because LimeEditor
		// is a static library and the linker drops object files that only register things.
		RegisterBuiltinUITests();

		const std::vector<FUITestRegistration>& Registrations = FUITestRegistry::Get().GetRegistrations();
		for (const FUITestRegistration& Registration : Registrations)
		{
			// The macro records the source file and line, which the test engine window uses to show
			// the test's source. Calling the underlying function directly would lose that.
			ImGuiTest* Test = IM_REGISTER_TEST(Engine, Registration.Category.c_str(), Registration.Name.c_str());
			if (Test == nullptr)
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_EDITOR, "Could not register UI test '{}'", Registration.GetQualifiedName());
				continue;
			}

			// ImGuiTest stores these as non-owning pointers, so they have to outlive the engine. The
			// registry owns the strings and is a function local static, so they do.
			Test->TestFunc = Registration.TestFunc;
			if (Registration.GuiFunc != nullptr)
			{
				Test->GuiFunc = Registration.GuiFunc;
			}
		}

		LIME_LOG_INFO(LIME_LOG_CATEGORY_EDITOR, "UI test engine ready with {} test(s)", Registrations.size());
	}

	void FEditorTestEngine::Shutdown()
	{
		if (Engine == nullptr)
		{
			return;
		}

		// Blocks until the coroutine has been joined. A suspended test resumed after the ImGui
		// context is gone would dereference freed memory, so this must complete first.
		ImGuiTestEngine_Stop(Engine);
		bRunPending = false;
	}

	void FEditorTestEngine::DestroyAfterImGuiContext()
	{
		if (Engine == nullptr)
		{
			return;
		}

		ImGuiTestEngine_DestroyContext(Engine);
		Engine = nullptr;
	}

	void FEditorTestEngine::PreSwap()
	{
		if (Engine != nullptr)
		{
			ImGuiTestEngine_PreSwap(Engine);
		}
	}

	void FEditorTestEngine::PostSwap()
	{
		if (Engine != nullptr)
		{
			ImGuiTestEngine_PostSwap(Engine);
		}
	}

	bool FEditorTestEngine::IsRequestingMaxAppSpeed() const
	{
		if (Engine == nullptr)
		{
			return false;
		}
		return ImGuiTestEngine_GetIO(Engine).IsRequestingMaxAppSpeed;
	}

	void FEditorTestEngine::DrawUI(bool* bOpen)
	{
		if (Engine != nullptr)
		{
			ImGuiTestEngine_ShowTestEngineWindows(Engine, bOpen);
		}
	}

	bool FEditorTestEngine::QueueTests(const std::string& Filter, std::string& OutError)
	{
		if (Engine == nullptr)
		{
			OutError = "The UI test engine is not running";
			return false;
		}

		ImVector<ImGuiTest*> Tests;
		ImGuiTestEngine_GetTestList(Engine, &Tests);

		// Matching is done here rather than through ImGuiTestEngine_QueueTests so the caller learns
		// how many tests matched. Its filter syntax cannot report that, and a filter that matches
		// nothing would otherwise look like a run that passed with no failures.
		int32 Matched = 0;
		for (ImGuiTest* Test : Tests)
		{
			if (Test == nullptr || Test->Category == nullptr || Test->Name == nullptr)
			{
				continue;
			}

			if (!Filter.empty())
			{
				const std::string Qualified = std::string(Test->Category) + "/" + Test->Name;
				if (Qualified.find(Filter) == std::string::npos)
				{
					continue;
				}
			}

			ImGuiTestEngine_QueueTest(Engine, Test, ImGuiTestRunFlags_None);
			++Matched;
		}

		if (Matched == 0)
		{
			OutError = Filter.empty() ? "No UI tests are registered"
			                          : "No UI test matches '" + Filter + "'; call 'uitest.list' for the registered ones";
			return false;
		}

		++RunCounter;
		bRunPending = true;
		LIME_LOG_INFO(LIME_LOG_CATEGORY_EDITOR, "UI test run {} queued with {} test(s)", RunCounter, Matched);
		return true;
	}

	void FEditorTestEngine::Abort()
	{
		if (Engine == nullptr)
		{
			return;
		}

		ImGuiTestEngine_AbortCurrentTest(Engine);
		LIME_LOG_INFO(LIME_LOG_CATEGORY_EDITOR, "UI test run {} aborted", RunCounter);
	}

	FUITestRunStatus FEditorTestEngine::GetRunStatus() const
	{
		FUITestRunStatus Status;
		Status.Run = RunCounter;

		if (Engine == nullptr)
		{
			return Status;
		}

		// Counted here rather than through ImGuiTestEngine_GetResultSummary, which asserts that no
		// test is currently running. Polling for progress is exactly the case where one is, and the
		// assertion would abort the process: on Windows that opens a modal dialog which blocks the
		// main thread, stopping the frame loop the tests need to advance.
		ImVector<ImGuiTest*> Tests;
		ImGuiTestEngine_GetTestList(Engine, &Tests);

		for (const ImGuiTest* Test : Tests)
		{
			if (Test == nullptr)
			{
				continue;
			}

			switch (Test->Output.Status)
			{
				case ImGuiTestStatus_Success:
					++Status.Tested;
					++Status.Succeeded;
					break;
				case ImGuiTestStatus_Error:
					++Status.Tested;
					break;
				case ImGuiTestStatus_Queued:
				case ImGuiTestStatus_Running:
				case ImGuiTestStatus_Suspended:
					++Status.Remaining;
					break;
				default:
					break;
			}
		}

		// The queue empties both before a run starts and after it ends, so bRunPending is what
		// distinguishes them. It is cleared here rather than on a callback because this is the only
		// place that observes the transition.
		const bool bQueueEmpty = ImGuiTestEngine_IsTestQueueEmpty(Engine);
		if (bRunPending && bQueueEmpty)
		{
			bRunPending = false;
		}
		Status.bRunning = bRunPending;

		return Status;
	}

	std::vector<FUITestResult> FEditorTestEngine::GetResults() const
	{
		std::vector<FUITestResult> Results;
		if (Engine == nullptr)
		{
			return Results;
		}

		ImVector<ImGuiTest*> Tests;
		ImGuiTestEngine_GetTestList(Engine, &Tests);
		Results.reserve(static_cast<SizeType>(Tests.Size));

		for (ImGuiTest* Test : Tests)
		{
			if (Test == nullptr || Test->Category == nullptr || Test->Name == nullptr)
			{
				continue;
			}

			FUITestResult Result;
			Result.Category = Test->Category;
			Result.Name = Test->Name;
			Result.Status = ToUITestStatus(Test->Output.Status);
			// EndTime stays zero while a test is still running, which would otherwise report a
			// nonsensical negative duration.
			if (Test->Output.EndTime > Test->Output.StartTime)
			{
				Result.DurationSeconds = static_cast<double>(Test->Output.EndTime - Test->Output.StartTime) / 1000000.0;
			}
			Results.push_back(std::move(Result));
		}

		return Results;
	}
} // namespace Lime
