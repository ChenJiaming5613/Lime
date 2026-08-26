#include "Renderer/Passes/BuiltinPasses.h"

#include "Core/CoreTypes.h"
#include "Renderer/Passes/BlinnPhongForwardLitPass.h"
#include "Renderer/Passes/PostProcessPass.h"
#include "Renderer/Passes/ShadowCasterPass.h"
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
			                                    [] { return std::static_pointer_cast<IRenderPass>(std::make_shared<PassType>()); });
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
			RegisterPass<FPostProcessPass>();
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
