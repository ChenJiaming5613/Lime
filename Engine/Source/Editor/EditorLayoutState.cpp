#include "Editor/EditorLayoutState.h"

#include "Core/Logging/LogManager.h"
#include "Editor/Panels/EditorPanel.h"
#include "Platform/PlatformPaths.h"

#include <algorithm>
#include <utility>

namespace Lime
{
	namespace
	{
		constexpr const char* LayoutFileName = "EditorLayout.json";
		constexpr const char* LogContext = "EditorLayout.json";
	} // namespace

	std::filesystem::path FEditorLayoutState::ResolvePath()
	{
		// The same directory the ImGui ini goes to, since the two are one layout in two files. No source
		// tree fallback: unlike EditorSettings.json this is generated state, so there is nothing a project
		// would author by hand and nothing to deploy from a build.
		return FPlatformPaths::GetSavedDirectory() / LayoutFileName;
	}

	uint32 FEditorLayoutState::ClampViewportCount(uint32 Value)
	{
		return std::clamp(Value, MinViewportCount, MaxViewportCount);
	}

	bool FEditorLayoutState::LoadFromFile(const std::filesystem::path& Path)
	{
		// Absence is the normal first run, so it is checked before LoadFromFile can warn about a file it
		// could not read.
		if (!std::filesystem::exists(Path))
		{
			return false;
		}

		FJson Root;
		if (!FJsonUtils::LoadFromFile(Path, Root))
		{
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_EDITOR, "Starting from the default layout because '{}' could not be read", Path.string());
			return false;
		}

		const int32 Version = FJsonUtils::ReadOr<int32>(Root, "version", 1, LogContext);
		if (Version != 1)
		{
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_EDITOR, "{}: unsupported version {}; parsing as version 1", LogContext, Version);
		}

		FJsonUtils::WarnUnknownKeys(Root, { "version", "viewportCount", "panels" }, {}, LogContext);

		const uint32 RequestedViewports = FJsonUtils::ReadOr<uint32>(Root, "viewportCount", ViewportCount, LogContext);
		ViewportCount = ClampViewportCount(RequestedViewports);
		if (ViewportCount != RequestedViewports)
		{
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_EDITOR, "{}: viewportCount {} is outside {}..{}; using {}", LogContext, RequestedViewports,
			   MinViewportCount, MaxViewportCount, ViewportCount);
		}

		// An object of name to bool rather than a list of open panels, so "closed" is recorded explicitly.
		// A list could not distinguish a panel that was closed from one that did not exist when the file
		// was written, and those differ: the first must stay closed, the second must follow its default.
		if (const FJson* Panels = FJsonUtils::Find(Root, "panels"); Panels != nullptr)
		{
			if (!Panels->is_object())
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_EDITOR, "{}: 'panels' must be an object of name to bool; ignoring it", LogContext);
			}
			else
			{
				for (const auto& [Name, Value] : Panels->items())
				{
					if (!Value.is_boolean())
					{
						LIME_LOG_WARNING(LIME_LOG_CATEGORY_EDITOR, "{}: panel '{}' must be a bool; leaving it at its default", LogContext,
						   Name);
						continue;
					}
					PanelVisibility[Name] = Value.get<bool>();
				}
			}
		}

		return true;
	}

	bool FEditorLayoutState::SaveToFile(const std::filesystem::path& Path) const
	{
		// Written from scratch rather than loaded and modified, which is the opposite of how
		// EditorSettings.json is saved. Nothing hand written lives here, so there are no comments or
		// unknown keys to preserve, and a stale panel entry left behind by a merge would outlive the panel.
		FJson Root = FJson::object();
		Root["version"] = 1;
		Root["viewportCount"] = ViewportCount;

		FJson Panels = FJson::object();
		for (const auto& [Name, bVisible] : PanelVisibility)
		{
			Panels[Name] = bVisible;
		}
		Root["panels"] = std::move(Panels);

		if (!FJsonUtils::SaveToFile(Path, Root))
		{
			return false;
		}

		LIME_LOG_TRACE(LIME_LOG_CATEGORY_EDITOR, "Editor layout saved to '{}'", Path.string());
		return true;
	}

	bool FEditorLayoutState::IsPanelVisible(const IEditorPanel& Panel) const
	{
		const auto Found = PanelVisibility.find(Panel.GetName());
		// The panel's own default when the file says nothing, so a panel added to the engine or by a
		// project appears the first time without anyone editing the saved layout.
		return Found != PanelVisibility.end() ? Found->second : Panel.IsVisibleByDefault();
	}

	bool FEditorLayoutState::operator==(const FEditorLayoutState& Other) const
	{
		// unordered_map compares by contents rather than by order, which is what is wanted here: the panel
		// list is assembled from registrations and its order carries no meaning.
		return ViewportCount == Other.ViewportCount && PanelVisibility == Other.PanelVisibility;
	}
} // namespace Lime
