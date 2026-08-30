#include "Renderer/RenderPassRegistry.h"

#include "Core/Logging/LogManager.h"
#include "Renderer/Renderer.h"

#include <algorithm>

namespace Lime
{
	namespace
	{
		const char* ToString(ERenderPassPriority Priority)
		{
			switch (Priority)
			{
				case ERenderPassPriority::Background:
					return "Background";
				case ERenderPassPriority::Scene:
					return "Scene";
				case ERenderPassPriority::PostProcess:
					return "PostProcess";
				case ERenderPassPriority::Overlay:
					return "Overlay";
				case ERenderPassPriority::EditorUI:
					return "EditorUI";
			}
			return "Custom";
		}
	} // namespace

	FRenderPassRegistry& FRenderPassRegistry::Get()
	{
		// Function local static: constructed on first use, so registrations from other translation
		// units cannot run before the registry exists.
		static FRenderPassRegistry Instance;
		return Instance;
	}

	void FRenderPassRegistry::Register(const char* Name, ERenderPassPriority Priority, FRenderPassRegistration::FFactory Factory,
	                                  bool bIsBuiltin, bool bIsPermanent)
	{
		if (Factory == nullptr)
		{
			return;
		}
		Registrations.push_back(FRenderPassRegistration{ Name, Priority, std::move(Factory), bIsBuiltin, bIsPermanent });
	}

	std::vector<FRenderPassRegistration> FRenderPassRegistry::GetSortedRegistrations() const
	{
		std::vector<FRenderPassRegistration> Sorted = Registrations;
		SortRenderPassRegistrations(Sorted);
		return Sorted;
	}

	void FRenderPassRegistry::InstantiateAll(FRenderer& Renderer) const
	{
		const std::vector<FRenderPassRegistration> Sorted = GetSortedRegistrations();

		if (Sorted.empty())
		{
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_RENDERER, "No render passes are registered; nothing will be drawn");
			return;
		}

		std::string Summary;
		for (const FRenderPassRegistration& Registration : Sorted)
		{
			// Graph-only passes are created on demand by the render graph, one per graph instance. A
			// single shared instance would alias their per-source state, which is the bug that moved
			// them out of the renderer's permanent list. Listed in the summary with a marker so a
			// missing registration is still visible in the log.
			if (!Registration.bIsPermanent)
			{
				if (!Summary.empty())
				{
					Summary += ", ";
				}
				Summary += Registration.Name;
				Summary += '(';
				Summary += ToString(Registration.Priority);
				Summary += ", graph";
				Summary += ')';
				continue;
			}

			std::shared_ptr<IRenderPass> Pass = Registration.Factory();
			if (Pass == nullptr)
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Factory for '{}' returned null", Registration.Name);
				continue;
			}

			if (!Renderer.AddPass(std::move(Pass)))
			{
				continue;
			}

			if (!Summary.empty())
			{
				Summary += ", ";
			}
			Summary += Registration.Name;
			Summary += '(';
			Summary += ToString(Registration.Priority);
			Summary += ')';
		}

		// Logged unconditionally: self registration is otherwise hard to diagnose when a translation
		// unit is accidentally excluded from the build.
		LIME_LOG_INFO(LIME_LOG_CATEGORY_RENDERER, "Registered render pass(es): {}", Summary);
	}

	void SortRenderPassRegistrations(std::vector<FRenderPassRegistration>& Registrations)
	{
		// stable_sort keeps registration order within one priority level.
		std::stable_sort(Registrations.begin(), Registrations.end(),
		                 [](const FRenderPassRegistration& Left, const FRenderPassRegistration& Right)
		                 { return static_cast<int32>(Left.Priority) < static_cast<int32>(Right.Priority); });
	}

	FRenderGraphPassTypeRegistry FRenderPassRegistry::BuildPassTypes() const
	{
		FRenderGraphPassTypeRegistry Types;

		for (const FRenderPassRegistration& Registration : Registrations)
		{
			// Constructed only to be asked what it reads and writes, then discarded. Reflect is required to
			// be cheap and free of device calls precisely so this is safe before a device exists.
			const std::shared_ptr<IRenderPass> Pass = Registration.Factory();
			if (Pass == nullptr)
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Factory for '{}' returned null while reflecting pass types", Registration.Name);
				continue;
			}

			FRenderGraphPassTypeDesc Type;
			Type.Name = Pass->GetTypeName();
			Pass->Reflect(Type);

			// A pass that declares nothing is still registered as a type, so a graph can name it for its
			// ordering alone. Dropping it here would make the file look like it referred to a missing pass.
			Types.Register(std::move(Type));
		}

		return Types;
	}
} // namespace Lime
