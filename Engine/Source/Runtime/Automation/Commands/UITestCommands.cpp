// Automation commands for UI tests.
//
// These drive the Dear ImGui test engine, which clicks and drags real widgets by injecting input
// events. They exist so the Python suite can run UI tests alongside everything else it already does.
//
// The run is asynchronous by design. A UI test needs many frames to complete, and a handler runs on
// the main thread between frames: waiting for the test inside the handler would stop the frame loop
// the test depends on, which is a deadlock rather than a slow reply. So uitest.run only queues the
// tests and returns, and the caller polls uitest.status. FAutomationInvocation::Defer is no help
// here either, since it resumes at the end of the current frame.

#include "Automation/AutomationCommandRegistry.h"

#if LIME_WITH_EDITOR && LIME_WITH_IMGUI_TEST_ENGINE

#include "Editor/EditorLayer.h"
#include "Editor/TestEngine/EditorTestEngine.h"

namespace Lime
{
	namespace
	{
		FEditorTestEngine* ResolveTestEngine(FAutomationInvocation& Invocation)
		{
			FEditorLayer* Editor = Invocation.GetContext().Editor;
			if (Editor == nullptr)
			{
				Invocation.Fail("The editor is not active in this session, so there is no UI to test");
				return nullptr;
			}

			FEditorTestEngine* TestEngine = Editor->GetTestEngine();
			if (TestEngine == nullptr)
			{
				Invocation.Fail("The UI test engine failed to start in this session");
			}
			return TestEngine;
		}

		FJson ResultsToJson(const std::vector<FUITestResult>& Results)
		{
			FJson Tests = FJson::array();
			for (const FUITestResult& Result : Results)
			{
				FJson Entry = FJson::object();
				Entry["category"] = Result.Category;
				Entry["name"] = Result.Name;
				Entry["qualifiedName"] = Result.Category + "/" + Result.Name;
				Entry["status"] = ToString(Result.Status);
				Entry["durationSeconds"] = Result.DurationSeconds;
				Tests.push_back(std::move(Entry));
			}
			return Tests;
		}

		void WriteRunStatus(FJson& Result, const FUITestRunStatus& Status)
		{
			Result["run"] = Status.Run;
			Result["running"] = Status.bRunning;
			Result["tested"] = Status.Tested;
			Result["succeeded"] = Status.Succeeded;
			Result["remaining"] = Status.Remaining;
		}
	} // namespace

	void RegisterUITestAutomationCommands()
	{
		FAutomationCommandRegistry& Registry = FAutomationCommandRegistry::Get();

		Registry.Register("uitest.list", "Lists the registered UI tests and the outcome of their last run",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  FEditorTestEngine* TestEngine = ResolveTestEngine(Invocation);
			                  if (TestEngine == nullptr)
			                  {
				                  return;
			                  }

			                  Invocation.GetResult()["tests"] = ResultsToJson(TestEngine->GetResults());
		                  });

		Registry.Register("uitest.run",
		                  "Queues UI tests and returns immediately. Params: filter (substring of "
		                  "'Category/Name', all tests when omitted). Poll 'uitest.status' for the outcome",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  FEditorTestEngine* TestEngine = ResolveTestEngine(Invocation);
			                  if (TestEngine == nullptr)
			                  {
				                  return;
			                  }

			                  std::string Filter;
			                  std::string Error;
			                  if (!Invocation.TryGetString("filter", Filter, Error))
			                  {
				                  Invocation.Fail(std::move(Error));
				                  return;
			                  }

			                  // A filter that matches nothing is reported as a failure rather than an
			                  // empty run, so a typo cannot look like a suite that passed.
			                  if (!TestEngine->QueueTests(Filter, Error))
			                  {
				                  Invocation.Fail(std::move(Error));
				                  return;
			                  }

			                  WriteRunStatus(Invocation.GetResult(), TestEngine->GetRunStatus());
		                  });

		Registry.Register("uitest.status", "Reports whether UI tests are still running, with per test results",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  FEditorTestEngine* TestEngine = ResolveTestEngine(Invocation);
			                  if (TestEngine == nullptr)
			                  {
				                  return;
			                  }

			                  WriteRunStatus(Invocation.GetResult(), TestEngine->GetRunStatus());
			                  Invocation.GetResult()["tests"] = ResultsToJson(TestEngine->GetResults());
		                  });

		Registry.Register("uitest.abort", "Stops the current UI test run and drops anything still queued",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  FEditorTestEngine* TestEngine = ResolveTestEngine(Invocation);
			                  if (TestEngine == nullptr)
			                  {
				                  return;
			                  }

			                  TestEngine->Abort();
			                  WriteRunStatus(Invocation.GetResult(), TestEngine->GetRunStatus());
		                  });
	}
} // namespace Lime

#else

namespace Lime
{
	// Keeps the registration site free of preprocessor branches. The commands simply do not appear in
	// the 'commands' listing when UI automation is not built.
	void RegisterUITestAutomationCommands() {}
} // namespace Lime

#endif
