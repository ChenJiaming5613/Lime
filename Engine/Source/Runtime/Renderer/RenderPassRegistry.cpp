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

	void FRenderPassRegistry::Register(const char* Name, ERenderPassPriority Priority, FRenderPassRegistration::FFactory Factory)
	{
		if (Factory == nullptr)
		{
			return;
		}
		Registrations.push_back(FRenderPassRegistration{ Name, Priority, std::move(Factory) });
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
} // namespace Lime
