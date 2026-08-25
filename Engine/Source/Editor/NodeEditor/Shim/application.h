// Stands in for the node editor samples' Application base class.
//
// Named application.h, and kept in a directory of its own, because that is the name the samples
// include: they write <application.h> and expect their framework's header. Putting a replacement on the
// include path is what lets the vendored sample compile byte for byte as shipped, so a submodule update
// does not have to be merged by hand. The directory holds nothing else, so the substitution cannot
// affect anything but the sample.
//
// Upstream, that Application owns a window, an ImGui context, a font atlas and a DX11 or OpenGL
// renderer, and drives its own main loop. This engine already owns every one of those, so using that
// framework would mean two windows and two contexts fighting over the same input.
//
// The basic-interaction sample calls none of the texture functions, so they report failure rather than
// pretending to work: a silent stub that returned a plausible id would be harder to diagnose than an
// obviously invalid one. They exist because the base class declares them, not because they are used.

#pragma once

#include <imgui.h>

#include <string>

// The samples refer to this unqualified, at global scope, exactly as their own framework declares it.
struct Application
{
	Application(const char* name)
	    : m_Name(name)
	{
	}
	Application(const char* name, int, char**)
	    : m_Name(name)
	{
	}

	virtual ~Application() = default;

	// Never called: the panel owns the window and the frame loop. Present so the sample's own Main,
	// which is compiled along with the rest of the file, still resolves.
	bool Create(int = -1, int = -1) { return false; }
	int Run() { return 0; }

	void SetTitle(const char*) {}
	bool Close() { return false; }
	void Quit() {}

	const std::string& GetName() const { return m_Name; }

	// The editor's own font, so a sample that pushes these gets something sensible without the engine
	// having to ship the sample's typefaces.
	ImFont* DefaultFont() const { return ImGui::GetIO().FontDefault; }
	ImFont* HeaderFont() const { return ImGui::GetIO().FontDefault; }

	// Unimplemented on purpose; see the note above. A sample needing textures would need the engine's
	// renderer wired in here.
	ImTextureID LoadTexture(const char*) { return ImTextureID_Invalid; }
	ImTextureID CreateTexture(const void*, int, int) { return ImTextureID_Invalid; }
	void DestroyTexture(ImTextureID) {}
	int GetTextureWidth(ImTextureID) { return 0; }
	int GetTextureHeight(ImTextureID) { return 0; }

	virtual void OnStart() {}
	virtual void OnStop() {}
	virtual void OnFrame(float) {}

	virtual ImGuiWindowFlags GetWindowFlags() const { return 0; }
	virtual bool CanClose() { return true; }

private:
	std::string m_Name;
};
