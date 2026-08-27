#include "Editor/EditorLayer.h"

#include "Core/Logging/LogManager.h"
#include "Editor/EditorPanelRegistry.h"
#include "Editor/Panels/ConsolePanel.h"
#include "Editor/Panels/EditorSettingsPanel.h"
#include "Editor/Panels/InspectorPanel.h"
#include "Editor/Panels/ProjectSettingsPanel.h"
#include "Editor/Panels/StatsPanel.h"

#if LIME_WITH_NODE_EDITOR
#include "Editor/RenderGraph/RenderGraphPanel.h"
#endif
#include "Platform/PlatformPaths.h"
#include "Platform/Window.h"
#include "RHI/DeviceManager.h"
#include "Renderer/ImGui/EditorUIPass.h"
#include "Renderer/RenderPassRegistry.h"
#include "Renderer/Renderer.h"

#include <imgui_impl_glfw.h>
#include <imgui_internal.h>

#include <algorithm>
#include <filesystem>

namespace Lime
{
	FEditorLayer::~FEditorLayer()
	{
		Shutdown();
	}

	bool FEditorLayer::Initialize(FWindow& Window, FRenderer& InRenderer, const FProjectSettings& ProjectSettings)
	{
		if (bInitialized)
		{
			return true;
		}

		Renderer = &InRenderer;

		// Before the context exists nothing can consume it, and after this point it is never reloaded:
		// the appearance on screen therefore always matches what the session started with.
		Settings.LoadFromFile(FEditorSettings::ResolveSettingsPath());
		Settings.LogSummary();

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		ImGuiIO& IO = ImGui::GetIO();
		IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
		IO.ConfigWindowsMoveFromTitleBarOnly = true;

		// The layout lives next to the executable so a clean build starts from the default docking.
		const std::filesystem::path LayoutPath = FPlatformPaths::GetSavedDirectory() / "EditorLayout.ini";
		bHasSavedLayout = std::filesystem::exists(LayoutPath);
		LayoutFilePath = FPlatformPaths::ToUtf8(LayoutPath);
		IO.IniFilename = LayoutFilePath.c_str();

		ApplyTheme();

		// Only the platform backend comes from ImGui; drawing goes through the NVRHI render pass so
		// D3D12 and Vulkan share one implementation.
		if (!ImGui_ImplGlfw_InitForOther(Window.GetHandle(), true))
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_EDITOR, "ImGui_ImplGlfw_InitForOther failed");
			ImGui::DestroyContext();
			return false;
		}

		CreatePanels(ProjectSettings);

#if LIME_WITH_IMGUI_TEST_ENGINE
		// After the platform backend, because the engine takes over the same ImGuiIO the backend
		// feeds. A failure here is not fatal: the editor is still usable without UI automation.
		if (!TestEngine.Initialize(ImGui::GetCurrentContext()))
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_EDITOR, "UI test engine failed to start; continuing without UI automation");
		}
#endif

		// Registered rather than added directly so it is ordered by priority together with the
		// project passes. Self registration is not usable here: LimeRenderer is a static library, and
		// the linker would be free to discard a translation unit that only registers.
		FRenderPassRegistry::Get().Register("EditorUI", FEditorUIPass::Priority,
		                                    [] { return std::make_shared<FEditorUIPass>(); }, /*bIsBuiltin*/ true);

		// The scene goes to an offscreen target so it can be shown inside the viewport panel. The
		// initial size is the back buffer; the panel corrects it on the first frame it is drawn.
		IDeviceManager& DeviceManager = InRenderer.GetDeviceManager();
		if (!InRenderer.EnableOffscreenRendering(DeviceManager.GetBackBufferWidth(), DeviceManager.GetBackBufferHeight()))
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_EDITOR, "Failed to create the viewport target");
#if LIME_WITH_IMGUI_TEST_ENGINE
			// The coroutine has to be joined before the context it runs against disappears.
			TestEngine.Shutdown();
#endif
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();
#if LIME_WITH_IMGUI_TEST_ENGINE
			TestEngine.DestroyAfterImGuiContext();
