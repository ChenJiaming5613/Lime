// Render pass registry.
//
// Passes register themselves from their own translation unit with LIME_REGISTER_RENDER_PASS, so a
// project only writes the class and the engine instantiates it at the right point in startup.
//
// This relies on static initializers running, which is guaranteed because project sources are
// compiled straight into the executable (see lime_add_project). Putting them in a static library
// instead would let the linker discard the registering object files.

#pragma once

#include "Renderer/RenderTypes.h"

#include <functional>
#include <memory>
#include <vector>

namespace Lime
{
	class FRenderer;

	struct FRenderPassRegistration
	{
		using FFactory = std::function<std::shared_ptr<IRenderPass>()>;

		const char* Name = nullptr;
		ERenderPassPriority Priority = ERenderPassPriority::Scene;
		FFactory Factory;
	};

	class FRenderPassRegistry
	{
	public:
		static FRenderPassRegistry& Get();

		void Register(const char* Name, ERenderPassPriority Priority, FRenderPassRegistration::FFactory Factory);

		// Creates and registers every entry, ordered by priority. Called once by FEngine after the
		// device exists, so passes always see a usable renderer.
		void InstantiateAll(FRenderer& Renderer) const;

		const std::vector<FRenderPassRegistration>& GetRegistrations() const { return Registrations; }

		// Sorted copy used both by InstantiateAll and by tests.
		std::vector<FRenderPassRegistration> GetSortedRegistrations() const;

	private:
		FRenderPassRegistry() = default;

		std::vector<FRenderPassRegistration> Registrations;
	};

	// Sorting is a free function so the ordering rule can be tested without a GPU.
	void SortRenderPassRegistrations(std::vector<FRenderPassRegistration>& Registrations);
} // namespace Lime

// Registers a pass type. Place at file scope in the pass's .cpp; the type must be default
// constructible and may be namespace qualified.
#define LIME_REGISTER_RENDER_PASS(PassType)                                                                                                \
	namespace                                                                                                                              \
	{                                                                                                                                      \
		const bool LIME_CONCAT(bLimeRegisteredPass_, __LINE__) = []                                                                        \
		{                                                                                                                                  \
			::Lime::FRenderPassRegistry::Get().Register(                                                                                   \
			    #PassType, PassType::Priority,                                                                                             \
			    [] { return std::static_pointer_cast<::Lime::IRenderPass>(std::make_shared<PassType>()); });                               \
			return true;                                                                                                                   \
		}();                                                                                                                               \
	}
