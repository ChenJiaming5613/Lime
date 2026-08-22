#include "Automation/AutomationServer.h"

#include "Core/Logging/LogManager.h"
#include "Platform/PlatformPaths.h"

#include "Automation/AutomationCommandRegistry.h"
#include "Automation/ScreenshotService.h"

#include <chrono>
#include <fstream>
#include <httplib.h>

namespace Lime
{
	namespace
	{
		constexpr const char* EndpointFileName = "AutomationEndpoint.json";
		// Loopback only: the commands expose full engine state and must not leave the machine.
		constexpr const char* BindAddress = "127.0.0.1";
		constexpr const char* JsonContentType = "application/json";

		void SendJson(httplib::Response& Response, const FJson& Payload, int StatusCode = 200)
		{
			Response.status = StatusCode;
			Response.set_content(Payload.dump(), JsonContentType);
		}

		// A failed command is a 200 with ok=false rather than an HTTP error: the request itself was
		// well formed and the client distinguishes cases through the payload. Malformed requests and
		// unknown commands do use HTTP status codes, since those are transport level mistakes.
		void SendCommandReply(httplib::Response& Response, const FJson& Reply)
		{
			SendJson(Response, Reply);
		}
	} // namespace

	FAutomationServer::FAutomationServer() = default;

	FAutomationServer::~FAutomationServer()
	{
		// Shutdown touches the file system and parses JSON, either of which can throw. Escaping a
		// destructor would terminate the process during teardown, which is worse than losing a stale
		// endpoint file.
		try
		{
			Shutdown();
		}
		catch (const std::exception& Error)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_AUTOMATION, "Shutdown failed: {}", Error.what());
		}
		catch (...)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_AUTOMATION, "Shutdown failed with an unknown exception");
		}
	}

	bool FAutomationServer::Initialize(const FAutomationServerDesc& InDesc, FAutomationContext InContext)
	{
		if (bRunning)
		{
			return true;
		}

		Desc = InDesc;
		Context = std::move(InContext);

		RegisterBuiltinAutomationCommands();

		Server = std::make_unique<httplib::Server>();
		InstallRoutes();

		// Binding before listening is what makes port 0 usable: the OS assigned port is known here, so
		// it can be published before any request arrives. The two httplib entry points differ in what
		// they report, hence the lambda rather than a plain conditional.
		const int Bound = [this]
		{
			if (Desc.Port == 0)
			{
				// Returns the port the OS chose, or 0 on failure.
				return Server->bind_to_any_port(BindAddress);
			}
			// Only reports success, so the requested port is what gets used.
			const bool bBound = Server->bind_to_port(BindAddress, static_cast<int>(Desc.Port));
			return bBound ? static_cast<int>(Desc.Port) : 0;
		}();

		if (Bound <= 0)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_AUTOMATION, "Cannot bind {}:{}; automation is disabled", BindAddress, Desc.Port);
			Server.reset();
			return false;
		}

		BoundPort = static_cast<uint16>(Bound);
		bStopping.store(false);
		bRunning.store(true);

		ListenerThread = std::thread(
		    [this]
		    {
			    // Blocks until stop() is called from Shutdown.
			    Server->listen_after_bind();
		    });

		if (Desc.bWriteEndpointFile)
		{
			WriteEndpointFile();
		}

		LIME_LOG_INFO(LIME_LOG_CATEGORY_AUTOMATION, "Automation server listening on http://{}:{} with {} command(s)", BindAddress,
		              BoundPort, FAutomationCommandRegistry::Get().GetAll().size());
		return true;
	}

	void FAutomationServer::Shutdown()
	{
		if (!bRunning.exchange(false))
		{
			return;
		}

		bStopping.store(true);

		// stop() unblocks listen_after_bind and closes the listening socket. In flight handlers are
		// allowed to finish, and the ones waiting on the queue are released just below.
		if (Server != nullptr)
		{
			Server->stop();
		}

		// Anything still queued would otherwise leave a handler blocked until its timeout expires.
		{
			std::lock_guard<std::mutex> Lock(QueueMutex);
			for (std::unique_ptr<FPendingCommand>& Command : Queue)
			{
				Command->Reply.set_value(MakeError("The engine is shutting down"));
			}
			Queue.clear();
		}

		for (FDeferredCommand& Command : Deferred)
		{
			Command.Reply.set_value(MakeError("The engine is shutting down"));
		}
		Deferred.clear();

		if (ListenerThread.joinable())
		{
			ListenerThread.join();
		}
		Server.reset();

		if (Desc.bWriteEndpointFile)
		{
			RemoveEndpointFile();
		}

		LIME_LOG_INFO(LIME_LOG_CATEGORY_AUTOMATION, "Automation server stopped");
	}

	void FAutomationServer::InstallRoutes()
	{
		// Identity document. A client can use this both to discover what it is talking to and, since
		// it needs no command dispatch, to detect readiness before the first frame.
		Server->Get("/",
		            [this](const httplib::Request&, httplib::Response& Response)
		            {
			            FJson Payload = FJson::object();
			            Payload["server"] = "LimeEngine";
			            Payload["protocol"] = 1;
			            Payload["project"] = Context.ProjectName;
			            Payload["port"] = BoundPort;
			            Payload["frame"] = Context.FrameCount;
			            Payload["commandCount"] = FAutomationCommandRegistry::Get().GetAll().size();
			            SendJson(Response, Payload);
		            });

		Server->Get("/commands",
		            [](const httplib::Request&, httplib::Response& Response)
		            {
			            FJson Commands = FJson::array();
			            for (const FAutomationCommand* Command : FAutomationCommandRegistry::Get().GetAll())
			            {
				            FJson Entry = FJson::object();
				            Entry["name"] = Command->Name;
				            Entry["description"] = Command->Description;
				            Commands.push_back(std::move(Entry));
			            }

			            FJson Payload = FJson::object();
			            Payload["commands"] = std::move(Commands);
			            SendJson(Response, Payload);
		            });

		// Command name in the body.
		Server->Post("/command",
		             [this](const httplib::Request& Request, httplib::Response& Response)
		             {
			             std::string Name;
			             FJson Params;
			             std::string Error;
			             if (!ParseCommandBody(Request.body, Name, Params, Error))
			             {
				             SendJson(Response, MakeError(std::move(Error)), 400);
				             return;
			             }
			             SendCommandReply(Response, Execute(std::move(Name), std::move(Params)));
		             });

		// Command name in the path, body is the params object. Lets a command be invoked with plain
		// curl and makes the access log readable.
		Server->Post(R"(/command/([A-Za-z0-9_.\-]+))",
		             [this](const httplib::Request& Request, httplib::Response& Response)
		             {
			             FJson Params = FJson::object();
			             if (!Request.body.empty())
			             {
				             try
				             {
					             Params = FJson::parse(Request.body);
				             }
				             catch (const FJson::parse_error& Error)
				             {
					             SendJson(Response, MakeError(std::string("Malformed JSON: ") + Error.what()), 400);
					             return;
				             }
				             if (!Params.is_object())
				             {
					             SendJson(Response, MakeError("The body must be a params object"), 400);
					             return;
				             }
			             }
			             SendCommandReply(Response, Execute(Request.matches[1].str(), std::move(Params)));
		             });

		// Several commands in one round trip. Each is dispatched in order and gets its own reply, so a
		// failure in the middle does not discard the results before it.
		Server->Post("/batch",
		             [this](const httplib::Request& Request, httplib::Response& Response)
		             {
			             FJson Body;
			             try
			             {
				             Body = FJson::parse(Request.body);
			             }
			             catch (const FJson::parse_error& Error)
			             {
				             SendJson(Response, MakeError(std::string("Malformed JSON: ") + Error.what()), 400);
				             return;
			             }

			             if (!Body.is_array())
			             {
				             SendJson(Response, MakeError("A batch must be a JSON array"), 400);
				             return;
			             }

			             FJson Replies = FJson::array();
			             for (const FJson& Entry : Body)
			             {
				             const auto NameIterator = Entry.is_object() ? Entry.find("command") : Entry.end();
				             if (NameIterator == Entry.end() || !NameIterator->is_string())
				             {
					             Replies.push_back(MakeError("Each entry needs a 'command' string"));
					             continue;
				             }

				             FJson Params = FJson::object();
				             if (const auto ParamsIterator = Entry.find("params"); ParamsIterator != Entry.end())
				             {
					             if (!ParamsIterator->is_object())
					             {
						             Replies.push_back(MakeError("'params' must be an object"));
						             continue;
					             }
					             Params = *ParamsIterator;
				             }

				             Replies.push_back(Execute(NameIterator->get<std::string>(), std::move(Params)));
			             }

			             FJson Payload = FJson::object();
			             Payload["ok"] = true;
			             Payload["replies"] = std::move(Replies);
			             SendJson(Response, Payload);
		             });

		// Returns the image itself rather than a path, so a client does not need to share a file
		// system with the engine. Encoding happens in memory; nothing is written to disk.
		Server->Get("/screenshot",
		            [this](const httplib::Request& Request, httplib::Response& Response)
		            {
			            EScreenshotSource Source = EScreenshotSource::BackBuffer;
			            if (Request.has_param("source"))
			            {
				            const std::string SourceText = Request.get_param_value("source");
				            if (!TryParseScreenshotSource(SourceText, Source))
				            {
					            SendJson(Response, MakeError("Unknown source '" + SourceText + "'; expected backBuffer or viewport"), 400);
					            return;
				            }
			            }

			            FJson Params = FJson::object();
			            Params["source"] = ToString(Source);
			            // Routed through the queue like any other command, so the capture still happens at
			            // the one point in the frame where the back buffer holds what was just drawn.
			            const FJson Reply = Execute("screenshot.encode", std::move(Params));

			            if (!Reply.value("ok", false))
			            {
				            SendJson(Response, Reply, 500);
				            return;
			            }

			            const FJson& Result = Reply["result"];
			            const auto BytesIterator = Result.find("bytes");
			            if (BytesIterator == Result.end() || !BytesIterator->is_binary())
			            {
				            SendJson(Response, MakeError("The capture produced no image data"), 500);
				            return;
			            }

			            const FJson::binary_t& Bytes = BytesIterator->get_binary();
			            Response.set_content(reinterpret_cast<const char*>(Bytes.data()), Bytes.size(), "image/png");
		            });

		Server->set_exception_handler(
		    [](const httplib::Request&, httplib::Response& Response, const std::exception_ptr& Exception)
		    {
			    std::string Message = "Unhandled exception";
			    try
			    {
				    std::rethrow_exception(Exception);
			    }
			    catch (const std::exception& Error)
			    {
				    Message = Error.what();
			    }
			    catch (...)
			    {
			    }
			    SendJson(Response, MakeError(std::move(Message)), 500);
		    });

		// A wrong URL is a client bug worth reporting clearly rather than returning httplib's default
		// HTML error page.
		Server->set_error_handler(
		    [](const httplib::Request& Request, httplib::Response& Response)
		    {
			    if (Response.status == 404)
			    {
				    SendJson(Response, MakeError("No route for " + Request.method + " " + Request.path), 404);
			    }
		    });
	}

	bool FAutomationServer::ParseCommandBody(const std::string& Body, std::string& OutName, FJson& OutParams, std::string& OutError)
	{
		FJson Request;
		try
		{
			Request = FJson::parse(Body);
		}
		catch (const FJson::parse_error& Error)
		{
			OutError = std::string("Malformed JSON: ") + Error.what();
			return false;
		}

		if (!Request.is_object())
		{
			OutError = "A request must be a JSON object";
			return false;
		}

		const auto NameIterator = Request.find("command");
		if (NameIterator == Request.end() || !NameIterator->is_string())
		{
			OutError = "A request needs a 'command' string";
			return false;
		}

		OutParams = FJson::object();
		if (const auto ParamsIterator = Request.find("params"); ParamsIterator != Request.end())
		{
			if (!ParamsIterator->is_object())
			{
				OutError = "'params' must be an object";
				return false;
			}
			OutParams = *ParamsIterator;
		}

		OutName = NameIterator->get<std::string>();
		return true;
	}

	FJson FAutomationServer::Execute(std::string Name, FJson Params)
	{
		if (FAutomationCommandRegistry::Get().Find(Name) == nullptr)
		{
			return MakeError("Unknown command '" + Name + "'; GET /commands for the list");
		}

		auto Command = std::make_unique<FPendingCommand>();
		Command->Name = std::move(Name);
		Command->Params = std::move(Params);
		std::future<FJson> Future = Command->Reply.get_future();

		{
			std::lock_guard<std::mutex> Lock(QueueMutex);
			if (!bRunning.load())
			{
				return MakeError("The engine is shutting down");
			}
			Queue.push_back(std::move(Command));
		}

		// The main thread runs the command during its next Tick. A dead main loop would hang the
		// client forever, so the wait is bounded.
		if (Future.wait_for(std::chrono::seconds(Desc.CommandTimeoutSeconds)) != std::future_status::ready)
		{
			return MakeError("Timed out waiting for the main thread");
		}
		return Future.get();
	}

	FJson FAutomationServer::MakeError(std::string Message)
	{
		FJson Reply = FJson::object();
		Reply["ok"] = false;
		Reply["error"] = std::move(Message);
		return Reply;
	}

	FJson FAutomationServer::MakeSuccess(FJson Result)
	{
		FJson Reply = FJson::object();
		Reply["ok"] = true;
		Reply["result"] = std::move(Result);
		return Reply;
	}

	void FAutomationServer::WriteEndpointFile() const
	{
		// JSON rather than a bare port: a client can then also confirm which project and protocol
		// version it found, which matters when several engines run at once.
		FJson Payload = FJson::object();
		Payload["port"] = BoundPort;
		Payload["url"] = std::string("http://") + BindAddress + ":" + std::to_string(BoundPort);
		Payload["protocol"] = 1;
		Payload["project"] = Context.ProjectName;
		Payload["pid"] = FPlatformPaths::GetProcessId();

		const std::string Serialized = Payload.dump(2);

		// Two files on purpose. The per process one under Saved/Automation is what makes concurrent
		// instances discoverable: sharing a single name would have them overwrite each other, and a
		// client would then connect to whichever wrote last. The well known name beside the executable
		// stays for the common single instance case, where guessing a PID would be awkward.
		std::error_code ErrorCode;
		const std::filesystem::path Directory = FPlatformPaths::GetSavedDirectory() / "Automation";
		std::filesystem::create_directories(Directory, ErrorCode);

		const std::filesystem::path Paths[] = { Directory / (std::to_string(FPlatformPaths::GetProcessId()) + ".json"),
		                                        FPlatformPaths::GetExecutableDirectory() / EndpointFileName };

		for (const std::filesystem::path& Path : Paths)
		{
			std::ofstream File(Path, std::ios::trunc);
			if (!File)
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_AUTOMATION, "Could not write '{}'", Path.string());
				continue;
			}
			File << Serialized << '\n';
		}
	}

	void FAutomationServer::RemoveEndpointFile() const
	{
		std::error_code ErrorCode;
		std::filesystem::remove(
		    FPlatformPaths::GetSavedDirectory() / "Automation" / (std::to_string(FPlatformPaths::GetProcessId()) + ".json"), ErrorCode);

		// Only removed when it still describes this process: another instance may have replaced it, and
		// deleting that would leave the surviving engine undiscoverable.
		const std::filesystem::path Shared = FPlatformPaths::GetExecutableDirectory() / EndpointFileName;
		std::ifstream File(Shared);
		if (!File)
		{
			return;
		}

		// Non-throwing parse: this runs during shutdown, including from the destructor, where an
		// exception would be worse than leaving the file behind.
		const FJson Existing = FJson::parse(File, nullptr, false);
		File.close();

		// A corrupt file cannot be attributed to another instance, so removing it is better than
		// leaving one no client can use.
		if (Existing.is_discarded() || Existing.value("pid", 0u) == FPlatformPaths::GetProcessId())
		{
			std::filesystem::remove(Shared, ErrorCode);
		}
	}

	void FAutomationServer::UpdateContext(float DeltaSeconds, float FramesPerSecond, double TotalSeconds, uint64 FrameCount)
	{
		Context.DeltaSeconds = DeltaSeconds;
		Context.FramesPerSecond = FramesPerSecond;
		Context.TotalSeconds = TotalSeconds;
		Context.FrameCount = FrameCount;
	}

	void FAutomationServer::Tick()
	{
		if (!bRunning.load())
		{
			return;
		}

		// The queue is swapped out so a handler is free to enqueue further work without deadlocking.
		std::vector<std::unique_ptr<FPendingCommand>> Batch;
		{
			std::lock_guard<std::mutex> Lock(QueueMutex);
			Batch.swap(Queue);
		}

		for (std::unique_ptr<FPendingCommand>& Command : Batch)
		{
			const FAutomationCommand* Entry = FAutomationCommandRegistry::Get().Find(Command->Name);
			if (Entry == nullptr)
			{
				// Possible when a command was replaced between dispatch and execution.
				Command->Reply.set_value(MakeError("Unknown command '" + Command->Name + "'"));
				continue;
			}

			FAutomationInvocation Invocation(Command->Params, Context);
			try
			{
				Entry->Handler(Invocation);
			}
			catch (const std::exception& Error)
			{
				// A handler bug must not take the engine down mid frame.
				Command->Reply.set_value(MakeError(std::string("Command threw: ") + Error.what()));
				continue;
			}

			if (Invocation.HasFailed())
			{
				Command->Reply.set_value(MakeError(Invocation.GetError()));
				continue;
			}

			if (Invocation.IsDeferred())
			{
				Deferred.push_back(FDeferredCommand{ Invocation.TakeCompletion(), Invocation.GetResult(), std::move(Command->Reply) });
				continue;
			}

			Command->Reply.set_value(MakeSuccess(Invocation.GetResult()));
		}
	}

	void FAutomationServer::FlushDeferred()
	{
		if (Deferred.empty())
		{
			return;
		}

		std::vector<FDeferredCommand> Batch;
		Batch.swap(Deferred);

		for (FDeferredCommand& Command : Batch)
		{
			std::string Error;
			try
			{
				Command.Completion(Context, Command.Result, Error);
			}
			catch (const std::exception& Exception)
			{
				Error = std::string("Completion threw: ") + Exception.what();
			}

			if (Error.empty())
			{
				Command.Reply.set_value(MakeSuccess(std::move(Command.Result)));
			}
			else
			{
				Command.Reply.set_value(MakeError(std::move(Error)));
			}
		}
	}
} // namespace Lime
