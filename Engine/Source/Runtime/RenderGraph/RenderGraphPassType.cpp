#include "RenderGraph/RenderGraphPassType.h"

#include <algorithm>
#include <utility>

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
	} // namespace

	const FRenderGraphResourceDesc* FRenderGraphPassTypeDesc::FindInput(std::string_view ResourceName) const
	{
		return FindByName(Inputs, ResourceName);
	}

	const FRenderGraphResourceDesc* FRenderGraphPassTypeDesc::FindOutput(std::string_view ResourceName) const
	{
		return FindByName(Outputs, ResourceName);
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

	FRenderGraphResourceDesc MakeTextureResource(std::string Name, ERenderGraphResourceVisibility Visibility, nvrhi::Format Format)
	{
		FRenderGraphResourceDesc Resource;
		Resource.Name = std::move(Name);
		Resource.Kind = ERenderGraphResourceKind::Texture;
		Resource.Visibility = Visibility;
		Resource.Format = Format;
		return Resource;
	}
} // namespace Lime
