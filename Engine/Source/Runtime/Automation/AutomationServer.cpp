#include "Automation/AutomationServer.h"

#include "Core/Logging/LogManager.h"
#include "Platform/PlatformPaths.h"

#include "Automation/AutomationCommandRegistry.h"

// Winsock must come before windows.h, which some other header may already have pulled in.
#include <fstream>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

namespace Lime
{
	namespace
	{
		constexpr uintptr_t InvalidSocket = ~uintptr_t{ 0 };
		// Guards against a malformed client streaming an unbounded line into memory.
		constexpr SizeType MaxRequestBytes = 1u << 20;
		constexpr const char* PortFileName = "AutomationPort.txt";

		// Reference counted per process, so repeated Initialize/Shutdown cycles stay valid.
		class FWinsockScope
		{
		public:
			static bool Acquire()
			{
				std::lock_guard<std::mutex> Lock(GetMutex());
				if (GetRefCount() == 0)
				{
					WSADATA Data{};
					const int Result = WSAStartup(MAKEWORD(2, 2), &Data);
					if (Result != 0)
					{
						LIME_LOG_ERROR(LIME_LOG_CATEGORY_AUTOMATION, "WSAStartup failed ({})", Result);
						return false;
					}
				}
				++GetRefCount();
				return true;
			}

			static void Release()
			{
				std::lock_guard<std::mutex> Lock(GetMutex());
				if (GetRefCount() == 0)
				{
					return;
				}
				if (--GetRefCount() == 0)
				{
					WSACleanup();
				}
			}

		private:
			static std::mutex& GetMutex()
			{
				static std::mutex Mutex;
				return Mutex;
			}

			static int32& GetRefCount()
			{
				static int32 RefCount = 0;
				return RefCount;
			}
		};

		bool SendAll(uintptr_t Socket, const std::string& Payload)
		{
			SizeType Sent = 0;
			while (Sent < Payload.size())
			{
				const int Result = send(static_cast<SOCKET>(Socket), Payload.data() + Sent, static_cast<int>(Payload.size() - Sent), 0);
				if (Result <= 0)
				{
					return false;
				}
				Sent += static_cast<SizeType>(Result);
			}
			return true;
		}
	} // namespace

	FAutomationServer::~FAutomationServer()
	{
		Shutdown();
	}

