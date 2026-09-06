// Scene graph tests.
//
// The scene holds no GPU objects by design, so all of this runs without a device. Uploading is covered
// separately by the Python suite against a real engine.
//
// The cases target what is easy to get wrong: transform composition order, a deep hierarchy overflowing
// the stack, a parenting cycle hanging the walk, and world bounds under rotation.

#include "Scene/Scene.h"
#include "Scene/SceneBuilder.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

using Catch::Approx;
using namespace Lime;

namespace
{
	// A mesh shaped like a unit cube centred on the origin. Only the bounds matter for these tests, but the
	// vertices are filled in so triangle counts are realistic.
	FMeshData MakeUnitCubeMesh(const std::string& Name)
	{
		FMeshData Mesh;
		Mesh.Name = Name;

		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			FMeshVertex Vertex;
			Vertex.Position = {
				(Corner & 1) != 0 ? 0.5f : -0.5f,
				(Corner & 2) != 0 ? 0.5f : -0.5f,
				(Corner & 4) != 0 ? 0.5f : -0.5f,
			};
			Vertex.Normal = FVector3::UnitY();
			Mesh.Bounds.Include(Vertex.Position);
			Mesh.Vertices.push_back(Vertex);
		}

		// Two triangles, enough to give a non zero triangle count.
		Mesh.Indices = { 0, 1, 2, 1, 3, 2 };

		FMeshSection Section;
		Section.FirstIndex = 0;
		Section.IndexCount = 6;
		Section.MaterialIndex = 0;
		Mesh.Sections.push_back(Section);

		return Mesh;
	}
} // namespace

TEST_CASE("Creating entities", "[Scene]")
{
	FScene Scene;

	SECTION("A new entity is a root with an identity transform")
	{
		const entt::entity Entity = Scene.CreateEntity("First");

		REQUIRE(Scene.GetRootEntities().size() == 1);
		REQUIRE(Scene.GetRootEntities()[0] == Entity);
		REQUIRE(Scene.GetRegistry().get<FNodeComponent>(Entity).Name == "First");
		REQUIRE(IsNearlyEqual(Scene.GetRegistry().get<FTransformComponent>(Entity).Position, FVector3::Zero()));
	}

	SECTION("Every entity has a hierarchy component")
	{
		// Added unconditionally so that walking the tree never has to test for its presence.
		const entt::entity Entity = Scene.CreateEntity("First");
		REQUIRE(Scene.GetRegistry().try_get<FNodeComponent>(Entity) != nullptr);
		// Parenthesised so Catch2 does not decompose the expression: entt::null_t has templated symmetric
		// comparison operators, and the decomposed form makes the overload ambiguous.
		REQUIRE((Scene.GetRegistry().get<FNodeComponent>(Entity).Parent == entt::null));
	}

	SECTION("An empty scene reports as empty")
	{
		REQUIRE(Scene.IsEmpty());
		Scene.CreateEntity("First");
		REQUIRE_FALSE(Scene.IsEmpty());
	}

	SECTION("FindByName locates an entity and reports a miss")
	{
		const entt::entity Entity = Scene.CreateEntity("Target");
		Scene.CreateEntity("Other");

		REQUIRE(Scene.FindByName("Target") == Entity);
		REQUIRE((Scene.FindByName("Missing") == entt::null));
	}
}

