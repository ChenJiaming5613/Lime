// Automation commands for the scene.
//
// Exists so a test can assert on what was loaded and put the camera in a known place before capturing.
// That last part is what makes a screenshot comparison meaningful: without a fixed camera, two captures
// of the same scene differ because the view drifted.

#include "Renderer/Renderer.h"

#include "Automation/AutomationCommandRegistry.h"
#include "Camera/Camera.h"
#include "Scene/Scene.h"

#if LIME_WITH_EDITOR
#include "Editor/EditorLayer.h"
#endif

#include <spdlog/fmt/fmt.h>

#include <vector>

namespace Lime
{
	namespace
	{
		// Resolves the scene. Fails the invocation and returns nullptr when there is none, so callers can
		// return straight away.
		FScene* ResolveScene(FAutomationInvocation& Invocation)
		{
			FRenderer* Renderer = Invocation.GetContext().Renderer;
			if (Renderer == nullptr)
			{
				Invocation.Fail("No renderer");
				return nullptr;
			}

			FScene* Scene = Renderer->GetScene();
			if (Scene == nullptr)
			{
				Invocation.Fail("No scene is loaded");
				return nullptr;
			}
			return Scene;
		}

		FJson ToJson(const FVector3& Vector)
		{
			return FJson::array({ Vector.X, Vector.Y, Vector.Z });
		}

		// Reads a three element numeric array. Absent leaves OutValue alone, which lets a caller set only
		// the fields it cares about.
		bool TryReadVector3(const FJson& Params, const char* Key, FVector3& OutValue, std::string& OutError)
		{
			const FJson* Node = FJsonUtils::Find(Params, Key);
			if (Node == nullptr)
			{
				return true;
			}

			if (!Node->is_array() || Node->size() != 3)
			{
				OutError = fmt::format("'{}' must be an array of three numbers", Key);
				return false;
			}

			for (const FJson& Element : *Node)
			{
				if (!Element.is_number())
				{
					OutError = fmt::format("'{}' must contain only numbers", Key);
					return false;
				}
			}

			OutValue = { (*Node)[0].get<float>(), (*Node)[1].get<float>(), (*Node)[2].get<float>() };
			return true;
		}

		// One node of the tree reported by scene.list. Children are nested rather than flattened with a
		// depth field, so a client can walk the structure without reconstructing it.
		FJson DescribeEntity(const FScene& Scene, entt::entity Entity)
		{
			const entt::registry& Registry = Scene.GetRegistry();

			FJson Node = FJson::object();
			Node["id"] = entt::to_integral(Entity);

			if (const FNodeComponent* NodeComponent = Registry.try_get<FNodeComponent>(Entity))
			{
				Node["name"] = NodeComponent->Name;
			}

			if (const FMeshRendererComponent* MeshRenderer = Registry.try_get<FMeshRendererComponent>(Entity))
			{
				Node["mesh"] = MeshRenderer->MeshIndex;
				Node["visible"] = MeshRenderer->bVisible;
				if (MeshRenderer->MeshIndex < Scene.GetMeshes().size())
				{
					Node["triangles"] = Scene.GetMeshes()[MeshRenderer->MeshIndex].GetTriangleCount();
				}
			}

			if (Registry.try_get<FDirectionalLightComponent>(Entity) != nullptr)
			{
				Node["light"] = true;
			}

			if (const FTransformComponent* Transform = Registry.try_get<FTransformComponent>(Entity))
			{
				Node["worldPosition"] = ToJson(Transform->GetWorldPosition());
			}

			if (const FNodeComponent* NodeComponent = Registry.try_get<FNodeComponent>(Entity))
			{
				if (!NodeComponent->Children.empty())
				{
					FJson Children = FJson::array();
					for (const entt::entity Child : NodeComponent->Children)
					{
						if (Registry.valid(Child))
						{
							Children.push_back(DescribeEntity(Scene, Child));
						}
					}
					Node["children"] = std::move(Children);
				}
			}

			return Node;
		}
	} // namespace

	void RegisterSceneAutomationCommands()
	{
		FAutomationCommandRegistry& Registry = FAutomationCommandRegistry::Get();

		Registry.Register("scene.info", "Reports the loaded scene's path and its entity, mesh and triangle counts",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  FRenderer* Renderer = Invocation.GetContext().Renderer;
			                  if (Renderer == nullptr)
			                  {
				                  Invocation.Fail("No renderer");
				                  return;
			                  }

			                  FJson& Result = Invocation.GetResult();

			                  // Reported before the scene itself, because during a background import there is no scene
			                  // yet and "loaded: false" alone cannot be told apart from a project that has none. A
			                  // script waiting for a large scene needs to know which of the two it is looking at.
			                  const std::function<FJson()>& QuerySceneLoad = Invocation.GetContext().QuerySceneLoad;
			                  Result["loading"] = false;
			                  if (QuerySceneLoad)
			                  {
				                  const FJson Load = QuerySceneLoad();
				                  if (!Load.is_null() && !Load.empty())
				                  {
					                  Result["loading"] = Load.value("loading", false);
					                  Result["load"] = Load;
				                  }
			                  }

			                  // Reports loaded=false rather than failing: "is a scene loaded" is a legitimate question, and
			                  // a project with no scene is a valid configuration.
			                  const FScene* Scene = Renderer->GetScene();
			                  if (Scene == nullptr)
			                  {
				                  Result["loaded"] = false;
				                  return;
			                  }

			                  const FSceneStats Stats = Scene->GetStats();
			                  Result["loaded"] = !Scene->GetSourcePath().empty();
			                  Result["path"] = Scene->GetSourcePath();
			                  Result["entities"] = Stats.EntityCount;
			                  Result["meshEntities"] = Stats.MeshEntityCount;
			                  Result["triangles"] = Stats.TriangleCount;
			                  Result["materials"] = Stats.MaterialCount;
			                  Result["textures"] = Stats.TextureCount;
			                  Result["roots"] = Scene->GetRootEntities().size();
		                  });

