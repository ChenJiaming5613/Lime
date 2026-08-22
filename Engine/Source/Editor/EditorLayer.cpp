#include "Editor/EditorLayer.h"

#include "Core/Logging/LogManager.h"
#include "Platform/PlatformPaths.h"
#include "Platform/Window.h"
#include "Renderer/ImGui/ImGuiRenderer.h"
#include "Renderer/Renderer.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_internal.h>

#include <filesystem>

namespace Lime
{
	FEditorLayer::~FEditorLayer()
	{
		Shutdown();
	}

	bool FEditorLayer::Initialize(FWindow& Window, FRenderer& Renderer)
	{
		if (bInitialized)
		{
			return true;
		}

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

		ApplyDarkTheme();

		// Only the platform backend comes from ImGui; drawing goes through the NVRHI render pass so
		// D3D12 and Vulkan share one implementation.
		if (!ImGui_ImplGlfw_InitForOther(Window.GetHandle(), true))
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_EDITOR, "ImGui_ImplGlfw_InitForOther failed");
			ImGui::DestroyContext();
			return false;
		}

		auto ImGuiPass = std::make_shared<FImGuiRenderer>();
		Renderer.AddPass(ImGuiPass);

		ConsolePanel = std::make_shared<FConsolePanel>();
		InspectorPanel = std::make_shared<FInspectorPanel>();
		Panels.push_back(ConsolePanel);
		Panels.push_back(InspectorPanel);

		bInitialized = true;
		LIME_LOG_INFO(LIME_LOG_CATEGORY_EDITOR, "Editor initialized with {} panel(s)", Panels.size());
		return true;
	}

	void FEditorLayer::Shutdown()
	{
		if (!bInitialized)
		{
			return;
		}

		Panels.clear();
		ConsolePanel.reset();
		InspectorPanel.reset();

		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		bInitialized = false;
		bLayoutBuilt = false;
	}

	void FEditorLayer::ApplyDarkTheme()
	{
		ImGui::StyleColorsDark();

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

		ImVec4* Colors = Style.Colors;
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
		Colors[ImGuiCol_CheckMark] = ImVec4(0.56f, 0.83f, 0.35f, 1.00f);
		Colors[ImGuiCol_SliderGrab] = ImVec4(0.56f, 0.83f, 0.35f, 1.00f);
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
				const ImVec2 DockSize = ImGui::GetContentRegionAvail();

				ImGui::DockBuilderRemoveNode(DockSpaceId);
				ImGui::DockBuilderAddNode(DockSpaceId, static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_PassthruCentralNode) |
				                                           ImGuiDockNodeFlags_DockSpace);
				ImGui::DockBuilderSetNodeSize(DockSpaceId, DockSize);

				ImGuiID CentralId = DockSpaceId;
				const ImGuiID BottomId = ImGui::DockBuilderSplitNode(CentralId, ImGuiDir_Down, 0.28f, nullptr, &CentralId);
				const ImGuiID RightId = ImGui::DockBuilderSplitNode(CentralId, ImGuiDir_Right, 0.24f, nullptr, &CentralId);

				ImGui::DockBuilderDockWindow("Console", BottomId);
				ImGui::DockBuilderDockWindow("Inspector", RightId);
				ImGui::DockBuilderFinish(DockSpaceId);

				LIME_LOG_INFO(LIME_LOG_CATEGORY_EDITOR, "Default dock layout built at {:.0f}x{:.0f}", DockSize.x, DockSize.y);
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
			for (const std::shared_ptr<IEditorPanel>& Panel : Panels)
			{
				ImGui::MenuItem(Panel->GetName(), nullptr, Panel->GetVisiblePtr());
			}
			ImGui::Separator();
			ImGui::MenuItem("ImGui Demo", nullptr, &bShowDemoWindow);
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

		DrawDockSpace();

		for (const std::shared_ptr<IEditorPanel>& Panel : Panels)
		{
			if (Panel->IsVisible())
			{
				Panel->OnDrawUI(Context);
			}
		}

		if (bShowDemoWindow)
		{
			ImGui::ShowDemoWindow(&bShowDemoWindow);
		}
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
