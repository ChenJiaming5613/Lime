#include "Scene/Scene.h"

#include "Core/Logging/LogManager.h"

#include <algorithm>

namespace Lime
{
	entt::entity FScene::CreateEntity(const std::string& Name)
	{
		const entt::entity Entity = Registry.create();
		Registry.emplace<FNameComponent>(Entity, Name);
		Registry.emplace<FTransformComponent>(Entity);
		Registry.emplace<FWorldTransformComponent>(Entity);
		// Added unconditionally so that walking the hierarchy never has to check whether the component
		// exists; an entity with no parent and no children is simply an isolated root.
		Registry.emplace<FHierarchyComponent>(Entity);

		RootEntities.push_back(Entity);
		return Entity;
	}

	bool FScene::IsDescendantOf(entt::entity Candidate, entt::entity Root) const
	{
		entt::entity Current = Candidate;
		while (Current != entt::null)
		{
			if (Current == Root)
			{
				return true;
			}

			const FHierarchyComponent* Hierarchy = Registry.try_get<FHierarchyComponent>(Current);
			Current = Hierarchy != nullptr ? Hierarchy->Parent : entt::null;
		}
		return false;
	}

	bool FScene::SetParent(entt::entity Child, entt::entity Parent)
	{
		if (Child == entt::null || !Registry.valid(Child))
		{
			return false;
		}

		if (Parent == entt::null)
		{
			DetachFromParent(Child);
			return true;
		}

		if (!Registry.valid(Parent) || Child == Parent)
		{
			return false;
		}

		// A cycle would make UpdateTransforms walk forever. Rejected rather than tolerated, because the
		// alternative is a hang with no diagnostic.
		if (IsDescendantOf(Parent, Child))
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_SCENE, "Refusing to parent an entity to its own descendant");
			return false;
		}

		DetachFromParent(Child);

		Registry.get<FHierarchyComponent>(Child).Parent = Parent;
		Registry.get<FHierarchyComponent>(Parent).Children.push_back(Child);

		// No longer a root now that it has a parent.
		std::erase(RootEntities, Child);
		return true;
	}

	void FScene::DetachFromParent(entt::entity Child)
	{
		if (!Registry.valid(Child))
		{
			return;
		}

		FHierarchyComponent& Hierarchy = Registry.get<FHierarchyComponent>(Child);
		if (Hierarchy.Parent == entt::null)
		{
			return;
		}

		if (Registry.valid(Hierarchy.Parent))
		{
			std::erase(Registry.get<FHierarchyComponent>(Hierarchy.Parent).Children, Child);
		}

		Hierarchy.Parent = entt::null;
		RootEntities.push_back(Child);
	}

	entt::entity FScene::FindByName(const std::string& Name) const
	{
		// A view rather than a map: glTF node names are not unique, so an index keyed by name would have to
		// pick a winner and would then disagree with the hierarchy panel about which entity that name means.
		for (const auto [Entity, NameComponent] : Registry.view<const FNameComponent>().each())
		{
			if (NameComponent.Name == Name)
			{
				return Entity;
			}
		}
		return entt::null;
	}

	void FScene::UpdateTransforms()
	{
		TraversalStack.clear();

		// Seeded with the roots and walked depth first, so a parent's world matrix is always final before
		// any of its children read it.
		for (const entt::entity Root : RootEntities)
		{
			if (Registry.valid(Root))
			{
				TraversalStack.push_back(Root);
			}
		}

		while (!TraversalStack.empty())
		{
			const entt::entity Entity = TraversalStack.back();
			TraversalStack.pop_back();

			if (!Registry.valid(Entity))
			{
				continue;
			}

			const FTransformComponent& Local = Registry.get<FTransformComponent>(Entity);
			const FHierarchyComponent& Hierarchy = Registry.get<FHierarchyComponent>(Entity);
			FWorldTransformComponent& World = Registry.get<FWorldTransformComponent>(Entity);

			const FMatrix4x4 LocalMatrix = Local.ToMatrix();
			if (Hierarchy.Parent != entt::null && Registry.valid(Hierarchy.Parent))
			{
				// Parent first, then local: composing the other way round would apply the child's rotation to
				// the parent's translation and make children orbit the origin.
				World.Matrix = Multiply(Registry.get<FWorldTransformComponent>(Hierarchy.Parent).Matrix, LocalMatrix);
			}
			else
			{
				World.Matrix = LocalMatrix;
			}

			// Inverse transpose, so normals stay perpendicular to the surface when the scale is non uniform.
			// With a uniform scale this reduces to the world matrix, but the general form costs the same here
			// and removes a special case.
			World.NormalMatrix = World.Matrix.GetInverse().GetTransposed();

			for (const entt::entity Child : Hierarchy.Children)
			{
				TraversalStack.push_back(Child);
			}
		}
	}

	void FScene::SetAssets(std::vector<FMeshData> InMeshes, std::vector<FMaterialData> InMaterials, std::vector<FImageData> InImages)
	{
		Meshes = std::move(InMeshes);
		Materials = std::move(InMaterials);
		Images = std::move(InImages);
		++AssetRevision;
	}

	FSceneStats FScene::GetStats() const
	{
		FSceneStats Stats;
		Stats.EntityCount = static_cast<uint32>(Registry.view<const FNameComponent>().size());
		Stats.MaterialCount = static_cast<uint32>(Materials.size());

		for (const FImageData& Image : Images)
		{
			if (Image.IsValid())
			{
				++Stats.TextureCount;
			}
		}

		// Counted per entity rather than per mesh, so a mesh instanced several times contributes each time.
		// That is what a viewer needs to know: how much is actually being drawn.
		for (const auto [Entity, MeshRenderer] : Registry.view<const FMeshRendererComponent>().each())
		{
			++Stats.MeshEntityCount;
			if (MeshRenderer.MeshIndex < Meshes.size())
			{
				Stats.TriangleCount += Meshes[MeshRenderer.MeshIndex].GetTriangleCount();
			}
		}

		return Stats;
	}

	FBoundingBox FScene::ComputeWorldBounds() const
	{
		FBoundingBox Result;

		for (const auto [Entity, MeshRenderer, World] :
		     Registry.view<const FMeshRendererComponent, const FWorldTransformComponent>().each())
		{
			if (!MeshRenderer.bVisible || MeshRenderer.MeshIndex >= Meshes.size())
			{
				continue;
			}

			const FBoundingBox& Local = Meshes[MeshRenderer.MeshIndex].Bounds;
			if (!Local.bValid)
			{
				continue;
			}

			// All eight corners, not just the two extremes: a rotated box's transformed min and max are not
			// the min and max of the transformed box, and using them would under-report the bounds and put
			// the camera too close.
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				const FVector3 Point{
					(Corner & 1) != 0 ? Local.Max.X : Local.Min.X,
					(Corner & 2) != 0 ? Local.Max.Y : Local.Min.Y,
					(Corner & 4) != 0 ? Local.Max.Z : Local.Min.Z,
				};
				Result.Include(World.Matrix.TransformPosition(Point));
			}
		}

		return Result;
	}

	void FScene::Clear()
	{
		Registry.clear();
		RootEntities.clear();
		Meshes.clear();
		Materials.clear();
		Images.clear();
		SourcePath.clear();
		TraversalStack.clear();
		// Bumped so the GPU side discards what it uploaded for the previous scene.
		++AssetRevision;
	}
} // namespace Lime
