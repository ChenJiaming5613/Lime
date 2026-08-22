#include "Core/Logging/LogRingBuffer.h"

#include <algorithm>

namespace Lime
{
	const char* ToString(ELogLevel Level)
	{
		switch (Level)
		{
			case ELogLevel::Trace:
				return "Trace";
			case ELogLevel::Debug:
				return "Debug";
			case ELogLevel::Info:
				return "Info";
			case ELogLevel::Warning:
				return "Warning";
			case ELogLevel::Error:
				return "Error";
			case ELogLevel::Critical:
				return "Critical";
			default:
				return "Unknown";
		}
	}

	FLogRingBuffer::FLogRingBuffer(SizeType InCapacity)
	    : Capacity(std::max<SizeType>(InCapacity, 1))
	{
		Entries.resize(Capacity);
	}

	void FLogRingBuffer::Push(FLogEntry Entry)
	{
		const std::lock_guard<std::mutex> Lock(Mutex);

		Entries[Head] = std::move(Entry);
		Head = (Head + 1) % Capacity;
		Count = std::min(Count + 1, Capacity);
		++Revision;
	}

	void FLogRingBuffer::Clear()
	{
		const std::lock_guard<std::mutex> Lock(Mutex);

		for (SizeType Index = 0; Index < Capacity; ++Index)
		{
			Entries[Index] = FLogEntry{};
		}
		Head = 0;
		Count = 0;
		++Revision;
	}

	void FLogRingBuffer::CopyTo(std::vector<FLogEntry>& OutEntries) const
	{
		const std::lock_guard<std::mutex> Lock(Mutex);

		OutEntries.clear();
		OutEntries.reserve(Count);

		const SizeType Start = (Head + Capacity - Count) % Capacity;
		for (SizeType Index = 0; Index < Count; ++Index)
		{
			OutEntries.push_back(Entries[(Start + Index) % Capacity]);
		}
	}

	SizeType FLogRingBuffer::GetCount() const
	{
		const std::lock_guard<std::mutex> Lock(Mutex);
		return Count;
	}

	uint64 FLogRingBuffer::GetRevision() const
	{
		const std::lock_guard<std::mutex> Lock(Mutex);
		return Revision;
	}
} // namespace Lime
