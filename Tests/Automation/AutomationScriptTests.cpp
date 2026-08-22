// Covers project automation script discovery. The description parser is pure text handling, so it is
// tested directly; the directory scan is exercised through a temporary tree.

#include "Platform/PlatformPaths.h"

#include "Automation/AutomationScript.h"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace Lime;

namespace
{
	// Points the library at a temporary directory by way of the project source path, which is the
	// first location it checks.
	class FScopedScriptDirectory
	{
	public:
		explicit FScopedScriptDirectory(const char* Label)
		{
			Previous = FPlatformPaths::GetProjectSourceDirectory();

			Root = std::filesystem::temp_directory_path() / "LimeScriptTests" / Label;
			std::error_code ErrorCode;
			std::filesystem::remove_all(Root, ErrorCode);
			std::filesystem::create_directories(Root / "Automation", ErrorCode);

			FPlatformPaths::SetProjectSourceDirectory(Root);
		}

		~FScopedScriptDirectory()
		{
			FPlatformPaths::SetProjectSourceDirectory(Previous);
			std::error_code ErrorCode;
			std::filesystem::remove_all(Root, ErrorCode);
		}

		LIME_NON_COPYABLE(FScopedScriptDirectory);
		LIME_NON_MOVABLE(FScopedScriptDirectory);

		void Write(const char* FileName, const char* Contents) const
		{
			std::ofstream File(Root / "Automation" / FileName, std::ios::trunc);
			File << Contents;
		}

	private:
		std::filesystem::path Root;
		std::filesystem::path Previous;
	};
} // namespace

TEST_CASE("Scripts are discovered and sorted", "[Automation][Script]")
{
	const FScopedScriptDirectory Directory("sorted");
	Directory.Write("zebra.py", "\"\"\"Last alphabetically.\"\"\"\n\ndef run(engine):\n    pass\n");
	Directory.Write("alpha.py", "\"\"\"First alphabetically.\"\"\"\n\ndef run(engine):\n    pass\n");

	const std::vector<FAutomationScript> Scripts = FAutomationScriptLibrary::Discover();

	REQUIRE(Scripts.size() == 2);
	// A stable order keeps the listing and any batch run reproducible.
	REQUIRE(Scripts[0].Name == "alpha");
	REQUIRE(Scripts[1].Name == "zebra");
	REQUIRE(Scripts[0].Description == "First alphabetically.");
}

TEST_CASE("Only Python files are treated as scripts", "[Automation][Script]")
{
	const FScopedScriptDirectory Directory("filtering");
	Directory.Write("valid.py", "\"\"\"A script.\"\"\"\n");
	Directory.Write("notes.txt", "not a script");
	// Module plumbing is not runnable, so it must not be offered as a choice.
	Directory.Write("__init__.py", "");
	Directory.Write("_helper.py", "\"\"\"Private helper.\"\"\"\n");

	const std::vector<FAutomationScript> Scripts = FAutomationScriptLibrary::Discover();

	REQUIRE(Scripts.size() == 1);
	REQUIRE(Scripts[0].Name == "valid");
}

TEST_CASE("Descriptions are read from docstrings and comments", "[Automation][Script]")
{
	const FScopedScriptDirectory Directory("descriptions");
	Directory.Write("docstring.py", "\"\"\"From a docstring.\"\"\"\n\ndef run(engine):\n    pass\n");
	Directory.Write("comment.py", "# From a comment.\n\ndef run(engine):\n    pass\n");
	Directory.Write("single.py", "'''Single quoted.'''\n");
	// A blank first line must not stop the search.
	Directory.Write("leading_blank.py", "\n\n\"\"\"After blank lines.\"\"\"\n");
	// Code before any comment means there is nothing worth showing.
	Directory.Write("code_first.py", "import sys\n\n\"\"\"Not a docstring.\"\"\"\n");

	std::vector<FAutomationScript> Scripts = FAutomationScriptLibrary::Discover();
	REQUIRE(Scripts.size() == 5);

	const auto Find = [&Scripts](const char* Name) -> const FAutomationScript*
	{
		for (const FAutomationScript& Script : Scripts)
		{
			if (Script.Name == Name)
			{
				return &Script;
			}
		}
		return nullptr;
	};

	REQUIRE(Find("docstring")->Description == "From a docstring.");
	REQUIRE(Find("comment")->Description == "From a comment.");
	REQUIRE(Find("single")->Description == "Single quoted.");
	REQUIRE(Find("leading_blank")->Description == "After blank lines.");
	REQUIRE(Find("code_first")->Description.empty());
}

TEST_CASE("A script is found by name regardless of case", "[Automation][Script]")
{
	const FScopedScriptDirectory Directory("lookup");
	Directory.Write("Smoke.py", "\"\"\"Smoke test.\"\"\"\n");

	FAutomationScript Script;
	REQUIRE(FAutomationScriptLibrary::Find("smoke", Script));
	REQUIRE(Script.Name == "Smoke");
	REQUIRE(FAutomationScriptLibrary::Find("SMOKE", Script));

	REQUIRE_FALSE(FAutomationScriptLibrary::Find("missing", Script));
}

TEST_CASE("A project without a script directory yields nothing", "[Automation][Script]")
{
	// No directory is a normal state, not an error: most projects will not ship scripts.
	const std::filesystem::path Previous = FPlatformPaths::GetProjectSourceDirectory();
	FPlatformPaths::SetProjectSourceDirectory({});

	const std::vector<FAutomationScript> Scripts = FAutomationScriptLibrary::Discover();
	// The executable directory may still hold one, so only the shape of the result is asserted.
	REQUIRE(Scripts.empty() == FAutomationScriptLibrary::GetScriptDirectory().empty());

	FPlatformPaths::SetProjectSourceDirectory(Previous);
}
