// Optional application hooks.
//
// Most projects only register passes and panels and need nothing here. Implement this and use
// LIME_IMPLEMENT_APPLICATION when global setup or teardown is required.

#pragma once

#include "Core/CoreTypes.h"

#include <functional>
#include <memory>

namespace Lime
{
	class FEngine;

	class ILimeApplication
	{
	public:
		virtual ~ILimeApplication() = default;

		// Runs after every subsystem is up and passes are registered. Return false to abort startup.
		virtual bool OnStartup(FEngine& Engine)
		{
			LIME_UNUSED(Engine);
			return true;
		}

		// Called once per frame before rendering. Passes usually update themselves instead.
		virtual void OnUpdate(float DeltaSeconds) { LIME_UNUSED(DeltaSeconds); }

		virtual void OnShutdown() {}
	};

	// Set by LIME_IMPLEMENT_APPLICATION. A registration hook is used rather than an overridable
	// function because MSVC has no portable weak symbols, so a default definition plus a project
	// override would be a duplicate symbol.
	class FApplicationFactory
	{
	public:
		using FFactory = std::function<std::unique_ptr<ILimeApplication>()>;

		static void Set(FFactory Factory);
		// Returns the project application, or a no-op instance when none was declared.
		static std::unique_ptr<ILimeApplication> Create();

	private:
		static FFactory& GetStorage();
	};
} // namespace Lime

// Declares the application type used by the engine entry point. Optional; place at file scope.
#define LIME_IMPLEMENT_APPLICATION(ApplicationType)                                                                                        \
	namespace                                                                                                                              \
	{                                                                                                                                      \
		const bool bLimeApplicationRegistered = []                                                                                         \
		{                                                                                                                                  \
			::Lime::FApplicationFactory::Set([] { return std::make_unique<ApplicationType>(); });                                          \
			return true;                                                                                                                   \
		}();                                                                                                                               \
	}
