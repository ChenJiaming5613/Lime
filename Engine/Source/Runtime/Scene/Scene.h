// Scene graph built on an EnTT registry.
//
// Owns the entities, the hierarchy and the mesh data they reference. Deliberately holds no GPU objects:
// uploading is a separate concern in FSceneGpuResources, which keeps the whole of this class testable
// without a device and lets the scene outlive a device reset.
//
// The registry is exposed rather than wrapped behind accessors for every component. Wrapping it would
// mean adding a method here for each new component, which defeats the point of using an ECS.

#pragma once

#include "Asset/AssetTypes.h"
#include "Scene/Components.h"

#include <entt/entity/registry.hpp>
#include <string>
#include <vector>

namespace Lime
{
	struct FSceneStats
	{
		uint32 EntityCount = 0;
		uint32 MeshEntityCount = 0;
		uint32 TriangleCount = 0;
		uint32 MaterialCount = 0;
		uint32 TextureCount = 0;
	};

	class FScene
	{
	public:
		FScene() = default;

		LIME_NON_COPYABLE(FScene);
		LIME_NON_MOVABLE(FScene);

		entt::registry& GetRegistry() { return Registry; }
		const entt::registry& GetRegistry() const { return Registry; }

		// Creates an entity with a name and an identity transform. Every entity gets a hierarchy component
		// so that walking the tree never has to test for its presence.
		entt::entity CreateEntity(const std::string& Name);

		// Makes Child a child of Parent, detaching it from any previous parent first.
		//
		// A cycle would make the transform walk run forever, so an attempt to parent an entity to its own
		// descendant is rejected and reported.
		bool SetParent(entt::entity Child, entt::entity Parent);
		void DetachFromParent(entt::entity Child);

		const std::vector<entt::entity>& GetRootEntities() const { return RootEntities; }

		// First entity with this name, or entt::null. Linear, intended for tooling and tests rather than
		// per frame use; glTF names are not unique so an index would be misleading.
		entt::entity FindByName(const std::string& Name) const;

		// Rebuilds every world matrix from the local transforms, parents before children.
		//
		// Iterative with an explicit stack rather than recursive: the Khronos sample assets contain node
		// trees deep enough that recursion is a real stack overflow risk, and the depth is data driven so
		// it cannot be bounded at compile time.
		void UpdateTransforms();

		// Meshes referenced by FMeshRendererComponent::MeshIndex. Owned by the scene so that several
		// entities can share one mesh and it is uploaded once.
		const std::vector<FMeshData>& GetMeshes() const { return Meshes; }
		const std::vector<FMaterialData>& GetMaterials() const { return Materials; }
		const std::vector<FImageData>& GetImages() const { return Images; }

		void SetAssets(std::vector<FMeshData> InMeshes, std::vector<FMaterialData> InMaterials, std::vector<FImageData> InImages);

		// Path the scene was loaded from, for display and for automation to assert against.
		const std::string& GetSourcePath() const { return SourcePath; }
		void SetSourcePath(std::string InPath) { SourcePath = std::move(InPath); }

		FSceneStats GetStats() const;

		// World space bounds of every visible mesh entity, used to frame the camera. Requires
		// UpdateTransforms to have run, since it transforms local bounds by the world matrix.
		FBoundingBox ComputeWorldBounds() const;

		void Clear();
		bool IsEmpty() const { return RootEntities.empty(); }

		// Incremented whenever the asset arrays change, so the GPU side can tell whether its upload is
		// still current instead of re-uploading every frame.
		uint32 GetAssetRevision() const { return AssetRevision; }

	private:
		// True when Candidate is Root or lies below it. Used to reject parenting cycles.
		bool IsDescendantOf(entt::entity Candidate, entt::entity Root) const;

		entt::registry Registry;
		std::vector<entt::entity> RootEntities;

		std::vector<FMeshData> Meshes;
		std::vector<FMaterialData> Materials;
		std::vector<FImageData> Images;

		std::string SourcePath;
		uint32 AssetRevision = 0;

		// Reused across UpdateTransforms calls so the per frame walk does not allocate.
		mutable std::vector<entt::entity> TraversalStack;
	};
} // namespace Lime