#endif
			return false;
		}

		// The texture object changes on resize, so the binding set has to be rebuilt each time.
		InRenderer.SetViewportResizedDelegate([this](FViewportTarget&) { RefreshViewportTexture(); });

		bInitialized = true;
		LIME_LOG_INFO(LIME_LOG_CATEGORY_EDITOR, "Editor initialized with {} panel(s)", Panels.size());
		return true;
	}

	void FEditorLayer::RefreshViewportTexture()
	{
		if (Renderer == nullptr || ViewportPanel == nullptr)
		{
			return;
		}

		FEditorUIPass* ImGuiPass = Renderer->FindPass<FEditorUIPass>();
		if (ImGuiPass == nullptr)
		{
			// The pass is created after Initialize, so the first call is expected to find nothing.
			return;
		}

		nvrhi::ITexture* Texture = Renderer->GetViewportTarget().GetTexture();
		if (Texture == nullptr)
		{
			return;
		}

		// Reusing the id keeps the panel's handle stable across resizes.
		const ImTextureID TextureId = ImGuiPass->RegisterTexture(Texture, ViewportTextureId);
		if (TextureId != ImTextureID_Invalid)
		{
			ViewportTextureId = TextureId;
			ViewportPanel->SetTextureId(TextureId);
		}
	}

	void FEditorLayer::SubmitViewportSize()
	{
		if (Renderer == nullptr || ViewportPanel == nullptr || !Renderer->IsOffscreenRenderingEnabled())
		{
			return;
		}

		// Applied at the start of the next frame, before anything binds the target.
		Renderer->GetViewportTarget().RequestResize(ViewportPanel->GetDesiredWidth(), ViewportPanel->GetDesiredHeight());
	}

	bool FEditorLayer::IsViewportHovered() const
	{
		// False before the panel has been drawn once, which is the safe answer: it prevents the camera from
		// starting on the very first frame, when no panel has reported hover state yet.
		return ViewportPanel != nullptr && ViewportPanel->IsHovered();
	}

	void FEditorLayer::CreatePanels(const FProjectSettings& ProjectSettings)
	{
		// Built-in panels first, then whatever the project registered.
		ViewportPanel = std::make_shared<FViewportPanel>();
		Panels.push_back(ViewportPanel);
		Panels.push_back(std::make_shared<FSceneHierarchyPanel>());
		Panels.push_back(std::make_shared<FConsolePanel>());
		Panels.push_back(std::make_shared<FStatsPanel>());
		Panels.push_back(std::make_shared<FInspectorPanel>());

		auto SettingsPanel = std::make_shared<FProjectSettingsPanel>();
		SettingsPanel->Initialize(ProjectSettings);
		Panels.push_back(std::move(SettingsPanel));

		// Seeded with the appearance in use, so the panel opens showing what is on screen rather than
		// the code defaults.
		auto AppearancePanel = std::make_shared<FEditorSettingsPanel>();
		AppearancePanel->Initialize(Settings);
		Panels.push_back(std::move(AppearancePanel));

#if LIME_WITH_NODE_EDITOR
		// Constructed here rather than through the panel registry.
		//
		// The registry works by static initialization, which the linker is free to discard for an object
		// file in a static library that nothing else references. That is fine for project panels, which
		// are compiled straight into an executable, but a panel living in this module would simply never
		// appear. Naming the type is what keeps its object file.
		//
		// Held as well as listed, so the automation commands can drive it without searching the panel list
		// and casting; the render graph commands need the concrete type.
		RenderGraphPanel = std::make_shared<FRenderGraphPanel>();
		RenderGraphPanel->Initialize(ProjectSettings);
		Panels.push_back(RenderGraphPanel);
#endif

		for (std::shared_ptr<IEditorPanel>& Panel : FEditorPanelRegistry::Get().InstantiateAll())
		{
			// A project panel may deliberately replace a built-in one by using the same name.
			const auto Existing = std::find_if(Panels.begin(), Panels.end(), [&Panel](const std::shared_ptr<IEditorPanel>& Candidate)
			                                   { return std::string_view(Candidate->GetName()) == std::string_view(Panel->GetName()); });

			if (Existing != Panels.end())
			{
				LIME_LOG_INFO(LIME_LOG_CATEGORY_EDITOR, "Project panel '{}' replaces the built-in one", Panel->GetName());
				*Existing = std::move(Panel);
			}
			else
			{
				Panels.push_back(std::move(Panel));
			}
		}

		// Applied here so every panel, built-in or from a project, honours its declared default.
		for (const std::shared_ptr<IEditorPanel>& Panel : Panels)
		{
			Panel->SetVisible(Panel->IsVisibleByDefault());
		}
	}

	IEditorPanel* FEditorLayer::FindPanel(const char* Name) const
	{
		if (Name == nullptr)
		{
			return nullptr;
		}

		const auto Found = std::find_if(Panels.begin(), Panels.end(), [Name](const std::shared_ptr<IEditorPanel>& Panel)
		                                { return std::string_view(Panel->GetName()) == std::string_view(Name); });
		return Found != Panels.end() ? Found->get() : nullptr;
	}

	void FEditorLayer::Shutdown()
	{
		if (!bInitialized)
		{
			return;
		}

		if (Renderer != nullptr)
		{
			Renderer->SetViewportResizedDelegate(nullptr);
			// The binding set references the viewport texture, so it goes before the target does.
			if (FEditorUIPass* ImGuiPass = Renderer->FindPass<FEditorUIPass>())
			{
				ImGuiPass->UnregisterTexture(ViewportTextureId);
			}
			Renderer->DisableOffscreenRendering();
		}
		ViewportTextureId = ImTextureID_Invalid;
		ViewportPanel.reset();
		Renderer = nullptr;

		Panels.clear();

#if LIME_WITH_IMGUI_TEST_ENGINE
		// Ordering is prescribed by the test engine and cannot be collapsed into one call: Stop()
		// joins the coroutine, which may still be suspended inside a test and would resume against a
		// destroyed context. Destroying the engine has to come last so it can save its own state.
		TestEngine.Shutdown();
#endif

		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

#if LIME_WITH_IMGUI_TEST_ENGINE
		TestEngine.DestroyAfterImGuiContext();
#endif

		bInitialized = false;
		bLayoutBuilt = false;
	}

	void FEditorLayer::PreSwap()
	{
#if LIME_WITH_IMGUI_TEST_ENGINE
		TestEngine.PreSwap();
#endif
	}

	void FEditorLayer::PostSwap()
	{
#if LIME_WITH_IMGUI_TEST_ENGINE
		TestEngine.PostSwap();
#endif
	}

	bool FEditorLayer::IsRequestingMaxAppSpeed() const
	{
#if LIME_WITH_IMGUI_TEST_ENGINE
		return TestEngine.IsRequestingMaxAppSpeed();
#else
		return false;
#endif
	}

	void FEditorLayer::ApplyTheme()
	{
		// The palette comes first, then the shared geometry, then the accent. Order matters: the
		// StyleColors* helpers overwrite every colour, so anything applied before them is lost.
		switch (Settings.Theme)
		{
			case EEditorTheme::Light:
				ImGui::StyleColorsLight();
				break;
			case EEditorTheme::Classic:
				ImGui::StyleColorsClassic();
				break;
			case EEditorTheme::Dark:
				ImGui::StyleColorsDark();
				break;
		}

		ImGuiStyle& Style = ImGui::GetStyle();
		Style.WindowRounding = 4.0f;
		Style.FrameRounding = 3.0f;
		Style.GrabRounding = 3.0f;
		Style.TabRounding = 3.0f;
		Style.ScrollbarRounding = 3.0f;
		Style.WindowBorderSize = 1.0f;
		Style.FrameBorderSize = 0.0f;
		Style.WindowPadding = ImVec2(8.0f, 8.0f);
		Style.FramePadding = ImVec2(6.0f, 3.0f);
		Style.ItemSpacing = ImVec2(8.0f, 5.0f);

		// FontSizeBase rather than FontScaleMain: scaling multiplies the atlas glyphs and blurs them,
		// while the base size is what the font is actually rasterized at.
		Style.FontSizeBase = Settings.FontSize;

		// The dark palette is the one the editor was designed against, so it keeps its hand tuned
		// greys. Light and classic are left as ImGui ships them, since overriding a few colours of a
		// palette that was not tuned here would only produce an inconsistent mix.
		ImVec4* Colors = Style.Colors;
		if (Settings.Theme == EEditorTheme::Dark)
		{
			Colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.12f, 0.14f, 1.00f);
			Colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
			Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);
			Colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.16f, 0.20f, 1.00f);
			Colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.11f, 0.13f, 1.00f);
			Colors[ImGuiCol_Header] = ImVec4(0.18f, 0.21f, 0.26f, 1.00f);
			Colors[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.29f, 0.36f, 1.00f);
			Colors[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.34f, 0.42f, 1.00f);
			Colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.18f, 0.22f, 1.00f);
			Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.25f, 0.31f, 1.00f);
			Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.23f, 0.29f, 1.00f);
			Colors[ImGuiCol_ButtonHovered] = ImVec4(0.27f, 0.32f, 0.40f, 1.00f);
			Colors[ImGuiCol_Tab] = ImVec4(0.13f, 0.15f, 0.18f, 1.00f);
			Colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.31f, 0.39f, 1.00f);
			Colors[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.24f, 0.30f, 1.00f);
		}

		// Applied to every theme, so the accent is what a project actually controls.
		const ImVec4 Accent(Settings.AccentColor.X, Settings.AccentColor.Y, Settings.AccentColor.Z, 1.00f);
		Colors[ImGuiCol_CheckMark] = Accent;
		Colors[ImGuiCol_SliderGrab] = Accent;
		Colors[ImGuiCol_SliderGrabActive] = Accent;
		Colors[ImGuiCol_TabSelectedOverline] = Accent;
	}

	void FEditorLayer::BeginFrame()
	{
		if (!bInitialized)
		{
			return;
		}

		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
	}

	void FEditorLayer::BuildDefaultLayout(ImGuiID DockSpaceId, const ImVec2& DockSize)
	{
		ImGui::DockBuilderRemoveNode(DockSpaceId);
		ImGui::DockBuilderAddNode(DockSpaceId,
		                          static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_PassthruCentralNode) | ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(DockSpaceId, DockSize);

		// Group the panels per slot first, so only the sides that are used get split and so panels
		// sharing a side can be stacked vertically instead of collapsing into tabs.
		std::vector<IEditorPanel*> PerSlot[4];
		for (const std::shared_ptr<IEditorPanel>& Panel : Panels)
		{
			PerSlot[static_cast<SizeType>(Panel->GetDefaultDockSlot())].push_back(Panel.get());
		}

		const auto& LeftPanels = PerSlot[static_cast<SizeType>(EEditorDockSlot::Left)];
		const auto& RightPanels = PerSlot[static_cast<SizeType>(EEditorDockSlot::Right)];
		const auto& BottomPanels = PerSlot[static_cast<SizeType>(EEditorDockSlot::Bottom)];
		const auto& CenterPanels = PerSlot[static_cast<SizeType>(EEditorDockSlot::Center)];

		ImGuiID CentralId = DockSpaceId;
		ImGuiID BottomId = 0;
		ImGuiID LeftId = 0;
		ImGuiID RightId = 0;

		if (!BottomPanels.empty())
		{
			BottomId = ImGui::DockBuilderSplitNode(CentralId, ImGuiDir_Down, 0.26f, nullptr, &CentralId);
		}
		if (!LeftPanels.empty())
		{
			LeftId = ImGui::DockBuilderSplitNode(CentralId, ImGuiDir_Left, 0.17f, nullptr, &CentralId);
		}
		if (!RightPanels.empty())
		{
			RightId = ImGui::DockBuilderSplitNode(CentralId, ImGuiDir_Right, 0.22f, nullptr, &CentralId);
		}

		// Splits a column into one row per panel, so all of them stay visible at once.
		const auto DockColumn = [](ImGuiID ColumnId, const std::vector<IEditorPanel*>& ColumnPanels)
		{
			ImGuiID Remaining = ColumnId;
			for (SizeType Index = 0; Index < ColumnPanels.size(); ++Index)
			{
				const bool bLast = Index + 1 == ColumnPanels.size();
				if (bLast)
				{
					ImGui::DockBuilderDockWindow(ColumnPanels[Index]->GetName(), Remaining);
					break;
				}

				// Divide the space that is still free evenly among the panels left to place.
				const float Ratio = 1.0f / static_cast<float>(ColumnPanels.size() - Index);
				const ImGuiID SliceId = ImGui::DockBuilderSplitNode(Remaining, ImGuiDir_Up, Ratio, nullptr, &Remaining);
				ImGui::DockBuilderDockWindow(ColumnPanels[Index]->GetName(), SliceId);
			}
		};

		if (LeftId != 0)
		{
			DockColumn(LeftId, LeftPanels);
		}
		if (RightId != 0)
		{
			DockColumn(RightId, RightPanels);
		}
		if (BottomId != 0)
		{
			// Bottom panels share one node as tabs; stacking them would leave each too short.
			for (IEditorPanel* Panel : BottomPanels)
			{
				ImGui::DockBuilderDockWindow(Panel->GetName(), BottomId);
			}
		}
		for (IEditorPanel* Panel : CenterPanels)
		{
			ImGui::DockBuilderDockWindow(Panel->GetName(), CentralId);
		}

		ImGui::DockBuilderFinish(DockSpaceId);
		LIME_LOG_INFO(LIME_LOG_CATEGORY_EDITOR, "Default dock layout built at {:.0f}x{:.0f} for {} panel(s)", DockSize.x, DockSize.y,
		              Panels.size());
	}

	void FEditorLayer::DrawDockSpace()
	{
		const ImGuiViewport* Viewport = ImGui::GetMainViewport();

		ImGui::SetNextWindowPos(Viewport->WorkPos);
		ImGui::SetNextWindowSize(Viewport->WorkSize);
		ImGui::SetNextWindowViewport(Viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

		constexpr ImGuiWindowFlags HostFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
		                                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
		                                       ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
		                                       ImGuiWindowFlags_NoBackground;

		ImGui::Begin("LimeEditorDockHost", nullptr, HostFlags);
		ImGui::PopStyleVar(3);

		DrawMenuBar();

		const ImGuiID DockSpaceId = ImGui::GetID("LimeEditorDockSpace");

		// Build the default layout before DockSpace() creates the node. bHasSavedLayout is captured
		// during Initialize, so a user arrangement persisted in the ini file is never overwritten.
		if (!bLayoutBuilt)
		{
			bLayoutBuilt = true;
			if (!bHasSavedLayout)
			{
				BuildDefaultLayout(DockSpaceId, ImGui::GetContentRegionAvail());
			}
		}

		// PassthruCentralNode leaves the middle of the screen transparent so the scene shows through.
		ImGui::DockSpace(DockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

		ImGui::End();
	}

	void FEditorLayer::DrawMenuBar()
	{
		if (!ImGui::BeginMenuBar())
		{
			return;
		}

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Exit", "Alt+F4"))
			{
				ImGui::GetIO().WantCaptureKeyboard = false;
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Window"))
		{
			// Panels appear automatically, so a project panel needs no engine change to be reachable.
			for (const std::shared_ptr<IEditorPanel>& Panel : Panels)
			{
				ImGui::MenuItem(Panel->GetName(), nullptr, Panel->GetVisiblePtr());
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Reset layout"))
			{
				RequestLayoutReset();
			}
			ImGui::MenuItem("ImGui Demo", nullptr, &bShowDemoWindow);
#if LIME_WITH_IMGUI_TEST_ENGINE
			ImGui::MenuItem("UI Tests", nullptr, &bShowTestEngineWindow);
#endif
			ImGui::EndMenu();
		}

		ImGui::EndMenuBar();
	}

	void FEditorLayer::DrawUI(const FEditorContext& Context)
	{
		if (!bInitialized)
		{
			return;
		}

		// The ImGui pass is instantiated after Initialize, so the first frame is where the viewport
		// texture can finally be bound.
		if (ViewportTextureId == ImTextureID_Invalid)
		{
			RefreshViewportTexture();
		}

		DrawDockSpace();

		// A local copy so the engine does not have to know about the selection when building the context,
		// and so panels always receive a valid pointer.
		FEditorContext PanelContext = Context;
		PanelContext.Selection = &Selection;

		// Dropped here, once, rather than checked inside each panel: a scene reload leaves a handle that a
		// later entity can reuse, and validating in one place means no panel can read the wrong entity.
		Selection.Validate(PanelContext.GetScene());

		for (const std::shared_ptr<IEditorPanel>& Panel : Panels)
		{
			if (Panel->IsVisible())
			{
				Panel->OnDrawUI(PanelContext);
			}
		}

		if (bShowDemoWindow)
		{
			ImGui::ShowDemoWindow(&bShowDemoWindow);
		}

#if LIME_WITH_IMGUI_TEST_ENGINE
		// Drawn last so the test list floats above the panels it drives. Only when open: the engine
		// runs tests regardless of whether its window is visible, which is what lets the automation
		// commands work headless.
		if (bShowTestEngineWindow)
		{
			TestEngine.DrawUI(&bShowTestEngineWindow);
		}
#endif
	}

	void FEditorLayer::EndFrame()
	{
		if (!bInitialized)
		{
			return;
		}

		ImGui::Render();
	}
} // namespace Lime
