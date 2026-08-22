// Project automation script discovery.
//
// A project ships its scripts under Automation/ and the engine only needs to find and describe them,
// never to interpret them: they are Python, run by an external process. Startup selection therefore
// means "launch this script against this engine", which keeps the engine free of an interpreter.
//
// Resolution order: the project source tree first, so an edited script is picked up without a
// rebuild, then the copy beside the executable for installed builds.

#pragma once

#include "Core/CoreTypes.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Lime
{
	struct FAutomationScript
	{
		// Stem of the file, which is how a script is named on the command line.
		std::string Name;
		std::filesystem::path Path;
		// First docstring or comment line, used for the listing.
		std::string Description;
	};

	class FAutomationScriptLibrary
	{
	public:
		// Project scripts, sorted by name. Empty when the project ships none.
		static std::vector<FAutomationScript> Discover();

		// Finds one script by name, case insensitively. Returns false when it does not exist.
		static bool Find(std::string_view Name, FAutomationScript& OutScript);

		// Directory holding the scripts, or an empty path when there is none.
		static std::filesystem::path GetScriptDirectory();

	private:
		// Reads the leading docstring or comment block for the listing.
		static std::string ReadDescription(const std::filesystem::path& Path);
	};
} // namespace Lime