TEST_CASE("Parenting", "[Scene]")
{
	FScene Scene;
	const entt::entity Parent = Scene.CreateEntity("Parent");
	const entt::entity Child = Scene.CreateEntity("Child");

	SECTION("A parented entity stops being a root")
	{
		REQUIRE(Scene.SetParent(Child, Parent));
		REQUIRE(Scene.GetRootEntities().size() == 1);
		REQUIRE(Scene.GetRootEntities()[0] == Parent);
		REQUIRE(Scene.GetRegistry().get<FNodeComponent>(Child).Parent == Parent);
		REQUIRE(Scene.GetRegistry().get<FNodeComponent>(Parent).Children.size() == 1);
	}

	SECTION("Detaching makes it a root again")
	{
		REQUIRE(Scene.SetParent(Child, Parent));
		Scene.DetachFromParent(Child);

		REQUIRE(Scene.GetRootEntities().size() == 2);
		REQUIRE((Scene.GetRegistry().get<FNodeComponent>(Child).Parent == entt::null));
		REQUIRE(Scene.GetRegistry().get<FNodeComponent>(Parent).Children.empty());
	}

	SECTION("Reparenting removes the entity from the previous parent")
	{
		// Leaving it in both child lists would make the hierarchy panel show it twice and the transform walk
		// visit it twice.
		const entt::entity Other = Scene.CreateEntity("Other");
		REQUIRE(Scene.SetParent(Child, Parent));
		REQUIRE(Scene.SetParent(Child, Other));

		REQUIRE(Scene.GetRegistry().get<FNodeComponent>(Parent).Children.empty());
		REQUIRE(Scene.GetRegistry().get<FNodeComponent>(Other).Children.size() == 1);
		REQUIRE(Scene.GetRegistry().get<FNodeComponent>(Child).Parent == Other);
	}

	SECTION("A cycle is refused")
	{
		// A cycle would make UpdateTransforms walk forever, so this is a hang rather than a wrong result.
		REQUIRE(Scene.SetParent(Child, Parent));
		REQUIRE_FALSE(Scene.SetParent(Parent, Child));
		REQUIRE((Scene.GetRegistry().get<FNodeComponent>(Parent).Parent == entt::null));
	}

	SECTION("A deeper cycle is refused too")
	{
		const entt::entity Grandchild = Scene.CreateEntity("Grandchild");
		REQUIRE(Scene.SetParent(Child, Parent));
		REQUIRE(Scene.SetParent(Grandchild, Child));

		// Parent is an ancestor of Grandchild, so this would close a loop.
		REQUIRE_FALSE(Scene.SetParent(Parent, Grandchild));
	}

	SECTION("An entity cannot be its own parent")
	{
		REQUIRE_FALSE(Scene.SetParent(Child, Child));
	}

	SECTION("Child order is preserved")
	{
		// The hierarchy panel draws in this order, so it has to be stable between runs.
		const entt::entity First = Scene.CreateEntity("A");
		const entt::entity Second = Scene.CreateEntity("B");
		const entt::entity Third = Scene.CreateEntity("C");

		Scene.SetParent(First, Parent);
		Scene.SetParent(Second, Parent);
		Scene.SetParent(Third, Parent);

		const std::vector<entt::entity>& Children = Scene.GetRegistry().get<FNodeComponent>(Parent).Children;
		REQUIRE(Children.size() == 3);
		REQUIRE(Children[0] == First);
		REQUIRE(Children[1] == Second);
		REQUIRE(Children[2] == Third);
	}
}

