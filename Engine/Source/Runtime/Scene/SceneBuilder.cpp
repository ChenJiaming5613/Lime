#include "Scene/SceneBuilder.h"

#include "Core/Logging/LogManager.h"

#include "Scene/Scene.h"

#include <vector>

namespace Lime
{
	namespace
	{
		// A node with no authored name would leave the hierarchy panel showing a column of blanks, which is
		// unusable for navigating a large model. The index keeps every row distinguishable.
		std::string MakeNodeName(const FSceneNodeData& Node, uint32 Index)
		{
			return Node.Name.empty() ? "Node " + std::to_string(Index) : Node.Name;
		}
	} // namespace

	FSceneBuildResult BuildScene(FScene& Scene, const FGltfSceneData& Data)
	{
		FSceneBuildResult Result;

		Scene.Clear();
		Scene.SetSourcePath(Data.SourcePath);
		Scene.SetAssets(Data.Meshes, Data.Materials, Data.Images);

		if (Data.Nodes.empty())
		{
			return Result;
		}

		entt::registry& Registry = Scene.GetRegistry();

		// Entities are created for every node first, so that parenting can be applied afterwards without
		// caring whether a child appears before or after its parent in the array.
		std::vector<entt::entity> Entities;
		Entities.reserve(Data.Nodes.size());

		for (uint32 Index = 0; Index < static_cast<uint32>(Data.Nodes.size()); ++Index)
		{
			const FSceneNodeData& Node = Data.Nodes[Index];
			const entt::entity Entity = Scene.CreateEntity(MakeNodeName(Node, Index));

			FTransformComponent& Transform = Registry.get<FTransformComponent>(Entity);
			Transform.Position = Node.Translation;
			Transform.Rotation = Node.Rotation;
			Transform.Scale = Node.Scale;

			if (Node.MeshIndex >= 0)
			{
				Registry.emplace<FMeshRendererComponent>(Entity, static_cast<uint32>(Node.MeshIndex), true);
				++Result.MeshEntityCount;
			}

			Entities.push_back(Entity);
		}

		for (uint32 Index = 0; Index < static_cast<uint32>(Data.Nodes.size()); ++Index)
		{
			for (const uint32 Child : Data.Nodes[Index].Children)
			{
				if (Child < Entities.size())
				{
					Scene.SetParent(Entities[Child], Entities[Index]);
				}
			}
		}

		// glTF has no concept of a light in its base specification, so shading always needs one supplied.
		// Aimed down and slightly to the side rather than straight down, which leaves the top faces lit and
		// the sides shaded so that shape reads at a glance.
		const entt::entity Light = Scene.CreateEntity("Directional Light");
		FDirectionalLightComponent& LightComponent = Registry.emplace<FDirectionalLightComponent>(Light);
		LightComponent.Direction = FVector3{ -0.4f, -0.8f, 0.45f }.GetNormalized();
		Result.bAddedDefaultLight = true;

		Result.EntityCount = static_cast<uint32>(Registry.view<const FNodeComponent>().size());

		// Run once here so that world matrices are valid before anything reads them, in particular the
		// bounds query used to frame the camera.
		Scene.UpdateTransforms();

		LIME_LOG_INFO(LIME_LOG_CATEGORY_SCENE, "Scene built: {} entities, {} with meshes, {} triangles", Result.EntityCount,
		              Result.MeshEntityCount, Scene.GetStats().TriangleCount);

		return Result;
	}
} // namespace Lime
