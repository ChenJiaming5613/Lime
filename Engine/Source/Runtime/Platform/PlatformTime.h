// High resolution frame timing with a smoothed frame rate.

#pragma once

#include "Core/CoreTypes.h"

#include <chrono>

namespace Lime
{
	class FTimer
	{
	public:
		FTimer();

		// Advances the timer and returns the seconds since the previous call.
		float Tick();
		void Reset();

		float GetDeltaSeconds() const { return DeltaSeconds; }
		double GetTotalSeconds() const { return TotalSeconds; }
		uint64 GetFrameCount() const { return FrameCount; }
		// Exponentially smoothed so the value is readable in a UI.
		float GetFramesPerSecond() const { return SmoothedFps; }

		// Guards against huge deltas after a breakpoint or a long stall.
		void SetMaxDeltaSeconds(float Value) { MaxDeltaSeconds = Value; }

	private:
		using FClock = std::chrono::steady_clock;

		FClock::time_point StartTime;
		FClock::time_point LastTime;
		float DeltaSeconds = 0.0f;
		double TotalSeconds = 0.0;
		uint64 FrameCount = 0;
		float SmoothedFps = 0.0f;
		float MaxDeltaSeconds = 0.25f;
	};
} // namespace Lime