TEST_CASE("World transform propagation", "[Scene]")
{
	FScene Scene;

	SECTION("A root's world matrix is its local matrix")
	{
		const entt::entity Entity = Scene.CreateEntity("Root");
		Scene.GetRegistry().get<FTransformComponent>(Entity).Position = { 1.0f, 2.0f, 3.0f };
		Scene.UpdateTransforms();

		REQUIRE(IsNearlyEqual(Scene.GetRegistry().get<FTransformComponent>(Entity).GetWorldPosition(), FVector3{ 1.0f, 2.0f, 3.0f }));
	}

	SECTION("A child follows its parent's translation")
	{
		const entt::entity Parent = Scene.CreateEntity("Parent");
		const entt::entity Child = Scene.CreateEntity("Child");
		Scene.SetParent(Child, Parent);

		Scene.GetRegistry().get<FTransformComponent>(Parent).Position = { 10.0f, 0.0f, 0.0f };
		Scene.GetRegistry().get<FTransformComponent>(Child).Position = { 0.0f, 5.0f, 0.0f };
		Scene.UpdateTransforms();

		REQUIRE(IsNearlyEqual(Scene.GetRegistry().get<FTransformComponent>(Child).GetWorldPosition(), FVector3{ 10.0f, 5.0f, 0.0f },
		                      1.0e-5f));
	}

	SECTION("A parent's rotation moves the child around it")
	{
		// Composing parent then local, rather than the other way round, is what makes this work. The reversed
		// order would make children orbit the world origin instead of their parent.
		const entt::entity Parent = Scene.CreateEntity("Parent");
		const entt::entity Child = Scene.CreateEntity("Child");
		Scene.SetParent(Child, Parent);

		// A quarter turn about Y maps the child's +X offset onto -Z.
		Scene.GetRegistry().get<FTransformComponent>(Parent).Rotation = FQuat::FromAxisAngle(FVector3::UnitY(), HalfPi);
		Scene.GetRegistry().get<FTransformComponent>(Child).Position = { 1.0f, 0.0f, 0.0f };
		Scene.UpdateTransforms();

		REQUIRE(IsNearlyEqual(Scene.GetRegistry().get<FTransformComponent>(Child).GetWorldPosition(), FVector3{ 0.0f, 0.0f, -1.0f },
		                      1.0e-4f));
	}

	SECTION("A parent's scale scales the child's offset")
	{
		const entt::entity Parent = Scene.CreateEntity("Parent");
		const entt::entity Child = Scene.CreateEntity("Child");
		Scene.SetParent(Child, Parent);

		Scene.GetRegistry().get<FTransformComponent>(Parent).Scale = { 2.0f, 2.0f, 2.0f };
		Scene.GetRegistry().get<FTransformComponent>(Child).Position = { 3.0f, 0.0f, 0.0f };
		Scene.UpdateTransforms();

		REQUIRE(Scene.GetRegistry().get<FTransformComponent>(Child).GetWorldPosition().X == Approx(6.0f).margin(1.0e-4f));
	}

	SECTION("Transforms accumulate across three levels")
	{
		const entt::entity A = Scene.CreateEntity("A");
		const entt::entity B = Scene.CreateEntity("B");
		const entt::entity C = Scene.CreateEntity("C");
		Scene.SetParent(B, A);
		Scene.SetParent(C, B);

		Scene.GetRegistry().get<FTransformComponent>(A).Position = { 1.0f, 0.0f, 0.0f };
		Scene.GetRegistry().get<FTransformComponent>(B).Position = { 0.0f, 2.0f, 0.0f };
		Scene.GetRegistry().get<FTransformComponent>(C).Position = { 0.0f, 0.0f, 3.0f };
		Scene.UpdateTransforms();

		REQUIRE(
		    IsNearlyEqual(Scene.GetRegistry().get<FTransformComponent>(C).GetWorldPosition(), FVector3{ 1.0f, 2.0f, 3.0f }, 1.0e-5f));
	}

	SECTION("A deep hierarchy does not overflow the stack")
	{
		// The Khronos sample assets contain node trees deep enough that a recursive walk is a real risk, and
		// the depth is data driven so it cannot be bounded at compile time.
		//
		// 4000 levels is well past what recursion survives on a default 1 MB stack, while keeping the build
		// cost reasonable: SetParent verifies there is no cycle by walking to the root, so constructing the
		// chain is inherently quadratic and a much larger depth would dominate the test run.
		constexpr int32 Depth = 4000;

		entt::entity Previous = Scene.CreateEntity("Level0");
		Scene.GetRegistry().get<FTransformComponent>(Previous).Position = { 1.0f, 0.0f, 0.0f };

		for (int32 Level = 1; Level < Depth; ++Level)
		{
			const entt::entity Current = Scene.CreateEntity("Level" + std::to_string(Level));
			Scene.GetRegistry().get<FTransformComponent>(Current).Position = { 1.0f, 0.0f, 0.0f };
			Scene.SetParent(Current, Previous);
			Previous = Current;
		}

		Scene.UpdateTransforms();

		// Each level adds one unit, so the deepest node ends up at the depth.
		REQUIRE(Scene.GetRegistry().get<FTransformComponent>(Previous).GetWorldPosition().X ==
		        Approx(static_cast<float>(Depth)).epsilon(0.01f));
	}

	SECTION("The normal matrix keeps normals perpendicular under a non uniform scale")
	{
		// With a non uniform scale the world matrix tilts normals away from the surface. The inverse
		// transpose is what corrects that, and getting it wrong shows up as visibly wrong lighting.
		const entt::entity Entity = Scene.CreateEntity("Squashed");
		Scene.GetRegistry().get<FTransformComponent>(Entity).Scale = { 1.0f, 0.25f, 1.0f };
		Scene.UpdateTransforms();

		const FTransformComponent& World = Scene.GetRegistry().get<FTransformComponent>(Entity);

		// A surface in the XY plane has a tangent along X and a normal along Z before scaling. After a squash
		// in Y, the transformed tangent and the corrected normal must still be perpendicular.
		const FVector3 Tangent = World.LocalToWorldMatrix.TransformDirection({ 0.0f, 1.0f, 0.0f });
		const FVector3 Normal = World.NormalMatrix.TransformDirection({ 0.0f, 0.0f, 1.0f }).GetNormalized();

		REQUIRE(Dot(Tangent.GetNormalized(), Normal) == Approx(0.0f).margin(1.0e-4f));
	}

	SECTION("Updating twice gives the same result")
	{
		// UpdateTransforms runs every frame, so it has to be idempotent for an unchanged scene.
		const entt::entity Parent = Scene.CreateEntity("Parent");
		const entt::entity Child = Scene.CreateEntity("Child");
		Scene.SetParent(Child, Parent);
		Scene.GetRegistry().get<FTransformComponent>(Parent).Position = { 4.0f, 0.0f, 0.0f };

		Scene.UpdateTransforms();
		const FMatrix4x4 First = Scene.GetRegistry().get<FTransformComponent>(Child).LocalToWorldMatrix;
		Scene.UpdateTransforms();

		REQUIRE(IsNearlyEqual(Scene.GetRegistry().get<FTransformComponent>(Child).LocalToWorldMatrix, First));
	}
}

