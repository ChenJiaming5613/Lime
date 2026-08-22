#include "Core/Logging/LogRingBuffer.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <thread>
#include <vector>

using namespace Lime;

namespace
{
	FLogEntry MakeEntry(std::string Message, ELogLevel Level = ELogLevel::Info)
	{
		FLogEntry Entry;
		Entry.Level = Level;
		Entry.Category = "Test";
		Entry.Message = std::move(Message);
		return Entry;
	}
} // namespace

TEST_CASE("Ring buffer stores entries in order", "[Logging]")
{
	FLogRingBuffer Buffer(4);
	REQUIRE(Buffer.GetCapacity() == 4);
	REQUIRE(Buffer.GetCount() == 0);

	Buffer.Push(MakeEntry("A"));
	Buffer.Push(MakeEntry("B"));

	std::vector<FLogEntry> Snapshot;
	Buffer.CopyTo(Snapshot);

	REQUIRE(Snapshot.size() == 2);
	REQUIRE(Snapshot[0].Message == "A");
	REQUIRE(Snapshot[1].Message == "B");
}

TEST_CASE("Ring buffer overwrites the oldest entry once full", "[Logging]")
{
	FLogRingBuffer Buffer(3);
	for (const char* Message : { "1", "2", "3", "4", "5" })
	{
		Buffer.Push(MakeEntry(Message));
	}

	REQUIRE(Buffer.GetCount() == 3);

	std::vector<FLogEntry> Snapshot;
	Buffer.CopyTo(Snapshot);

	REQUIRE(Snapshot.size() == 3);
	REQUIRE(Snapshot[0].Message == "3");
	REQUIRE(Snapshot[1].Message == "4");
	REQUIRE(Snapshot[2].Message == "5");
}

TEST_CASE("Clear empties the buffer but keeps the capacity", "[Logging]")
{
	FLogRingBuffer Buffer(2);
	Buffer.Push(MakeEntry("A"));
	Buffer.Push(MakeEntry("B"));
	Buffer.Clear();

	REQUIRE(Buffer.GetCount() == 0);
	REQUIRE(Buffer.GetCapacity() == 2);

	std::vector<FLogEntry> Snapshot;
	Buffer.CopyTo(Snapshot);
	REQUIRE(Snapshot.empty());

	// Writing after a clear must start from the beginning again.
	Buffer.Push(MakeEntry("C"));
	Buffer.CopyTo(Snapshot);
	REQUIRE(Snapshot.size() == 1);
	REQUIRE(Snapshot[0].Message == "C");
}

TEST_CASE("Revision advances on every mutation so readers can skip copies", "[Logging]")
{
	FLogRingBuffer Buffer(4);
	const uint64 Initial = Buffer.GetRevision();

	Buffer.Push(MakeEntry("A"));
	const uint64 AfterPush = Buffer.GetRevision();
	REQUIRE(AfterPush > Initial);

	std::vector<FLogEntry> Snapshot;
	Buffer.CopyTo(Snapshot);
	REQUIRE(Buffer.GetRevision() == AfterPush);

	Buffer.Clear();
	REQUIRE(Buffer.GetRevision() > AfterPush);
}

TEST_CASE("Levels and categories survive the round trip", "[Logging]")
{
	FLogRingBuffer Buffer(8);
	Buffer.Push(MakeEntry("trace", ELogLevel::Trace));
	Buffer.Push(MakeEntry("warning", ELogLevel::Warning));
	Buffer.Push(MakeEntry("critical", ELogLevel::Critical));

	std::vector<FLogEntry> Snapshot;
	Buffer.CopyTo(Snapshot);

	REQUIRE(Snapshot.size() == 3);
	REQUIRE(Snapshot[0].Level == ELogLevel::Trace);
	REQUIRE(Snapshot[1].Level == ELogLevel::Warning);
	REQUIRE(Snapshot[2].Level == ELogLevel::Critical);
	REQUIRE(Snapshot[0].Category == "Test");

	REQUIRE(std::string(ToString(ELogLevel::Warning)) == "Warning");
	REQUIRE(std::string(ToString(ELogLevel::Critical)) == "Critical");
}

TEST_CASE("A capacity of zero is clamped to one", "[Logging]")
{
	FLogRingBuffer Buffer(0);
	REQUIRE(Buffer.GetCapacity() == 1);

	Buffer.Push(MakeEntry("A"));
	Buffer.Push(MakeEntry("B"));

	std::vector<FLogEntry> Snapshot;
	Buffer.CopyTo(Snapshot);
	REQUIRE(Snapshot.size() == 1);
	REQUIRE(Snapshot[0].Message == "B");
}

TEST_CASE("Concurrent writers do not lose or corrupt entries", "[Logging]")
{
	constexpr int32 ThreadCount = 4;
	constexpr int32 PerThread = 250;

	FLogRingBuffer Buffer(ThreadCount * PerThread);
	std::vector<std::thread> Workers;
	Workers.reserve(ThreadCount);

	for (int32 ThreadIndex = 0; ThreadIndex < ThreadCount; ++ThreadIndex)
	{
		Workers.emplace_back(
		    [&Buffer, ThreadIndex]
		    {
			    for (int32 Index = 0; Index < PerThread; ++Index)
			    {
				    Buffer.Push(MakeEntry(std::to_string(ThreadIndex) + ":" + std::to_string(Index)));
			    }
		    });
	}

	for (std::thread& Worker : Workers)
	{
		Worker.join();
	}

	REQUIRE(Buffer.GetCount() == static_cast<SizeType>(ThreadCount * PerThread));

	std::vector<FLogEntry> Snapshot;
	Buffer.CopyTo(Snapshot);
	REQUIRE(Snapshot.size() == static_cast<SizeType>(ThreadCount * PerThread));
	for (const FLogEntry& Entry : Snapshot)
	{
		REQUIRE_FALSE(Entry.Message.empty());
	}
}
