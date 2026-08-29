#include "Editor/Panels/SceneHierarchyPanel.h"

#include "Editor/EditorSelection.h"

#include "Scene/Scene.h"

#include <imgui.h>

#include <algorithm>

namespace Lime
{
	namespace
	{
		// Case insensitive substring test, so filtering does not require matching the authored casing of a
		// glTF node name.
		bool ContainsCaseInsensitive(const std::string& Haystack, const std::string& Needle)
		{
			if (Needle.empty())
			{
				return true;
			}

			const auto Found =
			    std::search(Haystack.begin(), Haystack.end(), Needle.begin(), Needle.end(), [](char A, char B)
				            { return std::tolower(static_cast<unsigned char>(A)) == std::tolower(static_cast<unsigned char>(B)); });
			return Found != Haystack.end();
		}
	} // namespace

	bool FSceneHierarchyPanel::MatchesFilter(const FScene& Scene, entt::entity Entity) const
	{
		if (Filter.empty())
		{
			return true;
		}

		const entt::registry& Registry = Scene.GetRegistry();
		if (!Registry.valid(Entity))
		{
			return false;
		}

		if (const FNodeComponent* NodeComponent = Registry.try_get<FNodeComponent>(Entity))
		{
			if (ContainsCaseInsensitive(NodeComponent->Name, Filter))
			{
				return true;
			}
		}

		// A parent is kept when a descendant matches, otherwise the match would be unreachable: it would sit
		// inside a branch that the filter itself had hidden.
		if (const FNodeComponent* NodeComponent = Registry.try_get<FNodeComponent>(Entity))
		{
			for (const entt::entity Child : NodeComponent->Children)
			{
				if (MatchesFilter(Scene, Child))
				{
					return true;
				}
			}
		}

		return false;
	}

	void FSceneHierarchyPanel::DrawEntity(const FEditorContext& Context, FScene& Scene, entt::entity Entity)
	{
		entt::registry& Registry = Scene.GetRegistry();
		if (!Registry.valid(Entity) || !MatchesFilter(Scene, Entity))
		{
			return;
		}

		FNodeComponent& NodeComponent = Registry.get<FNodeComponent>(Entity);

		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (NodeComponent.Children.empty())
		{
			// Leaf gets no arrow, which is what makes the indentation read as structure rather than as
			// decoration.
			Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}
		if (Context.Selection != nullptr && Context.Selection->Is(Entity))
		{
			Flags |= ImGuiTreeNodeFlags_Selected;
		}
		if (!Filter.empty())
		{
			// Expanded while filtering, so a match deep in the tree is visible without manual clicking.
			Flags |= ImGuiTreeNodeFlags_DefaultOpen;
		}

		const std::string Label = NodeComponent.Name;

		// The entity id keys the node, so two nodes sharing a name stay independent; glTF names are not
		// unique. PushID with the integer overload rather than casting it to a void*, which ImGui also
		// accepts but which costs an int-to-pointer round trip.
		ImGui::PushID(static_cast<int>(entt::to_integral(Entity)));
		if (ImGui::Checkbox("", &NodeComponent.Enabled))
		{
			Scene.SetEntityEnabled(Entity, NodeComponent.Enabled);
		}
		ImGui::SameLine();
		const bool bOpen = ImGui::TreeNodeEx(Label.c_str(), Flags);

		// Checked before recursing, so clicking a parent selects the parent rather than a child.
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && Context.Selection != nullptr)
		{
			Context.Selection->Set(Entity);
		}

		if (bOpen && !NodeComponent.Children.empty())
		{
			// Copied because selecting or filtering can invalidate the registry's storage while iterating.
			const std::vector<entt::entity> Children = NodeComponent.Children;
			for (const entt::entity Child : Children)
			{
				DrawEntity(Context, Scene, Child);
			}
			ImGui::TreePop();
		}

		// After TreePop, so the id stack unwinds in the order ImGui expects.
		ImGui::PopID();
	}

	void FSceneHierarchyPanel::OnDrawUI(const FEditorContext& Context)
	{
		if (!ImGui::Begin(GetName(), GetVisiblePtr()))
		{
			ImGui::End();
			return;
		}

		FScene* Scene = Context.GetScene();
		if (Scene == nullptr)
		{
			ImGui::TextDisabled("No scene loaded");
			ImGui::Spacing();
			ImGui::TextWrapped("Set scene.gltf in ProjectSettings.json to load a model.");
			ImGui::End();
			return;
		}

		// A fixed buffer rather than binding the std::string directly: ImGui's text input needs a mutable
		// char array, and copying in and out each frame keeps the widget stateless.
		char FilterBuffer[128] = {};
		const size_t CopyLength = std::min(Filter.size(), sizeof(FilterBuffer) - 1);
		std::copy_n(Filter.begin(), CopyLength, FilterBuffer);

		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::InputTextWithHint("##Filter", "Filter by name", FilterBuffer, sizeof(FilterBuffer)))
		{
			Filter = FilterBuffer;
		}

		ImGui::Separator();

		if (Scene->GetRootEntities().empty())
		{
			ImGui::TextDisabled("Scene is empty");
			ImGui::End();
			return;
		}

		// Copied for the same reason as the child list: drawing can change the selection, and a view held
		// across that would be operating on stale storage.
		const std::vector<entt::entity> Roots = Scene->GetRootEntities();
		for (const entt::entity Root : Roots)
		{
			DrawEntity(Context, *Scene, Root);
		}

		ImGui::End();
	}
} // namespace Lime