TEST_CASE("Scene statistics and bounds", "[Scene]")
{
	FScene Scene;
	Scene.SetAssets({ MakeUnitCubeMesh("Cube") }, { FMaterialData{} }, {});

	SECTION("Triangles are counted once per instance, not once per mesh")
	{
		// What a viewer needs to know is how much is actually drawn, so an instanced mesh contributes each
		// time it appears.
		const entt::entity First = Scene.CreateEntity("First");
		const entt::entity Second = Scene.CreateEntity("Second");
		Scene.GetRegistry().emplace<FMeshRendererComponent>(First, 0u, true);
		Scene.GetRegistry().emplace<FMeshRendererComponent>(Second, 0u, true);

		const FSceneStats Stats = Scene.GetStats();
		REQUIRE(Stats.MeshEntityCount == 2);
		REQUIRE(Stats.TriangleCount == 4);
		REQUIRE(Stats.EntityCount == 2);
		REQUIRE(Stats.MaterialCount == 1);
	}

	SECTION("A mesh index out of range does not corrupt the count")
	{
		const entt::entity Entity = Scene.CreateEntity("Broken");
		Scene.GetRegistry().emplace<FMeshRendererComponent>(Entity, 99u, true);

		const FSceneStats Stats = Scene.GetStats();
		REQUIRE(Stats.MeshEntityCount == 1);
		REQUIRE(Stats.TriangleCount == 0);
	}

	SECTION("World bounds follow the entity's transform")
	{
		const entt::entity Entity = Scene.CreateEntity("Cube");
		Scene.GetRegistry().emplace<FMeshRendererComponent>(Entity, 0u, true);
		Scene.GetRegistry().get<FTransformComponent>(Entity).Position = { 10.0f, 0.0f, 0.0f };
		Scene.UpdateTransforms();

		const FBoundingBox Bounds = Scene.ComputeWorldBounds();
		REQUIRE(Bounds.bValid);
		REQUIRE(IsNearlyEqual(Bounds.GetCenter(), FVector3{ 10.0f, 0.0f, 0.0f }, 1.0e-4f));
	}

	SECTION("A rotated box's bounds enclose all eight corners")
	{
		// Transforming only the min and max corners would under-report the bounds of a rotated box, and the
		// camera would then be placed too close and clip the model.
		const entt::entity Entity = Scene.CreateEntity("Cube");
		Scene.GetRegistry().emplace<FMeshRendererComponent>(Entity, 0u, true);
		Scene.GetRegistry().get<FTransformComponent>(Entity).Rotation = FQuat::FromAxisAngle(FVector3::UnitY(), DegreesToRadians(45.0f));
		Scene.UpdateTransforms();

		const FBoundingBox Bounds = Scene.ComputeWorldBounds();
		// A unit cube turned 45 degrees about Y spans sqrt(2) across X and Z.
		REQUIRE(Bounds.GetLongestEdge() == Approx(1.41421f).margin(1.0e-3f));
	}

	SECTION("An invisible entity is excluded from the bounds")
	{
		const entt::entity Entity = Scene.CreateEntity("Hidden");
		Scene.GetRegistry().emplace<FMeshRendererComponent>(Entity, 0u, false);
		Scene.UpdateTransforms();

		REQUIRE_FALSE(Scene.ComputeWorldBounds().bValid);
	}

	SECTION("An empty scene reports invalid bounds rather than a zero box")
	{
		// The camera framing code needs to tell "nothing to frame" apart from "a box at the origin".
		FScene Empty;
		REQUIRE_FALSE(Empty.ComputeWorldBounds().bValid);
	}
}

