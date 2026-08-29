#include "Editor/Panels/InspectorPanel.h"

#include "Editor/EditorSelection.h"
#include "Renderer/Renderer.h"

#include "Scene/Scene.h"

#include <imgui.h>

namespace Lime
{
	namespace
	{
		// Read only: the inspector reports what the scene contains rather than editing it. Editing a
		// transform would need undo and a way to mark the scene dirty, neither of which exists yet, and a
		// control that silently loses its value on the next load would be worse than a label.
		void DrawSelectedEntity(const FEditorContext& Context, FScene& Scene, entt::entity Entity)
		{
			entt::registry& Registry = Scene.GetRegistry();
			LIME_UNUSED(Context);

			FNodeComponent& NodeComponent = Registry.get<FNodeComponent>(Entity);
			if (ImGui::Checkbox("Entity: ", &NodeComponent.Enabled))
			{
				Scene.SetEntityEnabled(Entity, NodeComponent.Enabled);
			}
			ImGui::SameLine();
			ImGui::Text("%s", NodeComponent.Name.c_str());

			const FTransformComponent& Transform = Registry.get<FTransformComponent>(Entity);
			if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("Position     %.3f, %.3f, %.3f", Transform.Position.X, Transform.Position.Y, Transform.Position.Z);
				ImGui::Text("Rotation     %.3f, %.3f, %.3f, %.3f", Transform.Rotation.X, Transform.Rotation.Y, Transform.Rotation.Z, Transform.Rotation.W);
				ImGui::Text("Scale        %.3f, %.3f, %.3f", Transform.Scale.X, Transform.Scale.Y, Transform.Scale.Z);
			}

			if (const FMeshRendererComponent* MeshRenderer = Registry.try_get<FMeshRendererComponent>(Entity))
			{
				if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Text("Mesh index   %u", MeshRenderer->MeshIndex);
					ImGui::Text("Visible      %s", MeshRenderer->bVisible ? "yes" : "no");

					if (MeshRenderer->MeshIndex < Scene.GetMeshes().size())
					{
						const FMeshData& Mesh = Scene.GetMeshes()[MeshRenderer->MeshIndex];
						ImGui::Text("Vertices     %zu", Mesh.Vertices.size());
						ImGui::Text("Triangles    %u", Mesh.GetTriangleCount());
						ImGui::Text("Sections     %zu", Mesh.Sections.size());
					}
					else
					{
						// Should not happen, but saying so beats showing nothing if it ever does.
						ImGui::TextDisabled("Mesh index is out of range");
					}
				}
			}

			if (const FDirectionalLightComponent* Light = Registry.try_get<FDirectionalLightComponent>(Entity))
			{
				if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Text("Direction    %.3f, %.3f, %.3f", Light->Direction.X, Light->Direction.Y, Light->Direction.Z);
					ImGui::Text("Intensity    %.3f", Light->Intensity);
				}
			}
		}
	} // namespace

	void FInspectorPanel::OnDrawUI(const FEditorContext& Context)
	{
		if (!ImGui::Begin(GetName(), GetVisiblePtr()))
		{
			ImGui::End();
			return;
		}

		if (Context.Renderer == nullptr)
		{
			ImGui::TextDisabled("No renderer");
			ImGui::End();
			return;
		}

		// The selection comes first: when the user has picked something in the hierarchy, that is what they
		// are looking for here, and the pass settings below stay available underneath.
		FScene* Scene = Context.GetScene();
		if (Scene != nullptr && Context.Selection != nullptr && Context.Selection->Has())
		{
			DrawSelectedEntity(Context, *Scene, Context.Selection->Get());
			// ImGui::Spacing();
			// ImGui::SeparatorText("Render Passes");
		}

		// int32 DrawnCount = 0;
		// for (const std::shared_ptr<IRenderPass>& Pass : Context.Renderer->GetPasses())
		// {
		// 	const FReflectedRef Settings = Pass->GetReflectedSettings();
		// 	if (!Settings.IsValid())
		// 	{
		// 		// Passes without reflected settings simply do not appear.
		// 		continue;
		// 	}
		//
		// 	++DrawnCount;
		// 	// Unique ID per pass so two passes with the same header label stay independent.
		// 	ImGui::PushID(Pass.get());
		// 	if (ImGui::CollapsingHeader(Pass->GetName(), ImGuiTreeNodeFlags_DefaultOpen))
		// 	{
		// 		FPropertyDrawer::Draw(Settings);
		// 	}
		// 	ImGui::PopID();
		// }
		//
		// if (DrawnCount == 0)
		// {
		// 	ImGui::TextWrapped("No render pass exposes reflected settings.");
		// 	ImGui::Spacing();
		// 	ImGui::TextDisabled("Add LIME_REFLECT to a settings struct and return it from\nGetReflectedSettings to populate this panel.");
		// }

		ImGui::End();
	}
} // namespace Lime
