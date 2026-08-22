// Locks the pass ordering contract: priority decides the draw order regardless of registration
// order, and ties keep registration order so a pass list stays predictable.

#include "Renderer/RenderPassRegistry.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace Lime;

namespace
{
	FRenderPassRegistration MakeRegistration(const char* Name, ERenderPassPriority Priority)
	{
		FRenderPassRegistration Registration;
		Registration.Name = Name;
		Registration.Priority = Priority;
		// A factory is required by Register but irrelevant to sorting.
		Registration.Factory = [] { return nullptr; };
		return Registration;
	}

	std::vector<std::string> NamesOf(const std::vector<FRenderPassRegistration>& Registrations)
	{
		std::vector<std::string> Names;
		Names.reserve(Registrations.size());
		for (const FRenderPassRegistration& Registration : Registrations)
		{
			Names.emplace_back(Registration.Name);
		}
		return Names;
	}
} // namespace

TEST_CASE("Passes are ordered by priority, not by registration order", "[Renderer][PassRegistry]")
{
	// Registered in the wrong order on purpose: the editor registers its UI pass before project
	// passes exist, which is exactly the case the priority is there to fix.
	std::vector<FRenderPassRegistration> Registrations{
		MakeRegistration("EditorUI", ERenderPassPriority::EditorUI),       MakeRegistration("Scene", ERenderPassPriority::Scene),
		MakeRegistration("Background", ERenderPassPriority::Background),   MakeRegistration("Overlay", ERenderPassPriority::Overlay),
		MakeRegistration("PostProcess", ERenderPassPriority::PostProcess),
	};

	SortRenderPassRegistrations(Registrations);

	REQUIRE(NamesOf(Registrations) == std::vector<std::string>{ "Background", "Scene", "PostProcess", "Overlay", "EditorUI" });
}

TEST_CASE("The editor UI pass always ends up last", "[Renderer][PassRegistry]")
{
	std::vector<FRenderPassRegistration> Registrations{
		MakeRegistration("ImGui", ERenderPassPriority::EditorUI),
		MakeRegistration("Triangle", ERenderPassPriority::Scene),
	};

	SortRenderPassRegistrations(Registrations);

	REQUIRE(std::string(Registrations.back().Name) == "ImGui");
}

TEST_CASE("Equal priorities keep registration order", "[Renderer][PassRegistry]")
{
	std::vector<FRenderPassRegistration> Registrations{
		MakeRegistration("First", ERenderPassPriority::Scene),
		MakeRegistration("Second", ERenderPassPriority::Scene),
		MakeRegistration("Third", ERenderPassPriority::Scene),
	};

	SortRenderPassRegistrations(Registrations);

	REQUIRE(NamesOf(Registrations) == std::vector<std::string>{ "First", "Second", "Third" });
}

TEST_CASE("Sorting handles empty and single element lists", "[Renderer][PassRegistry]")
{
	std::vector<FRenderPassRegistration> Empty;
	SortRenderPassRegistrations(Empty);
	REQUIRE(Empty.empty());

	std::vector<FRenderPassRegistration> Single{ MakeRegistration("Only", ERenderPassPriority::Overlay) };
	SortRenderPassRegistrations(Single);
	REQUIRE(Single.size() == 1);
	REQUIRE(std::string(Single.front().Name) == "Only");
}

TEST_CASE("The registry rejects null factories", "[Renderer][PassRegistry]")
{
	// The registry is a process wide singleton, so this only checks that a bad entry is not stored.
	const SizeType CountBefore = FRenderPassRegistry::Get().GetRegistrations().size();
	FRenderPassRegistry::Get().Register("Invalid", ERenderPassPriority::Scene, nullptr);
	REQUIRE(FRenderPassRegistry::Get().GetRegistrations().size() == CountBefore);
}

TEST_CASE("The priority also decides the render stage", "[Renderer][PassRegistry]")
{
	// Scene passes draw into the viewport target in editor mode; editor UI passes always draw into the
	// back buffer. The split point is the EditorUI priority, so this mapping is part of the contract.
	// Overlay staying on the scene side is what lets in-world UI be composited into the scene image.
	const auto IsEditorUIStage = [](ERenderPassPriority Priority)
	{ return static_cast<int32>(Priority) >= static_cast<int32>(ERenderPassPriority::EditorUI); };

	REQUIRE_FALSE(IsEditorUIStage(ERenderPassPriority::Background));
	REQUIRE_FALSE(IsEditorUIStage(ERenderPassPriority::Scene));
	REQUIRE_FALSE(IsEditorUIStage(ERenderPassPriority::PostProcess));
	REQUIRE_FALSE(IsEditorUIStage(ERenderPassPriority::Overlay));
	REQUIRE(IsEditorUIStage(ERenderPassPriority::EditorUI));
}
