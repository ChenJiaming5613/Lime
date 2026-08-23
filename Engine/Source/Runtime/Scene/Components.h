// Scene components.
//
// Plain data, no behaviour. Systems operate on them; nothing here knows how to draw itself or how to
// read a file. That is what keeps a component reusable by the renderer, the editor panels and the
// automation commands without any of them depending on each other.

#pragma once

#include "Core/CoreTypes.h"
#include "Core/Math/Matrix.h"
#include "Core/Math/Quaternion.h"
#include "Core/Math/Vector.h"

#include <entt/entity/registry.hpp>
#include <string>
#include <vector>

namespace Lime
{
	struct FNameComponent
	{
		std::string Name;
	};

	// Local transform, as authored. Stored as separate translation, rotation and scale rather than a
	// matrix because that is how glTF describes a node, and because it is the form the editor can present
	// as editable values.
	struct FTransformComponent
	{
		FVector3 Translation{ 0.0f, 0.0f, 0.0f };
		FQuat Rotation = FQuat::Identity();
		FVector3 Scale{ 1.0f, 1.0f, 1.0f };

		FMatrix4x4 ToMatrix() const { return MakeTransform(Translation, Rotation, Scale); }
	};

	// World transform, derived. Kept separate from the local one and rebuilt by FScene::UpdateTransforms
	// rather than maintained alongside it: two independently mutable copies of the same information
	// inevitably drift apart, and the bug shows up as a child that stops following its parent.
	struct FWorldTransformComponent
	{
		FMatrix4x4 Matrix = FMatrix4x4::Identity();
		// Inverse transpose of the upper 3x3, needed so that normals stay perpendicular to the surface
		// under a non uniform scale. Cached here because it costs a matrix inverse per entity and the
		// value is needed once per draw.
		FMatrix4x4 NormalMatrix = FMatrix4x4::Identity();

		FVector3 GetWorldPosition() const { return { Matrix.M[0][3], Matrix.M[1][3], Matrix.M[2][3] }; }
	};

	// Parent and children, both directions. The child list is what lets the hierarchy panel draw a tree
	// in one pass, and it preserves the order the nodes were authored in so the panel is stable between
	// runs. Storing only the parent would force the panel to scan every entity per node.
	struct FHierarchyComponent
	{
		entt::entity Parent = entt::null;
		std::vector<entt::entity> Children;
	};

	// References a mesh in the scene's mesh array by index rather than holding the data, so that the same
	// mesh drawn at several places is uploaded once.
	struct FMeshRendererComponent
	{
		uint32 MeshIndex = 0;
		bool bVisible = true;
	};

	// Marks the entity a camera follows. The camera itself is not a component: it is an ICamera the
	// renderer holds, and duplicating its state here would create a second source of truth.
	struct FCameraComponent
	{
		float FieldOfViewRadians = 0.0f;
		bool bIsActive = true;
	};

	struct FDirectionalLightComponent
	{
		// Direction the light travels, pointing away from the source. Normalized when the light is built.
		FVector3 Direction{ 0.0f, -1.0f, 0.0f };
		FVector3 Color{ 1.0f, 1.0f, 1.0f };
		float Intensity = 1.0f;
	};
} // namespace Lime
