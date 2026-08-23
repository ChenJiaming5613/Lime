# LimeCore: types, logging, assertions and math. No graphics dependencies.

lime_add_module(
	NAME LimeCore
	SOURCES
		CoreMinimal.h
		CoreTypes.h
		LimeAssert.h
		LimeAssert.cpp
		Json/JsonUtils.h
		Json/JsonUtils.cpp
		Logging/LogManager.h
		Logging/LogManager.cpp
		Logging/LogRingBuffer.h
		Logging/LogRingBuffer.cpp
		Math/MathUtils.h
		Math/Matrix.h
		Math/Matrix.cpp
		Math/Quaternion.h
		Math/Quaternion.cpp
		Math/Vector.h
		Math/Vector.cpp
		Reflection/PropertyMeta.h
		Reflection/Reflection.h
		Reflection/JsonArchive.h
		Reflection/JsonArchive.cpp
	PUBLIC_DEPS
		spdlog::spdlog
		nlohmann_json
		entt
	FOLDER "Engine/Runtime"
)