	bool FAutomationServer::Initialize(const FAutomationServerDesc& InDesc, FAutomationContext InContext)
	{
		if (bRunning)
		{
			return true;
		}

		Desc = InDesc;
		Context = std::move(InContext);

		if (!FWinsockScope::Acquire())
		{
			return false;
		}

		const SOCKET Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (Listener == INVALID_SOCKET)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_AUTOMATION, "socket() failed ({})", WSAGetLastError());
			FWinsockScope::Release();
			return false;
		}

		// Without this a restart within the TIME_WAIT window fails to bind the same port.
		int ReuseFlag = 1;
		setsockopt(Listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&ReuseFlag), sizeof(ReuseFlag));

		sockaddr_in Address{};
		Address.sin_family = AF_INET;
		Address.sin_port = htons(Desc.Port);
		// Loopback only: the commands expose full engine state and must not leave the machine.
		Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

		if (bind(Listener, reinterpret_cast<const sockaddr*>(&Address), sizeof(Address)) == SOCKET_ERROR)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_AUTOMATION, "Cannot bind 127.0.0.1:{} ({}); automation is disabled", Desc.Port,
			               WSAGetLastError());
			closesocket(Listener);
			FWinsockScope::Release();
			return false;
		}

		if (listen(Listener, SOMAXCONN) == SOCKET_ERROR)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_AUTOMATION, "listen() failed ({})", WSAGetLastError());
			closesocket(Listener);
			FWinsockScope::Release();
			return false;
		}

		// Port 0 asks the OS to pick one, so the real value has to be read back. A failure here leaves
		// the requested port, which is only wrong in the port 0 case and is reported below anyway.
		BoundPort = Desc.Port;
		sockaddr_in BoundAddress{};
		int BoundLength = sizeof(BoundAddress);
		if (getsockname(Listener, reinterpret_cast<sockaddr*>(&BoundAddress), &BoundLength) == 0)
		{
			BoundPort = ntohs(BoundAddress.sin_port);
		}

		RegisterBuiltinAutomationCommands();

		ListenSocket.store(static_cast<uintptr_t>(Listener));
		bStopping.store(false);
		bRunning.store(true);
		ListenerThread = std::thread([this] { ListenerLoop(); });

		if (Desc.bWritePortFile)
		{
			WritePortFile();
		}

		LIME_LOG_INFO(LIME_LOG_CATEGORY_AUTOMATION, "Automation server listening on 127.0.0.1:{} with {} command(s)", BoundPort,
		              FAutomationCommandRegistry::Get().GetAll().size());
		return true;
	}

	void FAutomationServer::Shutdown()
	{
		if (!bRunning.exchange(false))
		{
			return;
		}

		bStopping.store(true);

		// Closing the listener is what unblocks accept(); there is no portable interruption.
		const uintptr_t Listener = ListenSocket.exchange(InvalidSocket);
		if (Listener != InvalidSocket)
		{
			closesocket(static_cast<SOCKET>(Listener));
		}

		if (ListenerThread.joinable())
		{
			ListenerThread.join();
		}

		{
			std::lock_guard<std::mutex> Lock(ConnectionMutex);
			for (std::thread& Thread : ConnectionThreads)
			{
				if (Thread.joinable())
				{
					Thread.join();
				}
			}
			ConnectionThreads.clear();
		}

		// Anything still queued would otherwise leave a client blocked on a broken promise.
		{
			std::lock_guard<std::mutex> Lock(QueueMutex);
			for (std::unique_ptr<FPendingCommand>& Command : Queue)
			{
				Command->Reply.set_value(MakeError(nullptr, "The engine is shutting down"));
			}
			Queue.clear();
		}

		for (FDeferredCommand& Command : Deferred)
		{
			Command.Reply.set_value(MakeError(nullptr, "The engine is shutting down"));
		}
		Deferred.clear();

		if (Desc.bWritePortFile)
		{
			RemovePortFile();
		}

		FWinsockScope::Release();
		LIME_LOG_INFO(LIME_LOG_CATEGORY_AUTOMATION, "Automation server stopped");
	}

	void FAutomationServer::WritePortFile() const
	{
		const std::filesystem::path Path = FPlatformPaths::GetExecutableDirectory() / PortFileName;
		std::ofstream File(Path, std::ios::trunc);
		if (!File)
		{
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_AUTOMATION, "Could not write '{}'", Path.string());
			return;
		}
		File << BoundPort << '\n';
	}

	void FAutomationServer::RemovePortFile() const
	{
		std::error_code ErrorCode;
		std::filesystem::remove(FPlatformPaths::GetExecutableDirectory() / PortFileName, ErrorCode);
	}

	void FAutomationServer::ListenerLoop()
	{
		while (!bStopping.load())
		{
			const uintptr_t Listener = ListenSocket.load();
			if (Listener == InvalidSocket)
			{
				break;
			}

			const SOCKET Client = accept(static_cast<SOCKET>(Listener), nullptr, nullptr);
			if (Client == INVALID_SOCKET)
			{
				// Expected once the listener was closed by Shutdown.
				if (!bStopping.load())
				{
					LIME_LOG_WARNING(LIME_LOG_CATEGORY_AUTOMATION, "accept() failed ({})", WSAGetLastError());
				}
				break;
			}

			std::lock_guard<std::mutex> Lock(ConnectionMutex);
			// Threads of closed connections are reaped here rather than in a dedicated thread.
			for (auto Iterator = ConnectionThreads.begin(); Iterator != ConnectionThreads.end();)
			{
				if (!Iterator->joinable())
				{
					Iterator = ConnectionThreads.erase(Iterator);
				}
				else
				{
					++Iterator;
				}
			}
			ConnectionThreads.emplace_back([this, Client] { ConnectionLoop(static_cast<uintptr_t>(Client)); });
		}
	}

	void FAutomationServer::ConnectionLoop(uintptr_t ClientSocket)
	{
		std::string Buffer;
		std::array<char, 4096> Chunk{};

		while (!bStopping.load())
		{
			const int Received = recv(static_cast<SOCKET>(ClientSocket), Chunk.data(), static_cast<int>(Chunk.size()), 0);
			if (Received <= 0)
			{
				break;
			}

			Buffer.append(Chunk.data(), static_cast<SizeType>(Received));
			if (Buffer.size() > MaxRequestBytes)
			{
				SendAll(ClientSocket, MakeError(nullptr, "Request exceeds the size limit").dump() + "\n");
				break;
			}

			// One line is one request; a partial tail stays in the buffer for the next recv.
			SizeType LineEnd = Buffer.find('\n');
			while (LineEnd != std::string::npos)
			{
				std::string Line = Buffer.substr(0, LineEnd);
				Buffer.erase(0, LineEnd + 1);

				if (!Line.empty() && Line.back() == '\r')
				{
					Line.pop_back();
				}

				if (!Line.empty())
				{
					const FJson Reply = HandleRequestLine(Line);
					if (!SendAll(ClientSocket, Reply.dump() + "\n"))
					{
						closesocket(static_cast<SOCKET>(ClientSocket));
						return;
					}
				}

				LineEnd = Buffer.find('\n');
			}
		}

		closesocket(static_cast<SOCKET>(ClientSocket));
	}

	FJson FAutomationServer::HandleRequestLine(const std::string& Line)
	{
		FJson Request;
		try
		{
			Request = FJson::parse(Line);
		}
		catch (const FJson::parse_error& Error)
		{
			return MakeError(nullptr, std::string("Malformed JSON: ") + Error.what());
		}

		if (!Request.is_object())
		{
			return MakeError(nullptr, "A request must be a JSON object");
		}

		// Echoed back untouched so a client can correlate replies without assuming a type.
		const FJson Id = Request.contains("id") ? Request["id"] : FJson(nullptr);

		const auto CommandIterator = Request.find("command");
		if (CommandIterator == Request.end() || !CommandIterator->is_string())
		{
			return MakeError(Id, "A request needs a 'command' string");
		}

		FJson Params = FJson::object();
		if (const auto ParamsIterator = Request.find("params"); ParamsIterator != Request.end())
		{
			if (!ParamsIterator->is_object())
			{
				return MakeError(Id, "'params' must be an object");
			}
			Params = *ParamsIterator;
		}

		return DispatchCommand(CommandIterator->get<std::string>(), std::move(Params), Id);
	}

	FJson FAutomationServer::DispatchCommand(std::string Name, FJson Params, const FJson& Id)
	{
		if (FAutomationCommandRegistry::Get().Find(Name) == nullptr)
		{
			return MakeError(Id, "Unknown command '" + Name + "'; call 'help' for the list");
		}

		auto Command = std::make_unique<FPendingCommand>();
		Command->Name = std::move(Name);
		Command->Params = std::move(Params);
		std::future<FJson> Future = Command->Reply.get_future();

		{
			std::lock_guard<std::mutex> Lock(QueueMutex);
			if (!bRunning.load())
			{
				return MakeError(Id, "The engine is shutting down");
			}
			Queue.push_back(std::move(Command));
		}

		// The main thread runs the command during its next Tick. A dead main loop would hang the
		// client forever, so the wait is bounded.
		if (Future.wait_for(std::chrono::seconds(30)) != std::future_status::ready)
		{
			return MakeError(Id, "Timed out waiting for the main thread");
		}

		FJson Reply = Future.get();
		Reply["id"] = Id;
		return Reply;
	}

	FJson FAutomationServer::MakeError(const FJson& Id, std::string Message)
	{
		FJson Reply = FJson::object();
		Reply["id"] = Id;
		Reply["ok"] = false;
		Reply["error"] = std::move(Message);
		return Reply;
	}

	FJson FAutomationServer::MakeSuccess(const FJson& Id, FJson Result)
	{
		FJson Reply = FJson::object();
		Reply["id"] = Id;
		Reply["ok"] = true;
		Reply["result"] = std::move(Result);
		return Reply;
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
				Command->Reply.set_value(MakeError(nullptr, "Unknown command '" + Command->Name + "'"));
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
				Command->Reply.set_value(MakeError(nullptr, std::string("Command threw: ") + Error.what()));
				continue;
			}

			if (Invocation.HasFailed())
			{
				Command->Reply.set_value(MakeError(nullptr, Invocation.GetError()));
				continue;
			}

			if (Invocation.IsDeferred())
			{
				Deferred.push_back(FDeferredCommand{ Invocation.TakeCompletion(), Invocation.GetResult(), std::move(Command->Reply) });
				continue;
			}

			Command->Reply.set_value(MakeSuccess(nullptr, Invocation.GetResult()));
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
				Command.Reply.set_value(MakeSuccess(nullptr, std::move(Command.Result)));
			}
			else
			{
				Command.Reply.set_value(MakeError(nullptr, std::move(Error)));
			}
		}
	}
} // namespace Lime
