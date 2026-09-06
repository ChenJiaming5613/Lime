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
		// True for passes the engine itself provides, false for a project's own.
		//
		// Recorded rather than inferred from the name: a script that wants to drive the project's pass has
		// to be able to tell them apart, and a list of engine pass names kept outside the engine goes stale
		// the moment one is added or renamed.
		bool bIsBuiltin = false;
		// True when the pass should be added to the renderer as a long lived instance. False when the
		// pass is created on demand by the render graph, one per graph instance, which is the normal
		// case: a single shared instance would alias per-source state (a binding set, a pipeline)
		// across graph instances of the same type.
		//
		// A long lived pass can still appear in the graph. The editor UI pass does: the engine injects it,
		// and the renderer binds the instance from here rather than building a second one, because the
		// texture ids it handed out have to survive a graph recompile.
		bool bIsPermanent = false;
	};

	class FRenderPassRegistry
	{
	public:
		static FRenderPassRegistry& Get();

		// bIsBuiltin defaults to false, so a project registering through LIME_REGISTER_RENDER_PASS is
		// correctly reported as its own without having to say so.
		//
		// Name must be the pass's GetTypeName, which is the key the render graph's type table uses. The
		// registration macro takes it from there for that reason: a name that disagrees compiles against
		// the table and then fails to resolve a factory.
		void Register(const char* Name, ERenderPassPriority Priority, FRenderPassRegistration::FFactory Factory, bool bIsBuiltin = false,
		   bool bIsPermanent = false);

		// Creates and registers every entry, ordered by priority. Called once by FEngine after the
		// device exists, so passes always see a usable renderer.
		void InstantiateAll(FRenderer& Renderer) const;

		const std::vector<FRenderPassRegistration>& GetRegistrations() const { return Registrations; }

		// Sorted copy used both by InstantiateAll and by tests.
		std::vector<FRenderPassRegistration> GetSortedRegistrations() const;

		// The pass types a render graph can refer to, reflected from the registered passes.
		//
		// Built by instantiating each registration and asking it to describe itself, which is why it costs
		// a construction per pass and is only done at startup. The alternative was a hand written table,
		// and a graph validated against a table that had drifted from the passes would report a file as
		// good and then fail to run it.
		FRenderGraphPassTypeRegistry BuildPassTypes() const;

	private:
		FRenderPassRegistry() = default;

		std::vector<FRenderPassRegistration> Registrations;
	};

	// Sorting is a free function so the ordering rule can be tested without a GPU.
	void SortRenderPassRegistrations(std::vector<FRenderPassRegistration>& Registrations);
} // namespace Lime

// Registers a pass type. Place at file scope in the pass's .cpp; the type must be default
// constructible and may be namespace qualified.
//
// The name comes from GetTypeName on a file scope prototype rather than from stringifying the class, so
// the registered name and the reflected type name cannot disagree. Stringifying produced the qualified
// C++ name ("MyGame::FMyPass") while the graph's type table is keyed on GetTypeName ("MyPass"), so a
// graph naming the pass validated against the table and then failed to find a factory — which reports a
// pass as missing from a build that actually has it. This matches how the built-in passes register.
//
// The prototype is a namespace scope object rather than one local to the lambda because the registration
// stores the char pointer rather than copying it, so the string has to outlive the call.
#define LIME_REGISTER_RENDER_PASS(PassType)                     \
	namespace   \
	{     \
		const PassType LIME_CONCAT(LimePassPrototype_, __LINE__);     \
		const bool LIME_CONCAT(bLimeRegisteredPass_, __LINE__) = []        \
		{     \
			::Lime::FRenderPassRegistry::Get().Register(            \
			  LIME_CONCAT(LimePassPrototype_, __LINE__).GetTypeName(), PassType::Priority,          \
			    [] { return std::static_pointer_cast<::Lime::IRenderPass>(std::make_shared<PassType>()); });          \
			return true;         \
		}();   \
	}
