#include "Platform/PlatformTime.h"

namespace Lime
{
	namespace
	{
		constexpr float FpsSmoothingFactor = 0.1f;
	}

	FTimer::FTimer()
	{
		Reset();
	}

	void FTimer::Reset()
	{
		StartTime = FClock::now();
		LastTime = StartTime;
		DeltaSeconds = 0.0f;
		TotalSeconds = 0.0;
		FrameCount = 0;
		SmoothedFps = 0.0f;
	}

	float FTimer::Tick()
	{
		const FClock::time_point Now = FClock::now();
		DeltaSeconds = std::chrono::duration<float>(Now - LastTime).count();
		LastTime = Now;

		if (DeltaSeconds > MaxDeltaSeconds)
		{
			DeltaSeconds = MaxDeltaSeconds;
		}

		TotalSeconds = std::chrono::duration<double>(Now - StartTime).count();
		++FrameCount;

		if (DeltaSeconds > 0.0f)
		{
			const float InstantFps = 1.0f / DeltaSeconds;
			SmoothedFps = SmoothedFps <= 0.0f ? InstantFps : SmoothedFps + (InstantFps - SmoothedFps) * FpsSmoothingFactor;
		}

		return DeltaSeconds;
	}
} // namespace Lime
