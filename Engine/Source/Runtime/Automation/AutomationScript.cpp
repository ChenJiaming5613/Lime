#include "Automation/AutomationScript.h"

#include "Core/Logging/LogManager.h"
#include "Platform/PlatformPaths.h"

#include <algorithm>
#include <fstream>

namespace Lime
{
	namespace
	{
		constexpr const char* ScriptDirectoryName = "Automation";
		constexpr const char* ScriptExtension = ".py";
		// A description is a one line summary; a longer leading block is truncated rather than
		// flooding the listing.
		constexpr SizeType MaxDescriptionLength = 120;

		bool EqualsIgnoreCase(std::string_view Left, std::string_view Right)
		{
			return std::equal(Left.begin(), Left.end(), Right.begin(), Right.end(), [](char A, char B)
			                  { return std::tolower(static_cast<unsigned char>(A)) == std::tolower(static_cast<unsigned char>(B)); });
		}

		std::string_view Trim(std::string_view Text)
		{
			const auto First = Text.find_first_not_of(" \t\r\n");
			if (First == std::string_view::npos)
			{
				return {};
			}
			const auto Last = Text.find_last_not_of(" \t\r\n");
			return Text.substr(First, Last - First + 1);
		}
	} // namespace

	std::filesystem::path FAutomationScriptLibrary::GetScriptDirectory()
	{
		std::error_code ErrorCode;

		// The source tree wins, so editing a script does not require a rebuild.
		const std::filesystem::path& ProjectSource = FPlatformPaths::GetProjectSourceDirectory();
		if (!ProjectSource.empty())
		{
			const std::filesystem::path Authored = ProjectSource / ScriptDirectoryName;
			if (std::filesystem::is_directory(Authored, ErrorCode))
			{
				return Authored;
			}
		}

		const std::filesystem::path Deployed = FPlatformPaths::GetExecutableDirectory() / ScriptDirectoryName;
		if (std::filesystem::is_directory(Deployed, ErrorCode))
		{
			return Deployed;
		}
		return {};
	}

	std::vector<FAutomationScript> FAutomationScriptLibrary::Discover()
	{
		std::vector<FAutomationScript> Scripts;

		const std::filesystem::path Directory = GetScriptDirectory();
		if (Directory.empty())
		{
			return Scripts;
		}

		std::error_code ErrorCode;
		for (const std::filesystem::directory_entry& Entry : std::filesystem::directory_iterator(Directory, ErrorCode))
		{
			if (!Entry.is_regular_file(ErrorCode) || Entry.path().extension() != ScriptExtension)
			{
				continue;
			}

			const std::string Stem = FPlatformPaths::ToUtf8(Entry.path().stem());
			// Skips __init__.py and similar: those are module plumbing, not runnable scripts.
			if (Stem.empty() || Stem.starts_with("_"))
			{
				continue;
			}

			FAutomationScript Script;
			Script.Name = Stem;
			Script.Path = Entry.path();
			Script.Description = ReadDescription(Entry.path());
			Scripts.push_back(std::move(Script));
		}

		std::sort(Scripts.begin(), Scripts.end(),
		          [](const FAutomationScript& Left, const FAutomationScript& Right) { return Left.Name < Right.Name; });
		return Scripts;
	}

	bool FAutomationScriptLibrary::Find(std::string_view Name, FAutomationScript& OutScript)
	{
		for (FAutomationScript& Script : Discover())
		{
			if (EqualsIgnoreCase(Script.Name, Name))
			{
				OutScript = std::move(Script);
				return true;
			}
		}
		return false;
	}

	std::string FAutomationScriptLibrary::ReadDescription(const std::filesystem::path& Path)
	{
		std::ifstream File(Path);
		if (!File)
		{
			return {};
		}

		std::string Line;
		while (std::getline(File, Line))
		{
			const std::string_view Trimmed = Trim(Line);
			if (Trimmed.empty())
			{
				continue;
			}

			std::string_view Description = Trimmed;
			// Accepts either a docstring or a leading comment, whichever the script uses.
			if (Description.starts_with(R"(""")") || Description.starts_with("'''"))
			{
				Description.remove_prefix(3);
				if (Description.size() >= 3 && (Description.ends_with(R"(""")") || Description.ends_with("'''")))
				{
					Description.remove_suffix(3);
				}
			}
			else if (Description.starts_with("#"))
			{
				Description.remove_prefix(1);
			}
			else
			{
				// Code before any comment means the script has no description worth showing.
				return {};
			}

			Description = Trim(Description);
			if (Description.empty())
			{
				continue;
			}
			if (Description.size() > MaxDescriptionLength)
			{
				return std::string(Description.substr(0, MaxDescriptionLength)) + "...";
			}
			return std::string(Description);
		}
		return {};
	}
} // namespace Lime
