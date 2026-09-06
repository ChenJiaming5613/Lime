#include "Renderer/Passes/BuiltinPasses.h"

#include "Core/CoreTypes.h"
#include "Renderer/Passes/BlinnPhongForwardLitPass.h"
#include "Renderer/Passes/BlitPass.h"
#include "Renderer/Passes/DebugVisualizerPass.h"
#include "Renderer/Passes/ForwardLitPass.h"
#include "Renderer/Passes/PostProcessPass.h"
#include "Renderer/Passes/ShadowCasterPass.h"
#include "Renderer/Passes/SkyboxPass.h"
#include "Renderer/RenderPassRegistry.h"

#include <memory>

namespace Lime
{
	namespace
	{
		// Registers one pass under the name a graph file refers to it by.
		//
		// GetTypeName rather than a string written here, so the registered name and the reflected type name
		// cannot disagree: a graph naming one but not the other would compile and then fail to resolve.
		template<typename PassType>
		void RegisterPass()
		{
			const PassType Prototype;
			FRenderPassRegistry::Get().Register(Prototype.GetTypeName(), PassType::Priority,
			                                    [] { return std::static_pointer_cast<IRenderPass>(std::make_shared<PassType>()); },
			                                    /*bIsBuiltin*/ true);
		}
	} // namespace

	void RegisterBuiltinRenderPasses()
	{
		// Registration replaces by name, so running twice is harmless. Guarded anyway to avoid rebuilding the
		// list on every call from a test.
		static const bool bRegistered = []
		{
			RegisterPass<FShadowCasterPass>();
			RegisterPass<FBlinnPhongForwardLitPass>();
			// Shares the Blinn-Phong pass's priority: the two are alternative shading models over the same
			// scene, so a graph names one or the other. Naming both draws the scene twice.
			RegisterPass<FForwardLitPass>();
			RegisterPass<FPostProcessPass>();
			RegisterPass<FDebugVisualizerPass>();
			RegisterPass<FSkyboxPass>();
			// Registered like any other pass even though only the engine injects it, so that the injector
			// resolves it through the same factory table as everything else and the reflected type table
			// describes it. A graph file naming it is refused by the reserved prefix, not by its absence.
			RegisterPass<FBlitPass>();
			return true;
		}();
		LIME_UNUSED(bRegistered);
	}

	FRenderGraphPassTypeRegistry BuildRenderGraphPassTypes()
	{
		RegisterBuiltinRenderPasses();
		return FRenderPassRegistry::Get().BuildPassTypes();
	}
} // namespace Lime
