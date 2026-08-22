# LimeCore: types, logging, assertions and math. No graphics dependencies.

lime_add_module(
	NAME LimeCore
	SOURCES
		CoreMinimal.h
		CoreTypes.h
		LimeAssert.h
		LimeAssert.cpp
		Logging/LogManager.h
		Logging/LogManager.cpp
		Logging/LogRingBuffer.h
		Logging/LogRingBuffer.cpp
		Math/MathUtils.h
		Math/Matrix.h
		Math/Matrix.cpp
		Math/Vector.h
		Math/Vector.cpp
	PUBLIC_DEPS
		spdlog::spdlog
	FOLDER "Engine/Runtime"
)
