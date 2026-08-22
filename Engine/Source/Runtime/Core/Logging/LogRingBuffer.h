// Fixed capacity, thread safe log storage consumed by the editor console.

#pragma once

#include "Core/CoreTypes.h"

#include <mutex>
#include <string>
#include <vector>

namespace Lime
{
	enum class ELogLevel : uint8
	{
		Trace = 0,
		Debug,
		Info,
		Warning,
		Error,
		Critical,
		Count
	};

	const char* ToString(ELogLevel Level);

	struct FLogEntry
	{
		double TimeSeconds = 0.0;
		ELogLevel Level = ELogLevel::Info;
		std::string Category;
		std::string Message;
	};

	// Overwrites the oldest entry once full. Readers take a snapshot so the UI never holds the lock
	// while iterating, and never formats strings on the render thread.
	class FLogRingBuffer
	{
	public:
		static constexpr SizeType DefaultCapacity = 4096;

		explicit FLogRingBuffer(SizeType InCapacity = DefaultCapacity);

		LIME_NON_COPYABLE(FLogRingBuffer);
		LIME_NON_MOVABLE(FLogRingBuffer);

		void Push(FLogEntry Entry);
		void Clear();

		// Appends every stored entry, oldest first.
		void CopyTo(std::vector<FLogEntry>& OutEntries) const;

		SizeType GetCapacity() const { return Capacity; }
		SizeType GetCount() const;
		// Monotonically increasing across the buffer lifetime; lets readers detect new content cheaply.
		uint64 GetRevision() const;

	private:
		mutable std::mutex Mutex;
		std::vector<FLogEntry> Entries;
		SizeType Capacity = DefaultCapacity;
		SizeType Head = 0;
		SizeType Count = 0;
		uint64 Revision = 0;
	};
} // namespace Lime