		Registry.Register("scene.list", "Returns the scene hierarchy as a tree of named nodes",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  const FScene* Scene = ResolveScene(Invocation);
			                  if (Scene == nullptr)
			                  {
				                  return;
			                  }

			                  FJson Roots = FJson::array();
			                  for (const entt::entity Root : Scene->GetRootEntities())
			                  {
				                  if (Scene->GetRegistry().valid(Root))
				                  {
					                  Roots.push_back(DescribeEntity(*Scene, Root));
				                  }
			                  }

			                  Invocation.GetResult()["roots"] = std::move(Roots);
		                  });

		Registry.Register("scene.camera.get", "Reports the camera position, orientation and projection",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  FRenderer* Renderer = Invocation.GetContext().Renderer;
			                  if (Renderer == nullptr || Renderer->GetCamera() == nullptr)
			                  {
				                  Invocation.Fail("No camera");
				                  return;
			                  }

			                  const ICamera& Camera = *Renderer->GetCamera();
			                  FJson& Result = Invocation.GetResult();
			                  Result["position"] = ToJson(Camera.GetPosition());
			                  Result["forward"] = ToJson(Camera.GetForward());
			                  Result["aspectRatio"] = Camera.GetAspectRatio();
			                  Result["nearPlane"] = Camera.GetNearPlane();
			                  Result["farPlane"] = Camera.GetFarPlane();
			                  Result["projection"] =
			                      Camera.GetProjectionType() == ECameraProjection::Perspective ? "perspective" : "orthographic";
		                  });

		Registry.Register("scene.camera.set",
		                  "Places the camera. Params: position [x,y,z], yaw, pitch (degrees). Omitted fields are left alone",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  FAutomationContext& Context = Invocation.GetContext();
			                  if (!Context.SetCameraPose || Context.Renderer == nullptr || Context.Renderer->GetCamera() == nullptr)
			                  {
				                  Invocation.Fail("No camera");
				                  return;
			                  }

			                  // Seeded from the current pose so a request may set only what it cares about, which is what
			                  // makes "look from here" and "turn to this angle" separate one line calls.
			                  FVector3 Position = Context.Renderer->GetCamera()->GetPosition();
			                  float Yaw = 0.0f;
			                  float Pitch = 0.0f;

			                  std::string Error;
			                  if (!TryReadVector3(Invocation.GetParams(), "position", Position, Error))
			                  {
				                  Invocation.Fail(Error);
				                  return;
			                  }

			                  const auto ReadAngle = [&Invocation](const char* Key, float& OutValue)
			                  {
				                  const FJson* Node = FJsonUtils::Find(Invocation.GetParams(), Key);
				                  if (Node == nullptr)
				                  {
					                  return true;
				                  }
				                  if (!Node->is_number())
				                  {
					                  Invocation.Fail(fmt::format("'{}' must be a number", Key));
					                  return false;
				                  }
				                  OutValue = Node->get<float>();
				                  return true;
			                  };

			                  if (!ReadAngle("yaw", Yaw) || !ReadAngle("pitch", Pitch))
			                  {
				                  return;
			                  }

			                  Context.SetCameraPose(Position, Yaw, Pitch);

			                  // Echoed back so a caller can confirm what was applied; the camera clamps pitch, so the
			                  // requested and effective values can differ.
			                  const ICamera& Camera = *Context.Renderer->GetCamera();
			                  Invocation.GetResult()["position"] = ToJson(Camera.GetPosition());
			                  Invocation.GetResult()["forward"] = ToJson(Camera.GetForward());
		                  });

#if LIME_WITH_EDITOR
		Registry.Register("scene.select", "Selects an entity by name, or clears the selection when name is omitted. Params: name",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  FEditorLayer* Editor = Invocation.GetContext().Editor;
			                  if (Editor == nullptr)
			                  {
				                  Invocation.Fail("The editor is not enabled");
				                  return;
			                  }

			                  const FScene* Scene = ResolveScene(Invocation);
			                  if (Scene == nullptr)
			                  {
				                  return;
			                  }

			                  std::string Name;
			                  std::string Error;
			                  if (!Invocation.TryGetString("name", Name, Error))
			                  {
				                  Invocation.Fail(Error);
				                  return;
			                  }

			                  if (Name.empty())
			                  {
				                  Editor->GetSelection().Clear();
				                  Invocation.GetResult()["selected"] = FJson();
				                  return;
			                  }

			                  const entt::entity Entity = Scene->FindByName(Name);
			                  if (Entity == entt::null)
			                  {
				                  Invocation.Fail(fmt::format("No entity named '{}'; call 'scene.list' for the available ones", Name));
				                  return;
			                  }

			                  Editor->GetSelection().Set(Entity);
			                  Invocation.GetResult()["selected"] = Name;
			                  Invocation.GetResult()["id"] = entt::to_integral(Entity);
		                  });
#endif
	}
} // namespace Lime
