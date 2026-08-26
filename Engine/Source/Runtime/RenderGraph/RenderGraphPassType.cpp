#include "RenderGraph/RenderGraphPassType.h"

#include <algorithm>

namespace Lime
{
	namespace
	{
		const FRenderGraphResourceDesc* FindByName(const std::vector<FRenderGraphResourceDesc>& Resources, std::string_view Name)
		{
			const auto Found = std::find_if(Resources.begin(), Resources.end(),
			                                [Name](const FRenderGraphResourceDesc& Resource) { return Resource.Name == Name; });
			return Found != Resources.end() ? &*Found : nullptr;
		}

		FRenderGraphResourceDesc MakeTexture(std::string Name, std::string Format)
		{
			FRenderGraphResourceDesc Resource;
			Resource.Name = std::move(Name);
			Resource.Kind = EFrameResourceKind::Texture;
			Resource.Format = std::move(Format);
			return Resource;
		}
	} // namespace

	const FRenderGraphResourceDesc* FRenderGraphPassTypeDesc::FindInput(std::string_view ResourceName) const
	{
		return FindByName(Inputs, ResourceName);
	}

	const FRenderGraphResourceDesc* FRenderGraphPassTypeDesc::FindOutput(std::string_view ResourceName) const
	{
		return FindByName(Outputs, ResourceName);
	}

	FRenderGraphPassTypeRegistry::FRenderGraphPassTypeRegistry()
	{
		// A small pipeline that exercises every shape the inspector has to draw: a pass with outputs only,
		// passes with both, a pass that reads two resources at once, and a pass with inputs only. Chosen
		// for that coverage rather than for fidelity to any particular renderer.
		FRenderGraphPassTypeDesc ShadowCaster;
		ShadowCaster.Name = "ShadowCaster";
		ShadowCaster.Description = "Renders the scene from the light to produce a shadow depth map.";
		ShadowCaster.Outputs.push_back(MakeTexture("depth", "D32_FLOAT"));
		Register(std::move(ShadowCaster));

		FRenderGraphPassTypeDesc DepthPrepass;
		DepthPrepass.Name = "DepthPrepass";
		DepthPrepass.Description = "Renders depth only, so later passes can reject hidden fragments early.";
		DepthPrepass.Outputs.push_back(MakeTexture("depth", "D32_FLOAT"));
		Register(std::move(DepthPrepass));

		FRenderGraphPassTypeDesc ForwardLit;
		ForwardLit.Name = "ForwardLit";
		ForwardLit.Description = "Shades the scene, reading the shadow map and the prepass depth.";
		ForwardLit.Inputs.push_back(MakeTexture("shadowDepth", "D32_FLOAT"));
		ForwardLit.Inputs.push_back(MakeTexture("sceneDepth", "D32_FLOAT"));
		ForwardLit.Outputs.push_back(MakeTexture("color", "RGBA16_FLOAT"));
		Register(std::move(ForwardLit));

		FRenderGraphPassTypeDesc PostProcess;
		PostProcess.Name = "PostProcess";
		PostProcess.Description = "Full screen filter: one colour target in, one out.";
		PostProcess.Inputs.push_back(MakeTexture("input", "RGBA16_FLOAT"));
		PostProcess.Outputs.push_back(MakeTexture("output", "RGBA16_FLOAT"));
		Register(std::move(PostProcess));

		FRenderGraphPassTypeDesc ToneMap;
		ToneMap.Name = "ToneMap";
		ToneMap.Description = "Maps high dynamic range colour into the display range.";
		ToneMap.Inputs.push_back(MakeTexture("hdr", "RGBA16_FLOAT"));
		ToneMap.Outputs.push_back(MakeTexture("ldr", "RGBA8_UNORM"));
		Register(std::move(ToneMap));

		FRenderGraphPassTypeDesc Present;
		Present.Name = "Present";
		Present.Description = "Copies the final image to the swap chain. Has no outputs of its own.";
		Present.Inputs.push_back(MakeTexture("image", "RGBA8_UNORM"));
		Register(std::move(Present));
	}

	void FRenderGraphPassTypeRegistry::Register(FRenderGraphPassTypeDesc Type)
	{
		const auto Existing =
		    std::find_if(Types.begin(), Types.end(), [&Type](const FRenderGraphPassTypeDesc& Candidate) { return Candidate.Name == Type.Name; });
		if (Existing != Types.end())
		{
			*Existing = std::move(Type);
			return;
		}

		Types.push_back(std::move(Type));
	}

	const FRenderGraphPassTypeDesc* FRenderGraphPassTypeRegistry::Find(std::string_view TypeName) const
	{
		const auto Found =
		    std::find_if(Types.begin(), Types.end(), [TypeName](const FRenderGraphPassTypeDesc& Type) { return Type.Name == TypeName; });
		return Found != Types.end() ? &*Found : nullptr;
	}
} // namespace Lime