TEST_CASE("Clearing a scene", "[Scene]")
{
	FScene Scene;
	Scene.SetAssets({ MakeUnitCubeMesh("Cube") }, { FMaterialData{} }, {});
	Scene.SetSourcePath("some/path.gltf");

	const entt::entity Parent = Scene.CreateEntity("Parent");
	Scene.SetParent(Scene.CreateEntity("Child"), Parent);

	const uint32 RevisionBefore = Scene.GetAssetRevision();
	Scene.Clear();

	REQUIRE(Scene.IsEmpty());
	REQUIRE(Scene.GetRootEntities().empty());
	REQUIRE(Scene.GetMeshes().empty());
	REQUIRE(Scene.GetSourcePath().empty());
	REQUIRE(Scene.GetStats().EntityCount == 0);

	SECTION("The asset revision advances so the GPU side discards its upload")
	{
		// Without this the uploaded buffers of the previous scene would be considered current and the new
		// scene would draw the old geometry.
		REQUIRE(Scene.GetAssetRevision() != RevisionBefore);
	}

	SECTION("The scene is reusable after clearing")
	{
		const entt::entity Entity = Scene.CreateEntity("Fresh");
		Scene.UpdateTransforms();
		REQUIRE(Scene.GetRootEntities().size() == 1);
		REQUIRE(Scene.GetRegistry().get<FNodeComponent>(Entity).Name == "Fresh");
	}
}

TEST_CASE("Building a scene from imported data", "[Scene][Builder]")
{
	FGltfSceneData Data;
	Data.SourcePath = "model.gltf";
	Data.Meshes.push_back(MakeUnitCubeMesh("Cube"));

	FSceneNodeData Root;
	Root.Name = "Root";
	Root.Translation = { 1.0f, 0.0f, 0.0f };
	Root.Children = { 1 };
	Data.Nodes.push_back(Root);

	FSceneNodeData Child;
	Child.Name = "Child";
	Child.Translation = { 0.0f, 2.0f, 0.0f };
	Child.MeshIndex = 0;
	Data.Nodes.push_back(Child);

	Data.RootNodes = { 0 };

	FScene Scene;
	const FSceneBuildResult Result = BuildScene(Scene, Data);

	SECTION("Nodes become entities and keep their names")
	{
		REQUIRE((Scene.FindByName("Root") != entt::null));
		REQUIRE((Scene.FindByName("Child") != entt::null));
		REQUIRE(Result.MeshEntityCount == 1);
	}

	SECTION("The hierarchy is rebuilt from the index references")
	{
		const entt::entity RootEntity = Scene.FindByName("Root");
		const entt::entity ChildEntity = Scene.FindByName("Child");
		REQUIRE(Scene.GetRegistry().get<FNodeComponent>(ChildEntity).Parent == RootEntity);
	}

	SECTION("World transforms are valid immediately after building")
	{
		// The camera framing query runs right after this, so the matrices cannot be left stale.
		const entt::entity ChildEntity = Scene.FindByName("Child");
		REQUIRE(IsNearlyEqual(Scene.GetRegistry().get<FTransformComponent>(ChildEntity).GetWorldPosition(),
		                      FVector3{ 1.0f, 2.0f, 0.0f }, 1.0e-5f));
	}

	SECTION("A default light is added, since glTF carries none")
	{
		REQUIRE(Result.bAddedDefaultLight);
		REQUIRE(Scene.GetRegistry().view<const FDirectionalLightComponent>().size() == 1);

		// Normalized, so the shader can use it without normalising per pixel.
		for (const auto [Entity, Light] : Scene.GetRegistry().view<const FDirectionalLightComponent>().each())
		{
			REQUIRE(Light.Direction.Length() == Approx(1.0f).margin(1.0e-4f));
		}
	}

	SECTION("Assets and the source path are carried over")
	{
		REQUIRE(Scene.GetMeshes().size() == 1);
		REQUIRE(Scene.GetSourcePath() == "model.gltf");
	}

	SECTION("Building again replaces the previous contents")
	{
		// Loading a second scene must not leave entities from the first one behind.
		const uint32 EntitiesBefore = Scene.GetStats().EntityCount;
		BuildScene(Scene, Data);
		REQUIRE(Scene.GetStats().EntityCount == EntitiesBefore);
	}

	SECTION("An unnamed node still gets a distinguishable name")
	{
		// A column of blanks would make the hierarchy panel useless for navigating a large model.
		FGltfSceneData Unnamed;
		Unnamed.Nodes.push_back(FSceneNodeData{});
		Unnamed.Nodes.push_back(FSceneNodeData{});
		Unnamed.RootNodes = { 0, 1 };

		FScene Other;
		BuildScene(Other, Unnamed);

		REQUIRE((Other.FindByName("Node 0") != entt::null));
		REQUIRE((Other.FindByName("Node 1") != entt::null));
	}

	SECTION("Empty data yields a scene with nothing in it")
	{
		FScene Other;
		const FSceneBuildResult Empty = BuildScene(Other, FGltfSceneData{});

		REQUIRE(Empty.MeshEntityCount == 0);
		REQUIRE(Other.IsEmpty());
		// No light either: there is nothing to light, and an empty scene should report zero entities.
		REQUIRE(Other.GetStats().EntityCount == 0);
	}
}
